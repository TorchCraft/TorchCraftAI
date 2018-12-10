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
#include <sys/stat.h>

#include "common/autograd.h"
#include "cpid/batcher.h"
#include "cpid/distributed.h"
#include "cpid/evaluator.h"
#include "cpid/optimizers.h"
#include "cpid/sampler.h"
#include "fsutils.h"
#include "microplayer.h"
#include "modules.h"
#include "player.h"
#include "trainingsetup.h"

#include "common.h"
#include "flags.h"
#include "micromodule.h"
#include "model.h"
#include "rule_module.h"

namespace {

struct TrainingState {
  // Training stats
  std::mutex statMutex;
  std::shared_ptr<cpid::MetricsContext> metrics =
      std::make_shared<cpid::MetricsContext>();
  std::atomic<long> throughputCounter;
  std::atomic<int> numTrainEpisodes;
  std::atomic<int> numUpdates;
  double avgSteps = 0.;
  double avgReward = 0.;

  // Testing stats
  double avgTestSteps = 0;
  double avgTestReward = 0;
  std::vector<float> testWins;
  std::vector<float> testKills;
  std::vector<float> testDamage;

  // Actual state variables
  std::atomic_bool finish{false};
  std::shared_ptr<microbattles::TrainingSetup> training;
  cherrypi::hires_clock::time_point startTime;

  bool testing = false;
} state;

void runEnvironmentInThread(
    unsigned int threadId,
    std::shared_ptr<cpid::Trainer> trainer) {
  using namespace microbattles;
  using namespace cherrypi;

  while (!state.finish) {
    std::shared_ptr<MicroModule> microModule;
    try {
      auto provider = std::make_shared<MicroFixedScenario>(
          FLAGS_max_frames - 1,
          FLAGS_scenario,
          FLAGS_enable_gui && threadId == 0);
      std::string replayFile = "";
      auto respawn = [&]() {
        provider->setSpawns(FLAGS_scenario);
        return provider->spawnNextScenario(
            [&](BasePlayer* bot) {
              bot->addModule(Module::make<TopModule>());
              bot->addModule(
                  Module::make<MicroModule>(
                      threadId, state.training, trainer, provider->getReward()));
              bot->addModule(Module::make<UPCToCommandModule>());
              bot->setLogFailedCommands(false);
              bot->setRealtimeFactor(FLAGS_realtime);
              if (!replayFile.empty()) {
                bot->dumpTraceAlongReplay(replayFile);
              }
            },
            [&](BasePlayer* bot) {
              bot->addModule(Module::make<TopModule>());
              bot->addModule(Module::make<RuleModule>());
              bot->addModule(Module::make<UPCToCommandModule>());
              bot->setLogFailedCommands(false);
              bot->setRealtimeFactor(-1);
            });
      };
      int nsteps = 0;
      auto computeReplayPath = [&]() -> std::string {
        if ((rand() % std::min(FLAGS_dump_replays_rate, 1UL)) != 0) {
          return "";
        }
        if (FLAGS_dump_replays == "never") {
          return "";
        }
        else if (FLAGS_dump_replays == "eval" && !state.testing) {
          return "";
        }
        else if (FLAGS_dump_replays == "train" && state.testing) {
          return "";
        }
        std::string folder = FLAGS_results + "/replays-"
            + (state.testing ? "eval" : "train")
            + "/upd" + std::to_string(state.numUpdates.load());
        fsutils::mkdir(folder);
        return folder
            + "/rank" + std::to_string(cpid::distributed::globalContext()->rank)
            + "_thread" + std::to_string(threadId)
            + "_step" + std::to_string(nsteps)
            + ".rep";
      };
      std::shared_ptr<BasePlayer> p1, p2;
      while (!state.finish) {
        provider->cleanScenario();
        replayFile = computeReplayPath();
        provider->setReplay(replayFile);
        auto setup = respawn();
        p1 = setup.first;
        p2 = setup.second;
        microModule = p1->findModule<MicroModule>();
        nsteps = 0;

        // Quit only if:
        //  - we're done
        //  - game isn't active anymore, trainer says we should stop
        while (!provider->isFinished(nsteps, false /*checkAttack*/)) {
          if (state.finish || (microModule->started_ &&
                               !trainer->isActive(microModule->gameUID_))) {
            break;
          }
          p1->step();
          p2->step();
          nsteps++;
          state.throughputCounter++;
        }
        if (!provider->isFinished(nsteps, false /*checkAttack*/)) {
          // Never do anything with aborted episodes
          microModule->aborted_ = true;
        } else if (p1 && p2) { // If not first episode
          float aCount, eCount, aHp, eHp;
          std::tie(aCount, eCount, aHp, eHp) = getUnitCountsHealth(p1->state());
          // Note that this is called BEFORE microModule's onGameEnd() happens
          auto frame = microModule->currentFrame_;
          auto reward = microModule->frameReward_;

          if (!state.testing) {
            // Do stats
            state.numTrainEpisodes++;
            state.metrics->pushEvents(
                "episodeStats",
                {float(state.numUpdates.load()),
                 float(frame),
                 float(state.numTrainEpisodes.load()),
                 float(reward),
                 aCount,
                 microModule->firstAllyCount_,
                 aHp,
                 microModule->firstAllyHp_,
                 eCount,
                 microModule->firstEnemyCount_,
                 eHp,
                 microModule->firstEnemyHp_});
            state.metrics->incCounter("episodes");

            std::lock_guard<std::mutex> lock(state.statMutex);
            if (state.avgReward == 0) {
              state.avgReward = reward;
              state.avgSteps = frame;
            } else {
              state.avgReward = state.avgReward * 0.99 + reward * 0.01;
              state.avgSteps = state.avgSteps * 0.99 + frame * 0.01;
            }
          } else {
            state.metrics->incCounter("testEpisodes");
            state.metrics->pushEvents(
                "testEpisodeStats",
                {float(state.numUpdates.load()),
                 float(frame),
                 float(state.numTrainEpisodes.load()),
                 float(reward),
                 aCount,
                 microModule->firstAllyCount_,
                 aHp,
                 microModule->firstAllyHp_,
                 eCount,
                 microModule->firstEnemyCount_,
                 eHp,
                 microModule->firstEnemyHp_});
            std::lock_guard<std::mutex> lock(state.statMutex);
            state.avgTestSteps += frame;
            state.avgTestReward += reward;
            state.testWins.push_back(eCount == 0);
            state.testKills.push_back(
                1 - eCount / microModule->firstEnemyCount_);
            state.testDamage.push_back(1 - eHp / microModule->firstEnemyHp_);
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

  VLOG(0) << fmt::format("Scenario: {}", FLAGS_scenario);
  VLOG(0) << fmt::format("Model: {}", FLAGS_model);
  VLOG(0) << fmt::format("Resume: {}", FLAGS_resume);
  VLOG(0) << fmt::format("Evaluate: {}", FLAGS_evaluate);

  cherrypi::MicroFixedScenario::setMapPathPrefix(FLAGS_map_path_prefix);
  std::string resultsDir = FLAGS_results;
  std::string resultsJSON = fmt::format(
      "{}/metrics-rank-{}.json", resultsDir, dist::globalContext()->rank);
  std::string resultsCheckpoint = resultsDir + "/train_micro.bin";

  VLOG(0) << "resultsJSON: " << resultsJSON;
  VLOG(0) << "resultsCheckpoint: " << resultsCheckpoint;

  if (dist::globalContext()->rank == 0) {
    cherrypi::fsutils::mkdir(resultsDir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  }

  // Set up the trainer / model
  auto training = std::make_shared<TrainingSetup>();
  training->setCheckpointLocation(resultsCheckpoint);

  if (FLAGS_resume) {
    if (!cherrypi::fsutils::exists(resultsCheckpoint)) {
      VLOG(0) << "Failed to find existing model at " << resultsCheckpoint;
    } else if (!cherrypi::fsutils::exists(resultsJSON)) {
      VLOG(0) << "Failed to find metrics at " << resultsJSON;
    } else {
      VLOG(0) << "Found existing model! Loading it from " << resultsCheckpoint;
      training->loadModel(resultsCheckpoint);
      VLOG(0) << "Found existing metrics! Loading them from " << resultsJSON;
      state.metrics->loadJson(resultsJSON);
    }
  }

  state.training = training;
  {
    auto nParams = 0;
    for (auto& p : training->model->parameters()) {
      nParams += p.numel();
    }
    VLOG(0) << fmt::format("Model has {} total parameters", nParams);
  }

  dist::broadcast(training->model);

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
    for (auto& t : threads) {
      t.join();
    }
    threads.clear();
    workingTrainer->reset();
  };
  // sets up the evaluation and cleans it aftewards
  auto evaluate = [&]() {
    state.testing = true;
    auto model = training->trainer->model();
    model->eval();
    auto evaluator = training->trainer->makeEvaluator(
        FLAGS_num_test_episodes,
        std::move(std::make_unique<cpid::BaseSampler>()));
    startWorkers(evaluator);
    while (!evaluator->update()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    stopWorkers(evaluator);
    model->train();
    state.testing = false;
  };

  startWorkers(training->trainer);
  state.startTime = cherrypi::hires_clock::now();
  while (true) {
    if (training->trainer->update()) {
      state.numUpdates++;
      auto nEpisodes = state.numTrainEpisodes.load();
      auto framesSoFar = state.throughputCounter.load();
      using ms = std::chrono::duration<double, std::milli>;
      ms duration = cherrypi::hires_clock::now() - state.startTime;

      VLOG_MASTER(2) << common::WeightSummary(*training->model).toString();

      fmt::print(
          "rank {}\t"
          "episode {}\t"
          "avg_length {:.1f}\t"
          "avg_pop_reward {:.3f}\t"
          "forward/sec {:.3f}\t"
          "\n",
          dist::globalContext()->rank,
          nEpisodes,
          state.avgSteps,
          state.avgReward,
          1000. * framesSoFar / duration.count() / FLAGS_frame_skip);

      if (training->trainer->checkpoint()) {
        state.metrics->dumpJson(resultsJSON);
      }

      if ((state.numUpdates + 1) % FLAGS_test_freq == 0) {
        stopWorkers(training->trainer);
        evaluate();
        startWorkers(training->trainer);
        std::lock_guard<std::mutex> lock(state.statMutex);
        auto testWinTensor =
            at::CPU(at::kFloat)
                .tensorFromBlob(
                    state.testWins.data(), {(long)state.testWins.size()});
        auto testKillTensor =
            at::CPU(at::kFloat)
                .tensorFromBlob(
                    state.testKills.data(), {(long)state.testKills.size()});
        auto testDamageTensor =
            at::CPU(at::kFloat)
                .tensorFromBlob(
                    state.testDamage.data(), {(long)state.testDamage.size()});
        fmt::print(
            "TEST: "
            "rank {}\t"
            "update {}\t"
            "length {:.3f}\t"
            "reward {:.3f}\t"
            "wins {:.3f}|{:.3f}\t"
            "kills {:.3f}|{:.3f}\t"
            "damage {:.3f}|{:.3f}\t"
            "\n",
            dist::globalContext()->rank,
            state.numUpdates.load(),
            state.avgTestSteps / FLAGS_num_test_episodes,
            state.avgTestReward / FLAGS_num_test_episodes,

            testWinTensor.mean().item<float>(),
            testWinTensor.std().item<float>(),
            testKillTensor.mean().item<float>(),
            testKillTensor.std().item<float>(),
            testDamageTensor.mean().item<float>(),
            testDamageTensor.std().item<float>());
        state.metrics->dumpJson(resultsJSON);
        state.avgTestSteps = 0;
        state.avgTestReward = 0;
        state.testWins.clear();
        state.testKills.clear();
        state.testDamage.clear();
      }

      dist::allreduce(&nEpisodes, 1);
      if ((uint64_t)nEpisodes >= FLAGS_max_episodes) {
        break;
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  stopWorkers(training->trainer);
  evaluate();

  VLOG(0) << "Done!" << std::endl;
  return EXIT_SUCCESS;
}

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  FLAGS_lr = 1e-2;
  FLAGS_optim = "adam";
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (FLAGS_list_scenarios) {
    auto scenarioNames = cherrypi::MicroFixedScenario::listScenarios();
    for (auto& scenarioName : scenarioNames) {
      std::cout << scenarioName << std::endl;
    }
    return EXIT_SUCCESS;
  }
  return run(argc, argv);
}
