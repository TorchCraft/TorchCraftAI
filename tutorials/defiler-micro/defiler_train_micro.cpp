/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <csignal>
#include <fstream>
#include <functional>
#include <thread>

#include <fmt/format.h>
#include <glog/logging.h>
#include <sys/stat.h>

#include "common/assert.h"
#include "common/autograd.h"
#include "common/fsutils.h"
#include "cpid/batcher.h"
#include "cpid/distributed.h"
#include "cpid/evaluator.h"
#include "cpid/optimizers.h"
#include "cpid/sampler.h"
#include "gameutils/openbwprocess.h"
#include "microplayer.h"
#include "modules.h"
#include "player.h"
#include "trainingsetup.h"
#include "utils.h"

#include "common.h"
#include "defilercustomization.h"
#include "defilermicromodule.h"
#include "flags.h"
#include "micromodule.h"
#include "model.h"
#include "rule_module.h"
#include "trainingstate.h"
#include <torchcraft/client.h>

#include "gameutils/microscenarioproviderfixed.h"
#include "gameutils/microscenarioprovidersnapshot.h"

using namespace microbattles;
using namespace cherrypi;
namespace fsutils = common::fsutils;

namespace {

DEFINE_string(
    scenario,
    "5vu_10zl",
    "Scenarios (refer to gameutils/fixedscenarios.cpp)");
DEFINE_string(snapshot_directory, "", "Directory to look for snapshots");
DEFINE_string(
    map,
    "",
    "Path to a map to use instead of the map defined by the scenario.");
DEFINE_bool(
    list_scenarios,
    false,
    "Just print out the list of available scenarios and exit.");
DEFINE_int32(
    combine_frames,
    1,
    "Number of BWAPI frames to step for each TorchCraft frame");
DEFINE_int32(
    test_episodes_each_snapshot,
    10,
    "Number of testing episodes using one snapshot");

TrainingState state;

void setupBot(
    std::function<void(BasePlayer*)> addRulesModule,
    BasePlayer* bot) {
  bot->addModule(Module::make<TopModule>());
  addRulesModule(bot);
  bot->addModule(Module::make<UPCToCommandModule>());
  bot->setLogFailedCommands(false);
  bot->setRealtimeFactor(FLAGS_realtime);
  if (auto p = dynamic_cast<Player*>(bot)) {
    p->setMapHack(true);
  }
}

std::shared_ptr<MicroScenarioProvider> createScenarioProvider(
    bool training,
    unsigned int threadId) {
  if (FLAGS_snapshot_directory.empty()) {
    auto output = std::make_shared<MicroScenarioProviderFixed>(FLAGS_scenario);
    output->loadScenario(FLAGS_scenario);
    return output;
  }
  auto output = std::make_shared<MicroScenarioProviderSnapshot>();
  output->setSnapshotDirectory(FLAGS_snapshot_directory);
  output->setIndexFile(
      FLAGS_snapshot_directory + (training ? "/train.list" : "/valid.list"));
  if (FLAGS_print_rewards) {
    output->setIndexFile(FLAGS_snapshot_directory + "/all.list");
  }
  output->setPartitionSize(
      FLAGS_num_threads * cpid::distributed::globalContext()->size);
  output->setPartitionIndex(
      (int)threadId +
      cpid::distributed::globalContext()->rank * FLAGS_num_threads);
  if (!training) {
    output->setUseEachSnapshotTimes(FLAGS_test_episodes_each_snapshot);
  }
  return output;
}

void runEnvironmentInThread(
    unsigned int threadId,
    std::shared_ptr<cpid::Trainer> trainer,
    std::atomic<bool>& keepRunning) {
  common::setCurrentThreadName(fmt::format("game_t{}", threadId));
  cpid::distributed::setGPUToLocalRank();
  std::string opponent =
      state.testing ? FLAGS_eval_opponent : FLAGS_train_opponent;
  bool selfPlay = opponent == "self";
  while (keepRunning.load()) {
    try {
      std::string replayFile = "";
      auto provider = createScenarioProvider(!FLAGS_evaluate, threadId);
      provider->setMaxFrames(FLAGS_max_frames - 1);
      provider->setCombineFrames(FLAGS_combine_frames);
      provider->setGui(FLAGS_gui && threadId == 0);
      provider->setMapPathPrefix(FLAGS_map_path_prefix);
      provider->forceMap(FLAGS_map);

      std::function<void(BasePlayer*)> setupLearningLearningModule =
          [&](BasePlayer* bot) {
            std::vector<std::shared_ptr<Module>> modules;
            std::shared_ptr<MicroModule> module;
            if (FLAGS_defiler_behavior != "") {
              module = Module::make<DefilerMicroModule>(
                  // Modify the reward accordingly when you want to use
                  // different rewards
                  state.setup,
                  trainer,
                  defilerFullGameCombatReward());
              modules = addFullGameDefilerModules(module);
            } else {
              module = Module::make<MicroModule>(
                  state.setup, trainer, provider->getReward());
              modules.push_back(module);
            }
            bool isMainThread =
                cpid::distributed::globalContext()->rank == 0 && threadId == 0;
            module->setIllustrate(
                FLAGS_illustrate && (isMainThread || !replayFile.empty()));
            module->setGenerateHeatmaps(
                !FLAGS_visdom_env.empty() && isMainThread);
            bot->addModules(modules);
          };
      std::function<void(BasePlayer*)> setupRulesBasedModule =
          [&](BasePlayer* bot) {
            auto modules = getCombatModules(opponent);
            bot->addModules(modules);
          };

      auto respawn = [&]() {
        return provider->startNewScenario(
            // Player 0 is always learning, and dumps the replay if needed
            [&](BasePlayer* bot) {
              setupBot(setupLearningLearningModule, bot);
              if (!replayFile.empty()) {
                bot->dumpTraceAlongReplay(replayFile);
              }
            },
            // Player 1 can be learning (selfplay case)
            std::bind(
                setupBot,
                selfPlay ? setupLearningLearningModule : setupRulesBasedModule,
                std::placeholders::_1));
      };

      uint64_t gamesPlayed = 0;
      while (keepRunning.load()) {
        // End any existing scenarios, invoking onGameEnd() for its players.
        // ESTrainer may require a current batch of episodes to finish before
        // allowing others to begin (see startEpisode()).
        //
        // The try catch loop is required here since the underlying game might
        // be dead due to exception and the guard will be called anyway
        auto guardEndScenario = common::makeGuard([&] {
          try {
            provider->endScenario();
          } catch (std::exception const& cleaningException) {
            VLOG(0) << std::string("Caught exception in ending scenarios: ") +
                    cleaningException.what();
          } catch (...) {
            VLOG(0) << "Caught unknown exception in ending scenarios";
          }
        });
        std::array<cpid::EpisodeHandle, 2> episodes;
        episodes[0] = trainer->startEpisode();
        if (selfPlay) {
          episodes[1] = trainer->startEpisode();
        }
        if (!episodes[0] || (selfPlay && !episodes[1])) {
          // Free episode if any and try again later
          episodes = std::array<cpid::EpisodeHandle, 2>();
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          continue;
        }
        replayFile = state.computeReplayPath(threadId, gamesPlayed);
        provider->setReplay(replayFile);
        auto setup = respawn();
        gamesPlayed++;
        auto p1 = setup.first;
        auto p2 = setup.second;
        auto getFrame = [&]() { return p1->state()->currentFrame(); };
        int initialFrame = getFrame();
        int currentFrame = initialFrame;
        auto microModule = findMicroModule(p1);
        auto microModuleOpp = findMicroModule(p2);
        microModule->scenarioName = provider->getLastScenarioName();
        if (state.baselineRewards.find(microModule->scenarioName) !=
            state.baselineRewards.end()) {
          microModule->frameRewards =
              state.baselineRewards[microModule->scenarioName];
        }
        microModule->test = state.testing;
        microModule->handle = std::move(episodes[0]);
        if (microModuleOpp) {
          microModuleOpp->handle = std::move(episodes[1]);
        }
        auto isAborted = [&]() {
          return !keepRunning.load() || !microModule->handle ||
              (microModuleOpp && !microModuleOpp->handle) ||
              p1->state()->gameEnded() || p2->state()->gameEnded();
        };
        // Quit only if:
        //  - we're done
        //  - game isn't active anymore, trainer says we should stop
        while (!provider->isFinished(currentFrame - initialFrame) &&
               !isAborted()) {
          // If all handles are no longer active
          if (!trainer->isActive(microModule->handle)) {
            if (!microModuleOpp || !trainer->isActive(microModuleOpp->handle)) {
              break;
            }
          }

          p1->step();
          p2->step();
          currentFrame = getFrame();
          state.throughputCounter++;
          if (microModuleOpp) {
            state.throughputCounter++;
          }
        }
        if (isAborted()) {
          microModule->handle = cpid::EpisodeHandle();
          if (microModuleOpp) { // Self-play
            microModuleOpp->handle = cpid::EpisodeHandle();
          }
        } else { // If not first episode
          // We need to call onGameEnd before we calculate any metrics
          provider->endScenario();
          guardEndScenario.dismiss();
          if (state.testing) {
            state.addStatsTesting(p1);
            if (microModuleOpp) { // Self-play
              state.addStatsTesting(p2);
            }
          } else {
            state.addStatsTraining(p1);
            if (microModuleOpp) { // Self-play
              state.addStatsTraining(p2);
            }
          }
        }
      }
    } catch (std::exception const& trainingException) {
      VLOG(0) << std::string("Caught exception in training loop: ") +
              trainingException.what();
    } catch (...) {
      VLOG(0) << "Caught unknown exception in training loop";
    }
  }
}
} // namespace

struct WorkerThreadsRAII {
  WorkerThreadsRAII(std::shared_ptr<cpid::Trainer> t) : trainer(t) {
    for (std::size_t i = 0; i < FLAGS_num_threads; i++) {
      threads.emplace_back(
          runEnvironmentInThread, i, trainer, std::ref(keepRunning));
    }
  }
  ~WorkerThreadsRAII() {
    keepRunning.store(false);
    trainer->reset();
    for (auto& t : threads) {
      t.join();
    }
    threads.clear();
  }

  std::atomic<bool> keepRunning{true};
  std::shared_ptr<cpid::Trainer> trainer;
  std::vector<std::thread> threads;
};

void onSignalInt(int) {
  VLOG(0) << "SIGINT caught, shutting down...";
  VLOG(0) << "(press CTRL+C again to force exit now)";
  state.shouldExit.store(true);
  OpenBwProcess::preventFurtherProcesses();
  ForkServer::endForkServer();
  std::signal(SIGINT, SIG_DFL);
}

int run(int argc, char** argv) {
  namespace dist = cpid::distributed;
  using namespace microbattles;

  cherrypi::init();
  dist::init();
  cherrypi::initLogging(argv[0], "", true);
  ForkServer::startForkServer();

  std::signal(SIGINT, onSignalInt);

  VLOG(0) << fmt::format("Scenario: {}", FLAGS_scenario);
  VLOG(0) << fmt::format("Model: {}", FLAGS_model);
  VLOG(0) << fmt::format("Resume: {}", FLAGS_resume);
  VLOG(0) << fmt::format("Evaluate: {}", FLAGS_evaluate);

  std::string resultsDir = FLAGS_results;
  std::string resultsJSON = fmt::format(
      "{}/metrics-rank-{}.json", resultsDir, dist::globalContext()->rank);
  state.baselineDumpPath = fmt::format(
      "{}/rewards-rank-{}.json", resultsDir, dist::globalContext()->rank);
  std::string resultsCheckpoint = resultsDir + "/train_micro.bin";

  VLOG(0) << "resultsJSON: " << resultsJSON;
  VLOG(0) << "resultsCheckpoint: " << resultsCheckpoint;

  if (dist::globalContext()->rank == 0) {
    fsutils::mkdir(resultsDir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  }

  // Cpid2k setup
  state.worker = cpid::Cpid2kWorker::fromEnvVars();
  if (state.worker) {
    VLOG(0) << "WITH cpid2k worker id " << state.worker->info().id << " prefix "
            << state.worker->prefix();
  } else {
    VLOG(0) << "WITHOUT cpid2k";
  }

  // Set up the trainer / model
  auto setup = std::make_shared<TrainingSetup>();
  state.setup = setup;

  std::string resumeModel = FLAGS_resume;
  if (fsutils::isdir(resumeModel)) {
    resumeModel += "/trainer_latest.bin";
  }

  if (!resumeModel.empty()) {
    if (!fsutils::exists(resumeModel)) {
      VLOG(0) << "Failed to find existing model at " << resumeModel;
    } else {
      VLOG(0) << "Found existing model! Loading it from " << resumeModel;
      if (!setup->loadModel(resumeModel)) {
        VLOG(0) << "Cannot load it as a model! loading it as trainer from "
                << resumeModel;
        if (!setup->loadTrainer(resumeModel)) {
          VLOG(0) << "Cannot load it as a trainer either from " << resumeModel;
          throw std::runtime_error("Cannot resume due to loading issues!");
        }
      }

      std::string resumeDir = fsutils::dirname(resumeModel);
      std::string resumeJSON = fmt::format(
          "{}/metrics-rank-{}.json", resumeDir, dist::globalContext()->rank);
      if (fsutils::exists(resumeJSON)) {
        VLOG(0) << "Found existing metrics! Loading them from " << resumeJSON;
        state.metrics->loadJson(resumeJSON);
      } else {
        VLOG(0) << "Failed to find existing json at " << resumeJSON;
      }
    }
  }
  state.checkpointer = std::make_unique<cpid::Checkpointer>(setup->trainer);
  state.checkpointer->checkpointPath(resultsDir);
  state.checkpointer->epochLength(FLAGS_updates_per_epoch);
  setup->trainer->setMetricsContext(state.metrics);

  if (FLAGS_train_on_baseline_rewards && !FLAGS_print_rewards) {
    state.baselineLoadPath =
        fmt::format("{}/rewards.json", FLAGS_snapshot_directory);
    state.loadBaselineRewards();
  }

  {
    auto nParams = 0;
    for (auto& p : setup->model->parameters()) {
      nParams += p.numel();
    }
    VLOG(0) << fmt::format("Model has {} total parameters", nParams);
  }

  dist::broadcast(setup->model);

  if (dist::globalContext()->rank == 0 && !FLAGS_visdom_env.empty()) {
    visdom::ConnectionParams vparams;
    vparams.server = FLAGS_visdom_server;
    vparams.port = FLAGS_visdom_port;
    setup->setVisdom(vparams, FLAGS_visdom_env);

    std::ostringstream oss;
    oss << "<h4>Micro Training</h4>";
    oss << "<p>Training started " << cherrypi::utils::curTimeString() << "</p>";
    oss << "<hl><p>";
    char const* slurmJobId = std::getenv("SLURM_JOBID");
    if (slurmJobId != nullptr) {
      oss << "<b>"
          << "slurm_job_id"
          << "</b>: " << slurmJobId << "<br>";
    }
    for (auto const& it : cherrypi::utils::cmerge(
             cherrypi::utils::gflagsValues(
                 fsutils::dirname(__FILE__) + "/flags.cpp"),
             cpid::optimizerFlags())) {
      oss << "<b>" << it.first << "</b>: " << it.second << "<br>";
    }
    oss << "</p>";
    setup->vs->text(oss.str());
    state.saveModelParams();
  }

  // Worker functions
  VLOG(0) << (FLAGS_evaluate ? "Begin evaluating." : "Begin training!");
  VLOG_IF(0, FLAGS_print_rewards && FLAGS_evaluate)
      << "Dumping baseline reward to " << state.baselineDumpPath;
  auto threads = std::vector<std::thread>();
  // sets up the evaluation and cleans it aftewards
  auto evaluate = [&]() {
    state.testing = true;
    auto model = setup->trainer->model();
    model->eval();
    auto evaluator = setup->trainer->makeEvaluator(
        FLAGS_num_test_episodes, setup->createSampler("max"));
    { // Evaluation threads scope
      WorkerThreadsRAII evalThreads(evaluator);
      while (!evaluator->update() && !state.shouldExit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
    model->train();
    state.testing = false;
    state.printTestResult();
    state.metrics->dumpJson(resultsJSON);
    if (FLAGS_print_rewards) {
      state.dumpBaselineRewards();
    }
  };

  state.startTime = cherrypi::hires_clock::now();
  if (FLAGS_evaluate) {
    evaluate();
    return EXIT_SUCCESS;
  }
  std::optional<WorkerThreadsRAII> trainThreads(setup->trainer);
  while (!state.worker || !state.worker->isDone()) {
    if (state.shouldExit) {
      return EXIT_SUCCESS;
    }
    if (setup->trainer->update()) {
      std::unique_lock lock(state.statMutex);
      state.numUpdates++;
      state.checkpointer->updateDone(state.numUpdates.load());
      auto nEpisodes = state.numTrainEpisodes.load();
      auto framesSoFar = state.throughputCounter.load();
      auto numUpdates = state.numUpdates.load();
      using ms = std::chrono::duration<double, std::milli>;
      ms duration = cherrypi::hires_clock::now() - state.startTime;
      VLOG_MASTER(2) << common::WeightSummary(*setup->model).toString();

      for (auto& pair : setup->model->named_parameters()) {
        VLOG_ALL(1) << pair.key() << ": norm "
                    << pair.value().norm().item<float>() << " gradient "
                    << pair.value().grad().norm().item<float>();
        common::checkTensor(pair.value());
        common::checkTensor(pair.value().grad());
      }

      fmt::print(
          "rank {}\t"
          "episode {}\t"
          "avg_length {:.1f}\t"
          "avg_pop_reward {:.3f}\t"
          "forward/sec {:.3f}\t"
          "\n",
          dist::globalContext()->rank,
          nEpisodes,
          state.trainMetrics["avgSteps"],
          state.trainMetrics["avgReward"],
          1000. * framesSoFar / duration.count() / FLAGS_frame_skip);

      for (auto const& [key, val] : state.trainMetrics) {
        state.metrics->pushEvent(key + ".training", val);
      }
      state.metrics->pushEvent(
          "forward/sec.training",
          1000. * framesSoFar / duration.count() / FLAGS_frame_skip);
      if (state.worker) {
        state.worker->appendMetrics(
            "train",
            {{"avgSteps", state.trainMetrics["avgSteps"]},
             {"avgReward", state.trainMetrics["avgReward"]},
             {"update", state.numUpdates.load()}});
      }

      if (dist::globalContext()->rank == 0 && setup->vs) {
        auto delta = state.getDelta();
        setup->updatePlot(
            "episode_t", "Episode @Training", "episode", numUpdates, nEpisodes);
        setup->updatePlot(
            "forward/sec_t",
            "Forwards per sec @Training",
            "time",
            numUpdates,
            1000. * framesSoFar / duration.count() / FLAGS_frame_skip);
        for (auto const& [key, val] : state.trainMetrics) {
          setup->updatePlot(
              key + "_t", key + " @training", "", numUpdates, val);
        }
        if (FLAGS_debug_update) {
          for (auto& pair : setup->trainer->model()->named_parameters()) {
            common::checkTensor(pair.value());
            setup->updatePlot(
                pair.key() + "_norm",
                pair.key() + " norm",
                "",
                numUpdates,
                pair.value().norm().item<float>());
            common::checkTensor(pair.value().grad());
            setup->updatePlot(
                pair.key() + "_grad",
                pair.key() + " grad",
                "",
                numUpdates,
                pair.value().grad().norm().item<float>());
            if (delta.find(pair.key()) != delta.end()) {
              setup->updatePlot(
                  pair.key() + "_update",
                  pair.key() + " update",
                  "",
                  numUpdates,
                  delta[pair.key()]);
            }
          }
        }
      }

      lock.unlock();
      if ((numUpdates + 1) % FLAGS_test_freq == 0) {
        trainThreads.reset();
        evaluate();
        trainThreads.emplace(setup->trainer);
        if (dist::globalContext()->rank == 0) {
          ag::save(
              fmt::format("model_u{:05d}.bin", numUpdates),
              setup->trainer->model());
        }
      }

      state.saveModelParams();
      dist::allreduce(&nEpisodes, 1);
      if ((uint64_t)nEpisodes >= FLAGS_max_episodes) {
        break;
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  trainThreads.reset();
  evaluate();
  VLOG(0) << "Done!" << std::endl;
  return EXIT_SUCCESS;
}

void listScenarios() {
  auto scenarioNames = cherrypi::MicroScenarioProviderFixed::listScenarios();
  for (auto& scenarioName : scenarioNames) {
    std::cout << scenarioName << std::endl;
  }
}

void verifyOMPNumThreads() {
  auto ompNumThreads = std::getenv("OMP_NUM_THREADS");
  if (ompNumThreads == nullptr || strlen(ompNumThreads) == 0) {
    std::cout << "Warning: OMP_NUM_THREADS not specified; the default value is "
                 "80 when it should probably be 1."
              << std::endl;
  }
}

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (FLAGS_list_scenarios) {
    listScenarios();
    return EXIT_SUCCESS;
  }
  verifyOMPNumThreads();

  return run(argc, argv);
}
