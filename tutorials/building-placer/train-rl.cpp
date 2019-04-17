/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "bpgtrainer.h"
#include "rlbuildingplacer.h"
#include "scenarios.h"

#include "gameutils/openbwprocess.h"
#include "player.h"
#include "upcstorage.h"
#include "utils.h"

#include "models/bandit.h"
#include "models/buildingplacer.h"

#include <common/autograd/utils.h>
#include <common/fsutils.h>
#include <common/rand.h>
#include <cpid/checkpointer.h>
#include <cpid/distributed.h>
#include <cpid/evaluator.h>
#include <cpid/optimizers.h>

#include <gflags/gflags.h>
#include <visdom/visdom.h>

using namespace cherrypi;
using namespace cpid;
namespace dist = cpid::distributed;
namespace fsutils = common::fsutils;
auto const vopts = &visdom::makeOpts;

// Training options
DEFINE_string(
    scenario,
    "sunkenplacement",
    "Scenario mode (sunkenplacement|vsrules)");
DEFINE_int32(
    seed,
    -1,
    "Random seed. Use default seed based on current time if < 0");
DEFINE_int32(
    num_game_threads,
    -1,
    "How many threads to use per worker (each playing a game); estimate using "
    "number of cores on system if < 0");
DEFINE_int32(batch_size, 64, "Batch size per worker");
DEFINE_double(eta, 2, "Entropy regularization factor");
DEFINE_string(maps, "maps", "Restrict to this map or maps in this directory");
DEFINE_bool(gpu, common::gpuAvailable(), "Train on GPU");
DEFINE_int32(
    plot_every,
    200,
    "Visualize outputs every n updates (<= 0 to disable)");
DEFINE_int32(
    checkpoint_every,
    -1,
    "Checkpoint model every n updates (<= 0 to disable)"
    "value");
DEFINE_int32(
    evaluate_every,
    100,
    "Run evaluation every n updates (<= 0 to disable)");
DEFINE_int64(max_updates, 10000, "Stop training after this many updates");
DEFINE_int64(
    max_games,
    std::numeric_limits<int64_t>::max(),
    "Stop training after this many games played");
DEFINE_int64(num_eval_games, 500, "Run this many evaluation games");
DEFINE_string(checkpoint, "checkpoint", "Checkpoint locationt");
DEFINE_string(initial_model, "", "Start training from this model");
DEFINE_string(evaluate, "", "Run in evaluation mode (rules|argmax/max)");
DEFINE_bool(save_eval_replays, false, "Save replays in evaluation mode");

// Visualization
DEFINE_string(visdom_server, "localhost", "Visdom server address");
DEFINE_int32(visdom_port, 8097, "Visdom server port");
DEFINE_string(
    visdom_env,
    "",
    "Visdom environment (empty string disables visualization)");
DEFINE_bool(gui, false, "Show BroodWar UI for first thread on first worker");

// Flags defined in other places
DECLARE_string(bandit);
DECLARE_bool(game_history);

namespace {

std::atomic<int> gNumGamesTotal(0);

// Output directory for replays and metrics
std::string gResultsDir;

// This is used for model output visualization
std::mutex gLatestGameMutex;
std::vector<std::shared_ptr<RLBPUpcData>> gLatestGameData;

/// Run a single game
void runGame(
    std::shared_ptr<Trainer> trainer,
    std::pair<std::shared_ptr<BasePlayer>, std::shared_ptr<BasePlayer>> players,
    int maxFrames) {
  std::shared_ptr<BasePlayer> player1, player2;
  std::tie(player1, player2) = players;

  // Run actual game
  while (!trainer->isDone()) {
    if (player1->state()->gameEnded() && player2->state()->gameEnded()) {
      break;
    }
    if (player1->state()->currentFrame() > maxFrames ||
        player2->state()->currentFrame() > maxFrames) {
      // Ignore games that took too long
      trainer->metricsContext()->incCounter("games_played");
      trainer->metricsContext()->incCounter("timeout");
      return;
    }
    player1->step();
    player2->step();
  }

  trainer->metricsContext()->incCounter("games_played");
  trainer->metricsContext()->pushEvent(
      "game_length", player1->state()->currentFrame());
  if (player1->state()->won()) {
    trainer->metricsContext()->incCounter("wins_p1");
  } else if (player2->state()->won()) {
    trainer->metricsContext()->incCounter("wins_p2");
  }

  // We'll collect samples for visualization in the first worker
  std::vector<std::shared_ptr<RLBPUpcData>> gameData;
  auto bprlModule = player1->findModule<RLBuildingPlacerModule>();
  if (bprlModule == nullptr) {
    return;
  }
  auto storage = player1->state()->board()->upcStorage();
  for (auto const* post : storage->upcPostsFrom(bprlModule.get())) {
    auto data = std::static_pointer_cast<RLBPUpcData>(post->data);
    // Ignore samples that ended up in cancelled tasks
    if (data == nullptr || !data->valid) {
      continue;
    }
    // Ignore samples that just consisted of a single valid action
    if (data->sample.features.validLocations.sum().item<float>() <=
        1.0f + kfEpsilon) {
      continue;
    }
    gameData.push_back(data);
  }
  std::lock_guard<std::mutex> episodeLock(gLatestGameMutex);
  std::swap(gLatestGameData, gameData);
}

void runGameThread(std::shared_ptr<Trainer> trainer, int num) {
  dist::setGPUToLocalRank();

  auto provider = makeBPRLScenarioProvider(
      FLAGS_scenario,
      FLAGS_maps,
      (FLAGS_gui && num == 0 && dist::globalContext()->rank == 0));
  GameUID gameId;
  auto setupFn = [&](BasePlayer* player) {
    // Find RL building placer module
    auto bprlModule = player->findModule<RLBuildingPlacerModule>();
    if (bprlModule) {
      if (FLAGS_evaluate != "rules") {
        bprlModule->setTrainer(trainer);
      }
    }
  };

  while (!trainer->isDone()) {
    try {
      if (FLAGS_save_eval_replays && trainer->is<Evaluator>()) {
        fsutils::mkdir(fmt::format("{}/replays", gResultsDir));
        provider->setReplayPath(
            fmt::format("{}/replays/{}.rep", gResultsDir, gameId));
      }
      auto players = provider->startNewScenario(setupFn, setupFn);

      // This might have taken some time, so check stop condition again
      if (trainer->isDone()) {
        break;
      }

      runGame(trainer, players, provider->maxFrames());
      gNumGamesTotal++;
    } catch (std::exception const& e) {
      LOG(WARNING) << gameId << " exception: " << e.what();
    }
  }
}

void runEvaluation(
    std::shared_ptr<Trainer> trainer,
    int numGames,
    std::shared_ptr<MetricsContext> metrics) {
  int gamesPerWorker = numGames / dist::globalContext()->size;
  int remainder = numGames % dist::globalContext()->size;
  if (dist::globalContext()->rank < remainder) {
    gamesPerWorker++;
  }

  trainer->model()->eval();
  auto evaluator = trainer->makeEvaluator(
      gamesPerWorker, std::make_unique<DiscreteMaxSampler>("output"));
  evaluator->setMetricsContext(metrics);
  metrics->setCounter("timeout", 0);
  metrics->setCounter("wins_p1", 0);
  metrics->setCounter("wins_p2", 0);

  // Launch environments. The main thread just waits until everything is done
  std::vector<std::thread> threads;
  for (auto i = 0; i < FLAGS_num_game_threads; i++) {
    threads.emplace_back(runGameThread, evaluator, i);
  }

  while (!evaluator->update()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  evaluator->setDone();
  evaluator->reset();
  for (auto& thread : threads) {
    thread.join();
  }

  // Sync relevant metrics
  static float mvec[3];
  mvec[0] = metrics->getCounter("games_played");
  mvec[1] = metrics->getCounter("wins_p1");
  mvec[2] = metrics->getCounter("wins_p2");
  dist::allreduce(mvec, 3);
  metrics->setCounter("total_games_played", mvec[0]);
  metrics->setCounter("total_wins_p1", mvec[1]);
  metrics->setCounter("total_wins_p2", mvec[2]);

  trainer->model()->train();
}

void trainLoop(
    std::shared_ptr<Trainer> trainer,
    std::shared_ptr<visdom::Visdom> vs) {
  std::vector<std::thread> threads;
  auto startGameThreads = [&]() {
    trainer->setDone(false);
    for (auto i = 0; i < FLAGS_num_game_threads; i++) {
      threads.emplace_back(runGameThread, trainer, i);
    }
  };
  auto stopGameThreads = [&]() {
    trainer->setDone(true);
    trainer->reset();
    for (auto& thread : threads) {
      thread.join();
    }
    threads.clear();
  };

  // Take care to only put variables into dist:: primitives that are static or
  // don't change throughout the lifetime of the the process.
  static int totalGames = 0;
  static float reward;
  static float policyLoss;
  static float entropyLoss;

  int numModelUpdates = 0;
  auto evaluate = [&]() -> float {
    gResultsDir = fmt::format("eval-{:05d}", numModelUpdates);
    fsutils::mkdir(gResultsDir);

    auto evalMetrics = std::make_shared<MetricsContext>();
    runEvaluation(trainer, FLAGS_num_eval_games, evalMetrics);
    evalMetrics->dumpJson(fmt::format(
        "{}/{}-metrics.json", gResultsDir, dist::globalContext()->rank));
    auto total = evalMetrics->getCounter("total_games_played");
    auto winsP1 = evalMetrics->getCounter("total_wins_p1");
    return float(winsP1) / total;
  };

  auto updatePlot = [&](std::string const& window,
                        std::string const& title,
                        std::string const& ytitle,
                        float value) -> std::string {
    return vs->line(
        torch::tensor(value),
        torch::tensor(float(numModelUpdates)),
        window,
        vopts({{"title", title}, {"xtitle", "Updates"}, {"ytitle", ytitle}}),
        window.empty() ? visdom::UpdateMethod::None
                       : visdom::UpdateMethod::Append);
  };

  startGameThreads();

  fsutils::mkdir("checkpoints");
  auto checkpointer =
      cpid::Checkpointer(trainer).epochLength(5).checkpointPath("checkpoints");
  auto metrics = trainer->metricsContext();
  std::map<std::string, std::string> visdomWindows;
  int updatesSinceLastVisualization = 0;
  while (true) {
    if (numModelUpdates >= FLAGS_max_updates || totalGames >= FLAGS_max_games) {
      break;
    }

    auto updated = trainer->update();
    if (!updated) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }

    numModelUpdates++;
    checkpointer.updateDone(numModelUpdates);
    totalGames = gNumGamesTotal.load();
    dist::allreduce(&totalGames, 1);

    // Logging and plotting
    VLOG_ALL(1) << "Trainer update done in "
                << metrics->getLastInterval("trainer:model_update")
                << "ms with "
                << metrics->getLastEventValue(
                       "trainer:num_new_samples_per_update")
                << " samples from new games";
    metrics->dumpJson(
        fmt::format("{}-metrics.json", dist::globalContext()->rank));

    reward = metrics->getLastEventValue("trainer:mean_batch_reward");
    policyLoss = metrics->getLastEventValue("trainer:batch_policy_loss");
    entropyLoss = metrics->getLastEventValue("trainer:batch_entropy_loss");
    VLOG_ALL(1) << fmt::format(
        "Update {} with avg reward {} policy loss {} entropy loss {}",
        numModelUpdates,
        reward,
        policyLoss,
        entropyLoss);

    // Log and plot main metrics after synchronizing among workers
    dist::allreduce(&reward, 1);
    dist::allreduce(&policyLoss, 1);
    dist::allreduce(&entropyLoss, 1);
    reward /= dist::globalContext()->size;
    policyLoss /= dist::globalContext()->size;
    entropyLoss /= dist::globalContext()->size;
    VLOG_MASTER(0) << fmt::format(
        "Average perf at update {} ({} played): reward {} policy loss {} "
        "entropy loss {}",
        numModelUpdates,
        totalGames,
        reward,
        policyLoss,
        entropyLoss);
    if (vs != nullptr && dist::globalContext()->rank == 0) {
      visdomWindows["reward"] =
          updatePlot(visdomWindows["reward"], "Reward", "Reward", reward);
      visdomWindows["loss_p"] = updatePlot(
          visdomWindows["loss_p"], "Policy Loss", "Loss", policyLoss);
      visdomWindows["loss_e"] = updatePlot(
          visdomWindows["loss_e"], "Entropy Loss", "Loss", entropyLoss);
    }

    // Save checkpoint if requested
    if (FLAGS_checkpoint_every > 0 && dist::globalContext()->rank == 0) {
      if (numModelUpdates % FLAGS_checkpoint_every == 0) {
        std::string checkpointTempPath =
            std::string("checkpoints/checkpoint-") +
            std::to_string(numModelUpdates) + ".bin";
        cpid::Checkpointer::checkpointTrainer(trainer, checkpointTempPath);
      }
    }
    // Plot latest game if requested
    updatesSinceLastVisualization++;
    std::unique_lock<std::mutex> gameDataLock(gLatestGameMutex);
    if (vs != nullptr && dist::globalContext()->rank == 0 &&
        FLAGS_plot_every > 0 &&
        updatesSinceLastVisualization >= FLAGS_plot_every &&
        !gLatestGameData.empty()) {
      std::unordered_set<int> plottedTypes; // One output plot per building type
      for (auto const& data : gLatestGameData) {
        if (plottedTypes.find(data->sample.features.type) !=
            plottedTypes.end()) {
          continue;
        }
        auto title = "Sample@" + std::to_string(numModelUpdates) + " " +
            getUnitBuildType(data->sample.features.type)->name + " ";

        if (plottedTypes.empty()) {
          // Plot state once per game
          vs->heatmap(
              selectFeatures(
                  data->sample.features.map, {PlainFeatureType::GroundHeight})
                  .tensor.sum(0)
                  .toType(at::kFloat),
              vopts({{"title", title + " groundheight"}}));
          vs->heatmap(
              selectFeatures(
                  data->sample.features.map, {PlainFeatureType::Buildability})
                  .tensor.sum(0)
                  .gt(0)
                  .toType(at::kFloat),
              vopts({{"title", title + " buildability"}}));
          vs->heatmap(
              selectFeatures(
                  data->sample.features.map, {PlainFeatureType::UserFeature1})
                  .tensor.sum(0)
                  .gt(0)
                  .toType(at::kFloat),
              vopts({{"title", title + " upc"}}));
          vs->heatmap(
              data->sample.features.validLocations,
              vopts({{"title", title + " validMask"}}));
          vs->heatmap(
              subsampleFeature(
                  data->sample.unitFeaturizer.toSpatialFeature(
                      data->sample.features.units),
                  SubsampleMethod::Sum,
                  data->sample.features.map.scale)
                  .tensor.sum(0)
                  .gt(0)
                  .toType(at::kFloat),
              vopts({{"title", title + " units"}}));
        }

        // Model output is 1D but we want to see it in 2D
        auto dim = int(sqrt(data->output["output"][0].size(0)));
        auto out = data->output["output"][0].view({dim, dim});
        vs->heatmap(out, vopts({{"title", title + "output"}}));

        plottedTypes.insert(data->sample.features.type);
      }

      gLatestGameData.clear();
      updatesSinceLastVisualization = 0;
    }
    gameDataLock.unlock();

    // Run evaluation if requested
    if (FLAGS_evaluate_every > 0 &&
        numModelUpdates % FLAGS_evaluate_every == 0) {
      stopGameThreads();
      VLOG_MASTER(0) << fmt::format(
          "Starting evaluation after {} updates", numModelUpdates);
      float winRate = evaluate();
      metrics->pushEvent("eval_win_rate", winRate);
      VLOG_MASTER(0) << fmt::format(
          "Evaluate after {} updates ({} played): win rate {:.1f}%",
          numModelUpdates,
          totalGames,
          100.0f * winRate);

      if (dist::globalContext()->rank == 0) {
        if (vs != nullptr) {
          visdomWindows["winrate"] = updatePlot(
              visdomWindows["winrate"], "Evaluation Win Rate", "WR", winRate);
        }
      }
      startGameThreads();

      // Don't use evaluation game data for subsequent training plots
      std::lock_guard<std::mutex> lock(gLatestGameMutex);
      gLatestGameData.clear();
    }
  }

  // Write out final checkpoint
  if (dist::globalContext()->rank == 0) {
    cpid::Checkpointer::checkpointTrainer(trainer);
  }
  stopGameThreads();
}

} // namespace

int main(int argc, char** argv) {
  // Default values for a few common flags
  FLAGS_bandit = kBanditNone; // We chose opening builds manually
  FLAGS_game_history = false; // No need to write game history to disk
  FLAGS_lr = 1e-5;
  FLAGS_optim = "adam";
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  cherrypi::initLogging(argv[0], "", true);

  cherrypi::ForkServer::startForkServer();
  cherrypi::init();
  dist::init();

  if (FLAGS_seed < 0) {
    // Set the seed flag explicitly so we can log it as part of the flags
    FLAGS_seed = common::Rand::defaultRandomSeed();
    common::Rand::setSeed(FLAGS_seed + dist::globalContext()->rank);
  } else {
    common::Rand::setSeed(FLAGS_seed + dist::globalContext()->rank);
  }

  if (FLAGS_num_game_threads < 0) {
    // We require two BWAPILauncher instances per game, plus the game thread
    // itself performing Player::step() for both players sequentially. We assume
    // that we'll need a full core for the main thread, and half a core for the
    // two game instances.
    auto numCores = std::thread::hardware_concurrency();
    FLAGS_num_game_threads = numCores / 1.5f;
  }

  VLOG(1) << fmt::format(
      "Starting distributed process {}/{}",
      dist::globalContext()->rank,
      dist::globalContext()->size);
  dist::setGPUToLocalRank();

  std::shared_ptr<visdom::Visdom> vs;
  if (dist::globalContext()->rank == 0) {
    VLOG(0) << fmt::format(
        "Training run started with {} workers", dist::globalContext()->size);

    if (!FLAGS_visdom_env.empty()) {
      visdom::ConnectionParams vparams;
      vparams.server = FLAGS_visdom_server;
      vparams.port = FLAGS_visdom_port;
      char const* slurmJobId = std::getenv("SLURM_JOBID");
      std::string visdomEnv(FLAGS_visdom_env);
      if (slurmJobId != NULL) {
        visdomEnv = visdomEnv + "-" + std::string(slurmJobId);
      }
      vs = std::make_shared<visdom::Visdom>(vparams, visdomEnv);

      std::ostringstream oss;
      oss << "<h4>RL building placer training</h4>";
      oss << "<p>Training started " << utils::curTimeString() << "</p>";
      oss << "<hl><p>";
      for (auto const& it : utils::cmerge(
               utils::gflagsValues(__FILE__), cpid::optimizerFlags())) {
        oss << "<b>" << it.first << "</b>: " << it.second << "<br>";
      }
      oss << "</p>";
      vs->text(oss.str());
    }

    VLOG(0) << std::string(42, '=');
    for (auto const& it :
         utils::cmerge(utils::gflagsValues(__FILE__), cpid::optimizerFlags())) {
      VLOG(0) << fmt::format("{}: {}", it.first, it.second);
    }
    VLOG(0) << std::string(42, '=');
  }

  auto model =
      BuildingPlacerModel().flatten(true).masked(true).logprobs(false).make();
  if (dist::globalContext()->rank == 0 && !FLAGS_initial_model.empty()) {
    VLOG(0) << fmt::format(
        "Loading initial model from {}", FLAGS_initial_model);
    ag::load(FLAGS_initial_model, model);
  }

  // Synchronize model parameters among all workers
  dist::broadcast(model);
  if (FLAGS_gpu) {
    model->to(torch::kCUDA);
  }

  auto optim = cpid::selectOptimizer(model);
  auto metrics = std::make_shared<MetricsContext>();
  auto trainer = std::make_shared<BPGTrainer>(
      model,
      optim,
      std::make_unique<MultinomialSampler>("output"),
      FLAGS_batch_size,
      std::max(FLAGS_batch_size * 2, FLAGS_num_game_threads * 2),
      0.0, // gamma
      FLAGS_eta);
  trainer->setMetricsContext(metrics);

  if (fsutils::exists(FLAGS_checkpoint)) {
    VLOG(0) << fmt::format(
        "Found existing checkpoint {}; loading it", FLAGS_checkpoint);
    ag::load(FLAGS_checkpoint, trainer);
    dist::broadcast(model);
  }

  if (!FLAGS_evaluate.empty()) {
    if (FLAGS_evaluate != "argmax" && FLAGS_evaluate != "max" &&
        FLAGS_evaluate != "rules") {
      LOG(FATAL) << "Unknown evaluation mode: " << FLAGS_evaluate;
    }

    gResultsDir = ".";
    runEvaluation(trainer, FLAGS_num_eval_games, metrics);
    if (dist::globalContext()->rank == 0) {
      auto total = metrics->getCounter("total_games_played");
      auto winsP1 = metrics->getCounter("total_wins_p1");
      auto winsP2 = metrics->getCounter("total_wins_p2");
      VLOG(0) << fmt::format(
          "Done! Win rates for {} games: {:.1f}% {:.1f}%",
          total,
          100.0f * winsP1 / total,
          100.0f * winsP2 / total);
    }
    metrics->dumpJson(
        std::to_string(dist::globalContext()->rank) + "-metrics.json");
  } else {
    trainLoop(trainer, vs);
  }

  return EXIT_SUCCESS;
}
