/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "common.h"
#include "loops.h"
#include "modules.h"

#include "models/bos/models.h"
#include "models/bos/runner.h"
#include "models/bos/sample.h"

#include "forkserver.h"
#include "models/bandit.h"
#include "gameutils/openbwprocess.h"
#include "player.h"
#include "gameutils/selfplayscenario.h"
#include "upcstorage.h"
#include "zstdstream.h"

#include <common/datareader.h>
#include <common/fsutils.h>
#include <common/rand.h>
#include <common/serialization.h>
#include <cpid/batcher.h>
#include <cpid/optimizers.h>
#include <cpid/sampler.h>
#include <cpid/checkpointer.h>

#include <fmt/format.h>
#include <fmt/ostream.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <prettyprint/prettyprint.hpp>
#include <visdom/visdom.h>

DEFINE_string(
    mode,
    "polit",
    "Operation mode (online|offline|evaluate|polit|listbuilds))");
DEFINE_int32(
    num_offline_iterations,
    10,
    "Train for this many episodes in offline mode");

// Model options -- all others are defined in models.cpp
DEFINE_double(dropout, 0.0, "Dropout");

// Environment options
DEFINE_int32(
    seed,
    -1,
    "Random seed. Use default seed based on current time if < 0");
DEFINE_int32(
    num_game_threads,
    -1,
    "How many threads to use per worker (each playing a game); estimate using "
    "number of cores on system if < 0");
DEFINE_string(
    maps,
    "/workspace/bw_bots/maps/aiide",
    "Restrict to this map or maps in this directory");
DEFINE_string(
    opponents,
    "374_P_AIUR"
    ":374_P_MegaBot"
    ":374_P_Skynet"
    ":374_P_Xelnaga"
    ":374_P_Ximp"
    ":374_T_ICEBot"
    ":374_T_LetaBot"
    ":374_T_LetaBot-AIIDE2017"
    ":374_T_LetaBot-BBS"
    ":374_T_LetaBot-SCVMarineRush"
    ":374_T_LetaBot-SCVRush"
    ":374_T_Matej_Istenik"
    ":374_Z_Overkill"
    ":412_P_Bereaver"
    ":412_P_Juno"
    ":412_P_Locutus"
    ":412_P_McRave"
    ":412_P_McRave-4Gate"
    ":412_P_McRave-GatewayFE"
    ":412_P_NiteKatP"
    ":412_P_Randomhammer"
    ":412_P_UAlbertaBot"
    ":412_P_UITTest"
    ":412_P_WuliBot"
    //":412_R_Randomhammer"
    //":412_R_UAlbertaBot"
    ":412_T_Iron"
    ":412_T_Iron-AIIDE2017"
    ":412_T_NiteKatT"
    ":412_T_Randomhammer"
    ":412_T_Stone"
    ":412_T_UAlbertaBot"
    ":412_Z_Arrakhammer"
    ":412_Z_BlackCrow"
    ":412_Z_KillAll"
    ":412_Z_Killerbot"
    ":412_Z_Microwave"
    ":412_Z_NeoEdmundZerg"
    ":412_Z_NLPRBot_CPAC"
    ":412_Z_Overkill-AIIDE2016"
    ":412_Z_Overkill-AIIDE2017"
    ":412_Z_Steamhammer"
    ":412_Z_UAlbertaBot"
    ":412_Z_Zia_bot"
    ":420_P_BananaBrain"
    ":420_P_Prism_Cactus"
    ":420_P_SkyFORKNet"
    ":420_P_Tscmoo"
    //":420_R_Tscmoo"
    //":420_R_UAlbertaBot-AIIDE2017"
    ":420_T_HannesBredberg"
    ":420_T_HaoPan"
    ":420_T_Toothpick_Cactus"
    ":420_T_Tscmoo"
    ":420_T_WillyT"
    ":420_Z_AILien"
    ":420_Z_CUNYBot"
    ":420_Z_Pineapple_Cactus"
    ":420_Z_Proxy"
    ":420_Z_Tscmoo"
    ":420_Z_ZZZKBot",
    "Play against these opponents");
DEFINE_string(sample_path, "samples", "Save samples here");
DEFINE_string(playoutput, "playoutput", "Output folder for play script");

// Training options
DEFINE_string(checkpoint, "checkpoint", "Checkpoint location");
DEFINE_string(initial_model, "", "Start from this model");
DEFINE_bool(gpu, common::gpuAvailable(), "Train on GPU");
DEFINE_bool(
    save_samples,
    true,
    "Save samples in policy iteration and online training");
DEFINE_int32(valid_every, -1, "Validate every n processed episodes");
DEFINE_int32(batch_size, 64, "Batch this many examples");
DEFINE_int32(macro_batch_size, -1, "Run update loop on this many examples");
DEFINE_int32(
    macro_batch_size_validation,
    64,
    "Run update loop on this many examples");
DEFINE_int32(bptt, 64, "Temporal batch size for recurrent models");
DEFINE_int32(
    num_workers_per_trainer,
    24,
    "How many workers should feed one trainer");
DEFINE_bool(
    heterogeneous,
    false,
    "Training (servers) only GPU, rollouts only on CPU machines");
DEFINE_bool(decisions_only, true, "Train on decision points only");
DEFINE_bool(
    initial_nondec_samples,
    false,
    "Add non-decision samples before first decision");

DEFINE_uint64(skip_frames, 5 * 24, "Temporal stride");
DEFINE_double(
    num_bo_switches,
    1,
    "Switch build orders this many times per game (on avg)");
DEFINE_double(
    min_commitment_time,
    5.0,
    "For e-greedy exploration, stick to random switches for at least this many "
    "minutes");
DEFINE_double(
    max_commitment_time,
    13.0,
    "For e-greedy exploration, stick to random switches for at most this many "
    "minutes");

// Visualization
DEFINE_string(visdom_server, "localhost", "Visdom server address");
DEFINE_int32(visdom_port, 8097, "Visdom server port");
DEFINE_string(
    visdom_env,
    "",
    "Visdom environment (empty string disables visualization)");

// Flags defined in other places
DECLARE_string(build);
DECLARE_string(bandit);
DECLARE_string(bos_start);
DECLARE_double(bos_min_advantage);

using namespace cherrypi;
using namespace cpid;
namespace dist = cpid::distributed;
auto const vsopts = &visdom::makeOpts;
namespace fsutils = common::fsutils;

namespace {

class BosTrainer : public CentralTrainer {
 public:
  BosTrainer(
      bool isServer,
      ag::Container model,
      ag::Optimizer optim,
      std::shared_ptr<UpdateLoop> loop)
      : CentralTrainer(
            isServer,
            model,
            optim,
            std::make_unique<DiscreteMaxSampler>("vHeads"),
            nullptr /* no batcher */),
        loop_(loop) {
    loop_->setTrainer(this);
  }

  bool saveSamples = true;
  size_t numEpisodesReceived = 0;
  size_t numValidations = 0;

  UpdateLoop& loop() {
    return *loop_;
  }

  void validateOffline(
      common::DataReader<BosEpisodeData> dr,
      size_t numEpisodes /* for logging */) {
    // Ugh...
    auto prevBatchSize = loop_->batchSize;
    if (FLAGS_macro_batch_size > 0) {
      loop_->batchSize = FLAGS_macro_batch_size_validation;
    }

    // Feed data to the training loop directly instead of putting it into the
    // replayer first
    auto processEpisode = [&](auto& episode) {
      std::vector<BosSample> samples;
      samples.reserve(episode.frames.size());
      for (auto& frame : episode.frames) {
        samples.emplace_back(
            std::move(static_cast<BosReplayBufferFrame*>(frame.get())->sample));
      }
      if (!samples.empty()) {
        samples[0].staticData->gameId = episode.gameId;
      }
      (*loop_)(std::move(samples));
    };

    numValidations++;
    auto id = fmt::format("{:03d}/{:05d}", numValidations, numEpisodes / 1000);
    VLOG(0) << "Starting validation " << id;
    auto validMetrics = std::make_shared<MetricsContext>();
    setMetricsContext(validMetrics);
    loop_->eval();
    auto it = dr.iterator();
    while (it->hasNext()) {
      auto batch = it->next();
      for (auto const& epd : batch) {
        processEpisode(epd);
      }
    }
    loop_->flush();
    // XXX Wait until the model saw at least one batch before waiting for the
    // whole pipeline
    while (true) {
      try {
        validMetrics->getLastEvent("loss");
        break;
      } catch (std::exception const&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
    loop_->wait();

    VLOG(0) << "Validation " << id << " done";
    VLOG(0) << "V" << id << " " << validMetrics->getMeanEventValues();
    validMetrics->dumpJson(
        fmt::format(
            "{}_vmetrics_{:05d}.json",
            dist::globalContext()->rank,
            numEpisodes / 1000));

    loop_->batchSize = prevBatchSize;
  }

  void runOffline() {
    saveSamples = false;

    auto trainDr = common::DataReader<BosEpisodeData>(
        fsutils::readLinesPartition(
            FLAGS_sample_path + "/train.list",
            dist::globalContext()->rank,
            dist::globalContext()->size),
        16,
        4,
        FLAGS_sample_path);
    auto validDr = common::DataReader<BosEpisodeData>(
        fsutils::readLinesPartition(
            FLAGS_sample_path + "/valid.list",
            dist::globalContext()->rank,
            dist::globalContext()->size),
        16,
        4,
        FLAGS_sample_path);

    // Feed data to the training loop directly instead of putting it into the
    // replayer first
    auto processEpisode = [&](auto& episode) {
      std::vector<BosSample> samples;
      samples.reserve(episode.frames.size());
      for (auto& frame : episode.frames) {
        samples.emplace_back(
            std::move(static_cast<BosReplayBufferFrame*>(frame.get())->sample));
      }
      if (!samples.empty()) {
        samples[0].staticData->gameId = episode.gameId;
      }
      (*loop_)(std::move(samples));
    };

    auto ctx = metricsContext_;
    int64_t processed = 0;
    for (auto iter = 0; iter < FLAGS_num_offline_iterations; iter++) {
      // Train
      setMetricsContext(ctx);
      loop_->train();
      auto it = trainDr.iterator();
      while (it->hasNext()) {
        auto batch = it->next();
        for (auto const& epd : batch) {
          processEpisode(epd);

          processed++;
          if (FLAGS_valid_every > 0 && (processed % FLAGS_valid_every == 0)) {
            loop_->flush();
            loop_->wait();
            ag::save(
                fmt::format("model_p{:05d}.bin", processed / 1000), model_);
            validateOffline(validDr, processed);
            setMetricsContext(ctx);
            loop_->train();
          }
        }
      }
      loop_->flush();
      loop_->wait();
      ag::save(fmt::format("model_i{:02d}.bin", iter + 1), model_);

      // Validation
      if (FLAGS_valid_every <= 0) {
        validateOffline(validDr, processed);
      }

      VLOG_ALL(0) << "Offline training: finished iteration " << iter + 1;
      trainDr.shuffle();
    }
  }

  // Like validateOffline(), but also dump predictions to stdout
  void evaluateOffline() {
    saveSamples = false;

    auto validDr = common::DataReader<BosEpisodeData>(
        fsutils::readLinesPartition(
            FLAGS_sample_path + "/valid.list",
            dist::globalContext()->rank,
            dist::globalContext()->size),
        16,
        4,
        FLAGS_sample_path);

    loop_->dumpPredictions = true;
    validateOffline(validDr, 0);
    loop_->dumpPredictions = false;
  }

 protected:
  virtual void receivedFrames(
      GameUID const& gameId,
      EpisodeKey const& episodeKey) override {
    if (saveSamples) {
      // XXX This could be done directly in CentralTrainer when receiving a blob
      auto path = fmt::format(
          "{}/{:05d}/{}-{}.bin",
          FLAGS_sample_path,
          numEpisodesReceived / 1000,
          gameId,
          episodeKey);
      fsutils::mkdir(fsutils::dirname(path));
      common::zstd::ofstream os(path);
      cereal::BinaryOutputArchive ar(os);

      BosEpisodeData data;
      data.gameId = gameId;
      data.episodeKey = episodeKey;
      for (auto const& frame : replayer_.get(gameId, episodeKey)) {
        data.frames.push_back(frame);
      }
      ar(data);
    }
    numEpisodesReceived++;

    auto frames = cast<BosReplayBufferFrame>(replayer_.get(gameId, episodeKey));
    std::vector<BosSample> samples;
    for (auto const& frame : frames) {
      samples.push_back(std::move(frame->sample));
    }
    (*loop_)(std::move(samples));

    // We don't need this episode anymore
    replayer_.erase(gameId, episodeKey);
  }

  std::shared_ptr<UpdateLoop> loop_;
};

std::atomic<bool> gStopGameThreads(false);
std::vector<std::unique_ptr<PlayScriptScenario>> gScenarios;
std::mutex gScenariosMutex;

bool randomBuildOrderSwitch(
    GameUID const& gameId,
    std::shared_ptr<Trainer> trainer,
    State* state,
    float prob) {
  if (numBuilds(FLAGS_bos_targets) <= 1) {
    return false;
  }

  std::uniform_real_distribution<double> dist(0.0, 1.0);
  if (prob >= 0 && common::Rand::sample(dist) < prob) {
    auto build = selectRandomBuild(
        FLAGS_bos_targets,
        state->board()->get<std::string>(Blackboard::kEnemyNameKey));
    state->board()->post(Blackboard::kBuildOrderKey, build);
    VLOG(0) << gameId << " switch to build " << build << " at "
            << state->currentFrame() << " fames";
    return true;
  }
  return false;
}

void runGame(
    EpisodeHandle const& handle,
    std::shared_ptr<BasePlayer> player,
    int maxFrames,
    std::shared_ptr<Trainer> trainer) {
  auto* state = player->state();

  std::shared_ptr<BosStaticData> staticData = nullptr;
  FrameNum nextSampleFrame = 0;
  FrameNum nextSwitchableFrame = 0;
  bool canUseModelPrediction = false;
  auto modelRunner = FLAGS_mode == "online"
      ? nullptr
      : bos::makeModelRunner(trainer, FLAGS_bos_model_type);

  // For e-greedy exploration during policy iteration
  std::uniform_int_distribution<int> committmentDist(
      int(FLAGS_min_commitment_time * 24 * 60),
      int(FLAGS_max_commitment_time * 24 * 60));

  // Do one switch per X minutes
  auto switchFrequency = 8.0f;
  switch (common::Rand::rand() % 3) {
    case 0:
      switchFrequency = 10.0f;
      break;
    case 1:
      switchFrequency = 13.0f;
      break;
    case 2:
    default:
      break;
  }
  VLOG(0) << handle << " Random switch frequency: " << switchFrequency
          << " minutes";
  auto avgSamples = (24.0f * 60 * switchFrequency) / FLAGS_skip_frames;
  auto const switchProba = float(FLAGS_num_bo_switches) / avgSamples;

  bool timeout = false;
  while (true) {
    if (!trainer->isActive(handle)) {
      throw std::runtime_error(fmt::format("{} no longer active", handle));
    }
    if (gStopGameThreads.load()) {
      throw std::runtime_error(fmt::format("{} stop requested", handle));
    }
    if (state->gameEnded()) {
      int allEnemyUnitsCount = 0;
      for (Unit* u : state->unitsInfo().allUnitsEver()) {
        if (u->isEnemy && u->type != buildtypes::Zerg_Larva) {
          ++allEnemyUnitsCount;
        }
      }
      if (allEnemyUnitsCount <= 9) {
        VLOG(0) << handle << " opponent doesn't start.";
        return;
      }
      if (state->currentFrame() <= 24 * 180) {
        VLOG(0) << handle << " is too short, sth might be wrong.";
        return;
      }
      break;
    }
    if (state->currentFrame() > maxFrames) {
      player->leave();
      timeout = true;
    }

    player->step();

    // Start using model predicitions after observing a non-worker enemy unit
    // (or more than one enemy worker)
    if (FLAGS_bos_start == "firstenemy") {
      if (!canUseModelPrediction) {
        auto& enemies = state->unitsInfo().enemyUnits();
        // We saw more than one enemy unit
        canUseModelPrediction |= enemies.size() > 1;
        // We saw a non-worker unit
        canUseModelPrediction |=
            (enemies.size() > 0 && !enemies.front()->type->isWorker);
      }
    } else if (!canUseModelPrediction) {
      auto startTime = std::stof(FLAGS_bos_start) * 60;
      if (state->currentGameTime() < startTime) {
        for (Unit* u : state->unitsInfo().enemyUnits()) {
          // If they proxy or attack, start bos immediately
          if (!u->type->isWorker && !u->type->supplyProvided &&
              !u->type->isRefinery) {
            float baseDistance = kfInfty;
            for (Position pos :
                 state->areaInfo().candidateEnemyStartLocations()) {
              float d = state->areaInfo().walkPathLength(u->pos(), pos);
              if (d < baseDistance) {
                baseDistance = d;
              }
            }
            float myBaseDistance = state->areaInfo().walkPathLength(
                u->pos(), state->areaInfo().myStartLocation());
            if (myBaseDistance < baseDistance * 2.0f) {
              VLOG(0) << handle
                      << " proxy/rush dectected; starting BOS at frame "
                      << state->currentFrame();
              canUseModelPrediction = true;
              break;
            }
          }
        }
      } else {
        canUseModelPrediction = true;
      }
    }

    if (state->currentFrame() >= nextSampleFrame) {
      auto sample = BosSample(state, 32, 32, staticData);
      staticData = sample.staticData;
      staticData->switchProba = switchProba;
      VLOG(2) << handle << " extract sample at frame " << state->currentFrame();

      ag::Variant modelOutput;
      if (modelRunner) {
        modelOutput = modelRunner->forward(sample, handle);
      }

      nextSampleFrame += FLAGS_skip_frames;
      if (state->currentFrame() >= nextSwitchableFrame) {
        auto switched =
            randomBuildOrderSwitch(handle.gameID(), trainer, state, switchProba);
        if (FLAGS_mode == "polit") {
          if (switched) {
            nextSwitchableFrame += common::Rand::sample(committmentDist);
            VLOG(1) << handle << " sticking to random switch for "
                    << (nextSwitchableFrame - state->currentFrame()) / 24
                    << "s";
          } else {
            // No random switch -> use model prediction if possible
            if (modelOutput.isDict() && canUseModelPrediction &&
                modelOutput["advantage"].item<float>() >
                    FLAGS_bos_min_advantage) {
              auto buildFromModel = modelOutput.getDict()["build"].getString();
              VLOG(1) << handle << " switching to " << buildFromModel
                      << " according to model with advantage "
                      << modelOutput["advantage"].item<float>();
              state->board()->post(Blackboard::kBuildOrderKey, buildFromModel);
            }
            nextSwitchableFrame = nextSampleFrame;
          }
        }
        sample.switched = switched;
      }

      sample.nextBuildOrder = bos::addRacePrefix(
          state->board()->get<std::string>(Blackboard::kBuildOrderKey),
          bos::getOpponentRace(
              state->board()->get<std::string>(Blackboard::kEnemyNameKey)));
      sample.nextAbboStates = BosSample::simulateAbbo(
          state,
          bos::stripRacePrefix(sample.nextBuildOrder),
          {5 * 24, 15 * 24, 30 * 24});

      trainer->step(
          handle, std::make_shared<BosReplayBufferFrame>(std::move(sample)));
    }
  }

  trainer->metricsContext()->pushEvent("game_length", state->currentFrame());
  if (timeout) {
    // Ignore this game
    trainer->metricsContext()->incCounter("timeouts");
    VLOG(0) << handle << " timeout";
    return;
  }
  VLOG(0) << handle << (state->won() ? " won" : " lost") << " against "
          << player->state()->board()->get<std::string>(
                 Blackboard::kEnemyNameKey)
          << " after " << state->currentFrame() << " frames";
  if (staticData) {
    staticData->won = state->won();
  }

  // The final frame is just a dummy one
  trainer->step(handle, std::make_shared<BosReplayBufferFrame>(), true);
}

void runGameThread(std::shared_ptr<Trainer> trainer, int num) {
  dist::setGPUToLocalRank();
  auto constexpr kMaxGamesPerScenario = 25;

  uint32_t numGames = 0;
  int gamesWithCurrentScenario = -1;
  std::unique_ptr<PlayScriptScenario> scenario;
  while (!gStopGameThreads.load()) {
    auto handle = trainer->startEpisode();
    if (!handle) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    // Pick a random scanario if needed. Remove it from the list of scenarios so
    // that no other thread will use it
    if (scenario == nullptr || gamesWithCurrentScenario <= 0) {
      std::lock_guard<std::mutex> lock(gScenariosMutex);
      if (scenario) {
        // Put it pack
        gScenarios.push_back(nullptr);
        std::swap(scenario, gScenarios.back());
      }
      auto i = common::Rand::rand() % gScenarios.size();
      std::swap(gScenarios[i], gScenarios.back());
      std::swap(scenario, gScenarios.back());
      gScenarios.pop_back();
      gamesWithCurrentScenario = 1 + (common::Rand::rand() % 5);
    }

    try {
      // Obey maximum series length
      if (scenario == nullptr ||
          scenario->numGamesStarted() >= kMaxGamesPerScenario) {
        if (scenario != nullptr) {
          VLOG(0) << "Played " << scenario->numGamesStarted() << " against "
                  << scenario->enemyBot() << ", instantiating new scenario";
        }
        scenario =
            makeBosScenario(FLAGS_maps, FLAGS_opponents, FLAGS_playoutput);
      }
      gamesWithCurrentScenario--;

      auto player = std::make_shared<Player>(scenario->makeClient());
      player->setWarnIfSlow(false);
      player->setNonBlocking(false);
      player->setCheckConsistency(false);
      player->setCollectTimers(false);
      player->addModule(Module::make(kAutoTopModule));
      for (auto name : utils::stringSplit(kDefaultModules, ',')) {
        if (!name.empty()) {
          player->addModule(Module::make(name));
        }
      }
      player->addModule(Module::make(kAutoBottomModule));

      auto* state = player->state();
      state->board()->upcStorage()->setPersistent(false);
      // Set correct bandit directory
      state->board()->post(
          Blackboard::kBanditRootKey, scenario->path() + "/sc.0");

      // This might have taken some time, so check stop condition again
      if (gStopGameThreads.load()) {
        break;
      }
      VLOG(0) << handle << " starting against "
              << state->board()->get<std::string>(Blackboard::kEnemyNameKey)
              << " on " << state->tcstate()->map_name << ", #"
              << scenario->numGamesStarted() << " in series";

      player->init();
      runGame(handle, player, 86400, trainer);
    } catch (std::exception const& e) {
      LOG(WARNING) << handle << " exception: " << e.what();
    }
    numGames++;
  }
}

void trainLoop(
    std::shared_ptr<BosTrainer> trainer,
    std::shared_ptr<visdom::Visdom> vs) {
  int numThreads = FLAGS_num_game_threads;
  if (trainer->isServer() && FLAGS_bos_model_type != "idle") {
    if (FLAGS_heterogeneous) {
      numThreads = 0;
    } else {
      // Leave some room for actual training on server processes
      numThreads = std::max(1, numThreads - 5);
    }
  }

  // Create dummy scenarios -- they'll be lazily instantiated later
  {
    std::lock_guard<std::mutex> lock(gScenariosMutex);
    auto multiplier = (FLAGS_mode == "polit" ? 5 : 1);
    for (auto i = 0; i < multiplier * numThreads; i++) {
      gScenarios.push_back(nullptr);
    }
  }

  std::vector<std::thread> threads;
  auto startGameThreads = [&]() {
    gStopGameThreads.store(false);
    for (auto i = 0; i < numThreads; i++) {
      threads.emplace_back(runGameThread, trainer, i);
    }
  };
  auto stopGameThreads = [&]() {
    gStopGameThreads.store(true);
    trainer->reset();
    for (auto& thread : threads) {
      thread.join();
    }
    threads.clear();
  };

  startGameThreads();

  while (true) {
    trainer->update();
    if (!trainer->isServer()) {
      // This worker doesn't produce any gradients but still takes part in
      // gradient exchanges and model updates so that its local model is
      // up-to-date.
      trainer->optim()->zero_grad();
      trainer->loop().allreduceGradients(false);
      {
        auto lock = trainer->modelWriteLock();
        trainer->optim()->step();
      }
    }
  }

  // Write out final checkpoint
  if (dist::globalContext()->rank == 0 && FLAGS_bos_model_type != "idle") {
    trainer->loop().checkpointer->checkpointTrainer();
  }
  stopGameThreads();
}

void setDefaultFlags() {
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
  if (FLAGS_macro_batch_size < 0) {
    FLAGS_macro_batch_size = FLAGS_batch_size;
  }
}

} // namespace

int main(int argc, char** argv) {
  FLAGS_optim = "adam";
  FLAGS_lr = 5e-4;
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  cherrypi::initLogging(argv[0], "", true);
  ForkServer::startForkServer();
  cherrypi::init();
  dist::init();
  setDefaultFlags();

  VLOG(1) << "Gloo rank: " << dist::globalContext()->rank << " and size "
          << dist::globalContext()->size;
  dist::setGPUToLocalRank();

  std::shared_ptr<visdom::Visdom> vs;
  if (dist::globalContext()->rank == 0) {
    VLOG(0) << "Training run started with " << dist::globalContext()->size
            << " workers";

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
      oss << "<h4>Supervised BOS Training</h4>";
      oss << "<p>Training started " << utils::curTimeString() << "</p>";
      oss << "<hl><p>";
      for (auto const& it : utils::cmerge(
               std::map<std::string, std::string>{{"build", FLAGS_build},
                                                  {"bandit", FLAGS_bandit}},
               utils::gflagsValues(__FILE__),
               cpid::optimizerFlags(),
               bos::modelFlags())) {
        oss << "<b>" << it.first << "</b>: " << it.second << "<br>";
      }
      oss << "</p>";
      vs->text(oss.str());
    }

    VLOG(0) << std::string(42, '=');
    for (auto const& it : utils::cmerge(
             std::map<std::string, std::string>{{"build", FLAGS_build},
                                                {"bandit", FLAGS_bandit}},
             utils::gflagsValues(__FILE__),
             cpid::optimizerFlags(),
             bos::modelFlags())) {
      VLOG(0) << fmt::format("{}: {}", it.first, it.second);
    }
    VLOG(0) << std::string(42, '=');
  }

  auto model = bos::modelMakeFromCli(FLAGS_dropout);
  dist::broadcast(model);
  if (FLAGS_gpu) {
    model->to(torch::kCUDA);
  }
  model->train();

  auto loop = [&]() -> std::shared_ptr<UpdateLoop> {
    auto mtype = FLAGS_bos_model_type;
    if (mtype == "idle") {
      auto l = std::make_shared<IdleUpdateLoop>(FLAGS_batch_size);
      return l;
    } else if (mtype == "lstm" || mtype == "celstm") {
      auto l = std::make_shared<BpttUpdateLoop>(
          FLAGS_batch_size, FLAGS_bptt, FLAGS_decisions_only, vs);
      l->initialNonDecisionSamples = FLAGS_initial_nondec_samples;
      l->spatialFeatures = (mtype == "celstm");
      l->nonSpatialFeatures = (mtype == "lstm" || mtype == "celstm");
      return l;
    } else {
      auto l = std::make_shared<LinearModelUpdateLoop>(
          FLAGS_macro_batch_size, FLAGS_batch_size, FLAGS_decisions_only, vs);
      l->initialNonDecisionSamples = FLAGS_initial_nondec_samples;
      return l;
    }
  }();

  auto optim = selectOptimizer(model);
  auto metrics = std::make_shared<MetricsContext>();
  bool isServer =
      dist::globalContext()->rank % FLAGS_num_workers_per_trainer == 0;
  if (FLAGS_heterogeneous) {
    try {
      if (!torch::cuda::is_available()) {
        isServer = false;
      }
    } catch (std::exception&) {
      isServer = false;
    }
  }
  auto trainer = std::make_shared<BosTrainer>(isServer, model, optim, loop);

  trainer->setMetricsContext(metrics);
  trainer->loop().checkpointer = std::make_unique<cpid::Checkpointer>(trainer);
  trainer->loop().checkpointer->epochLength(5);
  std::string checkpointPath = std::getenv("SLURM_ARRAY_TASK_ID") == NULL
      ? FLAGS_checkpoint
      : fmt::format(
            "{}-{}", FLAGS_checkpoint, std::getenv("SLURM_ARRAY_TASK_ID"));
  trainer->loop().checkpointer->checkpointPath(checkpointPath);
  trainer->saveSamples = FLAGS_save_samples;

  if (!FLAGS_initial_model.empty()) {
    ag::load(FLAGS_initial_model, model);
  } else if (
      fsutils::exists(checkpointPath) &&
      FLAGS_bos_model_type !=
          "idl"
          "e") {
    VLOG(0) << "Found existing checkpoint " << checkpointPath << "; loading it";
    ag::load(checkpointPath, trainer);
  }

  if (FLAGS_mode == "evaluate") {
    trainer->evaluateOffline();
  } else if (FLAGS_mode == "offline") {
    trainer->runOffline();
  } else if (FLAGS_mode == "online" || FLAGS_mode == "polit") {
    loop->saveModelInterval = 50;
    trainLoop(trainer, vs);
  } else if (FLAGS_mode == "listbuilds") {
    int id = 0;
    for (auto race :
         {+tc::BW::Race::Zerg, +tc::BW::Race::Terran, +tc::BW::Race::Protoss}) {
      for (auto it : model::buildOrdersForTraining()) {
        // For simplicity, include openings here so that we can use the same
        // list of builds for input embeddings and output heads.
        if (!it.second.validSwitch() && !it.second.validOpening()) {
          continue;
        }
        auto& o = it.second.ourRaces_;
        if (std::find(o.begin(), o.end(), +tc::BW::Race::Zerg) == o.end()) {
          continue;
        }
        auto& r = it.second.enemyRaces_;
        if (std::find(r.begin(), r.end(), race) != r.end()) {
          std::cout << "{\"" << race._to_string()[0] << "-" << it.first
                    << "\", " << id++ << "}," << std::endl;
        }
      }
    }
  } else {
    LOG(FATAL) << "Unknown mode: " << FLAGS_mode;
  }

  return EXIT_SUCCESS;
}
