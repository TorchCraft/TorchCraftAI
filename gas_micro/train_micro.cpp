/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <fstream>
#include <functional>
#include <thread>

#include <fmt/format.h>
#include <glog/logging.h>
#include <omp.h>
#include <sys/stat.h>

#include "common/assert.h"
#include "common/autograd.h"
#include "common/fsutils.h"
#include "cpid/batcher.h"
#include "cpid/synctrainer.h"
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
#include "flags.h"
#include "gasmicromodule.h"
#include "micromodule.h"
#include "model.h"
#include "rule_module.h"
#include "trainingstate.h"
#include <torchcraft/client.h>

DEFINE_bool(custom_scenario_vary_start, true, "For custom scenarios, vary the starting positions and angle of units");
DEFINE_bool(no_model, false, "Train without a model, just two bot opponents");


using namespace microbattles;
using namespace cherrypi;
namespace fsutils = common::fsutils;

namespace {

TrainingState state;

void setupBot(
    std::function<void(BasePlayer*)> addRulesModule,
    BasePlayer* bot) {
  bot->addModule(Module::make<TopModule>());
  addRulesModule(bot);
  bot->addModule(Module::make<UPCToCommandModule>());
  bot->setLogFailedCommands(false);
  bot->setRealtimeFactor(FLAGS_realtime);
}

void runEnvironmentInThread(
    unsigned int threadId,
    std::shared_ptr<cpid::Trainer> trainer) {
  cpid::distributed::setGPUToLocalRank();
  std::string opponent =
      state.testing ? FLAGS_eval_opponent : FLAGS_train_opponent;
  bool selfPlay = opponent == "self";
  while (!state.finish) {
    try {
      std::string replayFile = "";
      auto createScenariosProvider = [&]() {
        std::shared_ptr<MicroFixedScenario> output;
        if (FLAGS_scenario == "customOutnumber") {
          ScenarioInfo scenario = customAdvantageScenario(
              FLAGS_custom_scenario_unit,
              FLAGS_custom_scenario_enemy,
              FLAGS_custom_scenario_num,
              FLAGS_custom_scenario_advantage,
              FLAGS_custom_scenario_vary_start,
              FLAGS_custom_scenario_angle,
              FLAGS_custom_scenario_sep
          );
          output = std::make_shared<MicroFixedScenario>(
              FLAGS_max_frames - 1,
              scenario,
              FLAGS_enable_gui && threadId == 0);
        }
        else {
          output = std::make_shared<MicroFixedScenario>(
              FLAGS_max_frames - 1,
              FLAGS_scenario,
              FLAGS_enable_gui && threadId == 0);
        }
        output->setMapPathPrefix(FLAGS_map_path_prefix);
        return output;
      };
      auto provider = createScenariosProvider();
      std::function<void(PlayerId, BasePlayer*)> setupLearningLearningModule =
          [&](PlayerId playerId, BasePlayer* bot) {
            std::shared_ptr<MicroModule> module;
            if (state.setup->gasMode) {
              module = Module::make<GasMicroModule>(
                  state.setup,
                  trainer,
                  combatDeltaReward(
                      /* dmgScale */ FLAGS_dmg_scale,
                      /* ally dmgScale */ FLAGS_dmg_taken_scale,
                      /* deathScale */ FLAGS_death_scale,
                      /* killScale */ FLAGS_kill_scale,
                      /* winScale */ FLAGS_win_scale));
            } else {
              module = Module::make<MicroModule>(
                  state.setup, trainer, provider->getReward(playerId));
            }
            bool isMainThread =
                cpid::distributed::globalContext()->rank == 0 && threadId == 0;
            module->setIllustrate(
                FLAGS_illustrate && (isMainThread || !replayFile.empty()));
            module->setGenerateHeatmaps(
                !FLAGS_visdom_env.empty() && isMainThread);
            bot->addModule(module);\
            if (FLAGS_no_model){
                bot->addModule(Module::make<DummyTacticsModule>());
                bot->addModule(Module::make<SquadCombatAttackClosest>());
            }
          };
      std::function<void(BasePlayer*)> setupRulesBasedModule =
          [&](BasePlayer* bot) {
            auto modules = getCombatModules(opponent);
            for (auto& module : modules) {
              bot->addModule(module);
            }
          };
      auto respawn = [&]() {
        if (FLAGS_scenario == "customOutnumber") {
          ScenarioInfo scenario = customAdvantageScenario(
              FLAGS_custom_scenario_unit,
              FLAGS_custom_scenario_enemy,
              FLAGS_custom_scenario_num,
              FLAGS_custom_scenario_advantage,
              FLAGS_custom_scenario_vary_start,
              FLAGS_custom_scenario_angle,
              FLAGS_custom_scenario_sep
          );
          provider->loadScenario(scenario);
        }
        else {
          provider->loadScenario(FLAGS_scenario);
        }
        return provider->spawnNextScenario(
            // Player 0 is always learning, and dumps the replay if needed
            [&](BasePlayer* bot) {
              /*
              if (FLAGS_no_model) {
                setupBot(
                    setupRulesBasedModule,
                    bot);
              }
              else {
              */
              setupBot(
                  std::bind(
                      setupLearningLearningModule,
                      PlayerId(0),
                      std::placeholders::_1),
                  bot);
              //}
              if (!replayFile.empty()) {
                bot->dumpTraceAlongReplay(replayFile);
              }
            },
            // Player 1 can be learning (selfplay case)
            std::bind(
                setupBot,
                selfPlay ? std::bind(
                               setupLearningLearningModule,
                               PlayerId(1),
                               std::placeholders::_1)
                         : setupRulesBasedModule,
                std::placeholders::_1));
      };

      uint64_t gamesPlayed = 0;
      while (!state.finish) {
        provider->cleanScenario();
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
        auto microModule = p1->findModule<MicroModule>();
        auto microModuleOpp = p2->findModule<MicroModule>();
        int nsteps = 0;
        microModule->handle_ = std::move(episodes[0]);
        if (microModuleOpp) {
          microModuleOpp->handle_ = std::move(episodes[1]);
        }
        auto isAborted = [&]() {
          return state.finish || !microModule->handle_ ||
              (microModuleOpp && !microModuleOpp->handle_);
        };

        // Quit only if:
        //  - we're done
        //  - game isn't active anymore, trainer says we should stop
        while (!provider->isFinished(nsteps, false /*checkAttack*/) &&
               !isAborted()) {
          // If all handles are no longer active
          if (!trainer->isActive(microModule->handle_)) {
            if (!microModuleOpp ||
                !trainer->isActive(microModuleOpp->handle_)) {
              break;
            }
          }

          p1->step();
          p2->step();
          nsteps++;
          state.throughputCounter++;
          if (microModuleOpp) {
            state.throughputCounter++;
          }
        }
        if (isAborted()) {
          microModule->handle_ = cpid::EpisodeHandle();
          if (microModuleOpp) { // Self-play
            microModuleOpp->handle_ = cpid::EpisodeHandle();
          }
        } else { // If not first episode

          // Note that this is called BEFORE microModule's onGameEnd() happens
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

int run(int argc, char** argv) {
  namespace dist = cpid::distributed;
  using namespace microbattles;

  cherrypi::init();
  dist::init();
  cherrypi::initLogging(argv[0], "", true);
  ForkServer::startForkServer();

  VLOG(0) << fmt::format("Scenario: {}", FLAGS_scenario);
  VLOG(0) << fmt::format("Model: {}", FLAGS_model);
  VLOG(0) << fmt::format("Resume: {}", FLAGS_resume);
  VLOG(0) << fmt::format("Evaluate: {}", FLAGS_evaluate);

  std::string resultsDir = FLAGS_results;
  std::string resultsJSON = fmt::format(
      "{}/metrics-rank-{}.json", resultsDir, dist::globalContext()->rank);
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
      VLOG(0) << "Found existing trainer! Loading it from " << resumeModel;
      if (!setup->loadTrainer(resumeModel)) {
        VLOG(0) << "Cannot load it as a trainer from " << resumeModel;
        if (!setup->loadModel(resumeModel)) {
          VLOG(0) << "Cannot load it as a model either! " << resumeModel;
          throw std::runtime_error("Cannot resume due to loading issues!");
        }
      }
      auto syncTrainer = std::dynamic_pointer_cast<cpid::SyncTrainer>(setup->trainer);
      if (syncTrainer != nullptr) {
        VLOG(-3) << "Starting training at " << syncTrainer->getUpdateCount();  
        state.numUpdates = syncTrainer->getUpdateCount();
      }
      std::string resumeDir = fsutils::dirname(resumeModel);
      std::string resumeJSON = fmt::format(
          "{}/metrics-rank-{}.json", resumeDir, dist::globalContext()->rank);
      if (fsutils::exists(resumeJSON)) {
        VLOG(0) << "Found existing metrics! Loading them from " << resumeJSON;
        state.metrics->loadJson(resumeJSON);
        setup->trainer->setMetricsContext(state.metrics);
      } else {
        VLOG(0) << "Failed to find existing json at " << resumeJSON;
      }
    }

  } else {
    VLOG(0) << "Directory to resume from is empty, starting from new model";
  }
  state.checkpointer = std::make_unique<cpid::Checkpointer>(setup->trainer);
  state.checkpointer->checkpointPath(resultsDir);
  state.checkpointer->epochLength(FLAGS_updates_per_epoch);
  setup->trainer->setMetricsContext(state.metrics);

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
  auto threads = std::vector<std::thread>();
  auto startWorkers = [&](std::shared_ptr<cpid::Trainer> workingTrainer) {
    state.finish.store(false);
    for (std::size_t i = 0; i < FLAGS_num_threads; i++) {
      threads.emplace_back(runEnvironmentInThread, i, workingTrainer);
    }
  };
  auto stopWorkers = [&](std::shared_ptr<cpid::Trainer> workingTrainer) {
    state.finish.store(true);
    workingTrainer->reset();
    for (auto& t : threads) {
      t.join();
    }
    threads.clear();
  };
  // sets up the evaluation and cleans it aftewards
  auto evaluate = [&]() {
    stopWorkers(setup->trainer);
    state.testing = true;
    auto model = setup->trainer->model();
    model->eval();
    auto evaluator = setup->trainer->makeEvaluator(
        FLAGS_num_test_episodes, setup->createSampler());
    startWorkers(evaluator);
    while (!evaluator->update()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    VLOG(0) << "stopping evaluator";
    stopWorkers(evaluator);
    model->train();
    state.testing = false;
    state.printTestResult();
    state.metrics->dumpJson(resultsJSON);
  };

  state.startTime = cherrypi::hires_clock::now();
  if (FLAGS_evaluate) {
    evaluate();
    return EXIT_SUCCESS;
  }
  startWorkers(setup->trainer);
  while (!state.worker || !state.worker->isDone()) {
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
      if ((numUpdates + 1) % FLAGS_stats_freq == 0) {
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
              "episode_t",
              "Episode @Training",
              "episode",
              numUpdates,
              nEpisodes);
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
      }

      lock.unlock();
      if ((numUpdates + 1) % FLAGS_test_freq == 0) {
        VLOG(0) << "evaluating";
        evaluate();
        startWorkers(setup->trainer);
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
  evaluate();
  VLOG(0) << "Done!" << std::endl;
  return EXIT_SUCCESS;
}

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (FLAGS_list_scenarios) {
    auto scenarioNames = cherrypi::MicroFixedScenario::listScenarios();
    for (auto& scenarioName : scenarioNames) {
      std::cout << scenarioName << std::endl;
    }
    return EXIT_SUCCESS;
  }

  auto ompNumThreads = std::getenv("OMP_NUM_THREADS");
  if (ompNumThreads == nullptr || strlen(ompNumThreads) == 0) {
    std::cout << "Warning: OMP_NUM_THREADS not specified; the default value is "
                 "80 when it should probably be 1."
              << std::endl;
  }

  return run(argc, argv);
}
