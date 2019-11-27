/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <chrono>
#include <cmath>
#include <experimental/filesystem>
#include <fenv.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>

#include "../../scripts/distributed_bench/environments.h"
#include "common/autograd.h"
#include "common/fsutils.h"
#include "common/rand.h"
#include "cpid/a2c.h"
#include "cpid/batcher.h"
#include "cpid/checkpointer.h"
#include "cpid/distributed.h"
#include "cpid/evaluator.h"
#include "cpid/metrics.h"
#include "cpid/optimizers.h"
#include "cpid/sampler.h"
#include "custombatcher.h"
#include "featurize.h"
#include "flags.h"
#include "gameutils/microscenarioproviderfixed.h"
#include "gameutils/scenarioprovider.h"
#include "keys.h"
#include "microplayer.h"
#include "mockmodule.h"
#include "model.h"
#include "modules.h"
#include "parameters.h"
#include "sampler.h"
#include "src/utils.h"
#include "targetingmodule.h"
#include "threadpool.h"
#include <torch/torch.h>
#include <torchcraft/constants.h>
#include <visdom/visdom.h>

const std::string kValueKey = "V";
const std::string kQKey = "Q";
const std::string kPiKey = "Pi";
const std::string kSigmaKey = "std";
const std::string kActionQKey = "actionQ";
const std::string kActionKey = "action";
const std::string kPActionKey = "pAction";
using namespace cpid;
using namespace cherrypi;

auto const vopts = &visdom::makeOpts;
constexpr const auto vappend = visdom::UpdateMethod::Append;
constexpr const auto vnone = visdom::UpdateMethod::None;

tc::BW::UnitType kUnitToSpawnMine = tc::BW::UnitType::Zerg_Zergling;
tc::BW::UnitType kUnitToSpawnThem = tc::BW::UnitType::Zerg_Zergling;
ModelType kModelType;
Targeting kPolicy = Targeting::Trainer;

class WRLogger {
 public:
  WRLogger(std::shared_ptr<MetricsContext> metrics)
      : metricsContext_(metrics) {}

  void log(bool victory) {
    std::lock_guard<std::mutex> lk(mut_);
    gameCount_++;
    if (victory) {
      victoryCount_++;
    }
    if (gameCount_ >= 50 && !FLAGS_eval) {
      metricsContext_->pushEvent(
          "winrate", double(victoryCount_) / double(gameCount_));
      gameCount_ = 0;
      victoryCount_ = 0;
    }
  }

  void print_final() {
    LOG(INFO) << "Final winrate " << double(victoryCount_) / double(gameCount_);
  }

 private:
  std::shared_ptr<MetricsContext> metricsContext_;
  std::mutex mut_;
  int gameCount_ = 0;
  int victoryCount_ = 0;
};

namespace dist = cpid::distributed;

double runMainLoop(
    std::shared_ptr<Trainer> trainer,
    int ind,
    std::shared_ptr<WRLogger> wrlog,
    bool dumpReplays,
    bool evalMode = false,
    int num_episodes = 1000000000,
    bool log = true) {
  common::Rand::setLocalSeed(
      ind * distributed::globalContext()->size +
      distributed::globalContext()->rank);
  distributed::setGPUToLocalRank();

  EpisodeHandle myHandle, myOtherHandle;
  FixedScenario scenario;
  scenario.addUpgrade(0, tc::BW::UpgradeType::Metabolic_Boost, 1);
  scenario.addUpgrade(1, tc::BW::UpgradeType::Metabolic_Boost, 1);
  float vSpread = 15.;
  if (FLAGS_scenario == "zergtank") {
    vSpread = 5.;
  }
  if (FLAGS_scenario == "zergfb") {
    vSpread = 5.;
  }
  float theirVSpread = 15.;
  scenario.allies().push_back(
      {FLAGS_scenario_size, kUnitToSpawnMine, 80, 132, 5, vSpread});
  int theirCount = FLAGS_scenario_size;
  if (FLAGS_scenario == "marine") {
    theirCount = FLAGS_scenario_size;
  } else if (FLAGS_scenario == "muta") {
    theirCount = FLAGS_scenario_size + 2;
    theirVSpread = 25.;
    scenario.addUpgrade(0, tc::BW::UpgradeType::Zerg_Flyer_Attacks, 3);
    scenario.addUpgrade(1, tc::BW::UpgradeType::Zerg_Flyer_Attacks, 3);
  } else if (FLAGS_scenario == "wraith") {
    theirCount = FLAGS_scenario_size + 2;
    theirVSpread = 25.;
  } else if (FLAGS_scenario == "scout") {
    theirCount = FLAGS_scenario_size + 2;
    scenario.addUpgrade(0, tc::BW::UpgradeType::Protoss_Air_Weapons, 3);
    scenario.addUpgrade(1, tc::BW::UpgradeType::Protoss_Air_Weapons, 3);
    theirVSpread = 25.;
  } else if (FLAGS_scenario == "corsair") {
    theirCount = FLAGS_scenario_size + 1;
    scenario.addUpgrade(0, tc::BW::UpgradeType::Protoss_Air_Weapons, 3);
    scenario.addUpgrade(1, tc::BW::UpgradeType::Protoss_Air_Weapons, 3);
  } else if (FLAGS_scenario == "zergtank") {
    theirCount = FLAGS_scenario_size / 3;
  } else if (FLAGS_scenario == "zergfb") {
    theirCount = FLAGS_scenario_size / 3;
    theirVSpread = 30.;
  } else if (FLAGS_scenario == "dragzeal") {
    scenario.addUpgrade(0, tc::BW::UpgradeType::Leg_Enhancements, 1);
    scenario.addUpgrade(1, tc::BW::UpgradeType::Leg_Enhancements, 1);
    scenario.addUpgrade(0, tc::BW::UpgradeType::Singularity_Charge, 1);
    scenario.addUpgrade(1, tc::BW::UpgradeType::Singularity_Charge, 1);
    scenario.allies().push_back({FLAGS_scenario_size,
                                 tc::BW::UnitType::Protoss_Zealot,
                                 65,
                                 132,
                                 5,
                                 vSpread});
  } else if (FLAGS_scenario == "zerghydra") {
    scenario.addUpgrade(0, tc::BW::UpgradeType::Grooved_Spines, 1);
    scenario.addUpgrade(1, tc::BW::UpgradeType::Grooved_Spines, 1);
    scenario.allies().push_back({FLAGS_scenario_size,
                                 tc::BW::UnitType::Zerg_Hydralisk,
                                 65,
                                 132,
                                 5,
                                 vSpread});
  } else {
    LOG(FATAL) << "Unknown scenario " << FLAGS_scenario;
  }
  theirCount += FLAGS_difficulty;
  scenario.enemies().push_back(
      {theirCount, kUnitToSpawnThem, 170, 132, 5, theirVSpread});
  if (FLAGS_scenario == "zerghydra") {
    scenario.enemies().push_back({theirCount,
                                  tc::BW::UnitType::Zerg_Hydralisk,
                                  190,
                                  132,
                                  5,
                                  theirVSpread});
  }
  if (FLAGS_scenario == "dragzeal") {
    scenario.enemies().push_back({theirCount,
                                  tc::BW::UnitType::Protoss_Dragoon,
                                  190,
                                  132,
                                  5,
                                  theirVSpread});
  }

  bool is_main = ind == 0 && distributed::globalContext()->rank == 0;

  auto make_provider = [&]() {
    auto s = std::make_shared<MicroScenarioProviderFixed>(scenario);
    s->setMaxFrames(5000);
    s->setGui(FLAGS_enable_gui && ind == 0);
    s->setMapPathPrefix(FLAGS_map_path_prefix);
    return s;
  };

  std::shared_ptr<MicroScenarioProviderFixed> provider;
  LOG(INFO) << "starting playing thread " << ind << " rank "
            << distributed::globalContext()->rank;

  auto respawn = [&](const std::string replay_path) {
    if (!provider)
      provider = make_provider();
    provider->setReplay(replay_path);
    return provider->startNewScenario(
        [&](BasePlayer* bot) {
          bot->addModule(Module::make<TopModule>());
          bot->addModule(Module::make<MockTacticsModule>());
          if (!evalMode || kPolicy == Targeting::Trainer) {
            bot->addModule(Module::make<TargetingModule>(
                Targeting::Trainer,
                trainer,
                trainer->startEpisode(),
                kModelType));
          } else {
            bot->addModule(Module::make<TargetingModule>(
                kPolicy, nullptr, trainer->startEpisode(), kModelType));
          }
          bot->addModule(Module::make<UPCToCommandModule>());
          bot->setRealtimeFactor(FLAGS_enable_gui ? FLAGS_realtime : -1);
          if (!replay_path.empty()) {
            bot->dumpTraceAlongReplay(replay_path);
          }
        },
        [&](BasePlayer* bot) {
          bot->addModule(Module::make<TopModule>());
          bot->addModule(Module::make<MockTacticsModule>());
          auto h = trainer->startEpisode();
          bot->addModule(Module::make<TargetingModule>(
              Targeting::BuiltinAI, nullptr, std::move(h), kModelType));
          bot->addModule(Module::make<UPCToCommandModule>());
          bot->setLogFailedCommands(false);
          bot->setRealtimeFactor(-1);
        });
  };

  int won = 0;
  for (int i = 0; i < num_episodes; ++i) {
    try {
      std::shared_ptr<BasePlayer> p1, p2;
      std::string replay_path = "";
      if (is_main && dumpReplays && (evalMode || i % 10 == 0)) {
        replay_path = "replay_" + std::to_string(i) + ".rep";
      }
      std::tie(p1, p2) = respawn(replay_path);

      int steps = 0;

      while (!provider->isFinished(steps, true)) {
        steps++;
        p1->step();
        p2->step();
      }
      int units1 = p1->state()->unitsInfo().myUnits().size();
      int units2 = p2->state()->unitsInfo().myUnits().size();
      wrlog->log(units1 > units2);
      if (evalMode && log) {
        if (units1 > units2) {
          LOG(INFO) << "WON " << units1 << " - " << units2;
        } else {
          LOG(INFO) << "LOST " << units1 << " - " << units2;
        }
      }
      won += (units1 > units2) ? 1 : 0;
      p1->findModule<TargetingModule>()->sendLastFrame(p1->state());
      p2->findModule<TargetingModule>()->sendLastFrame(p2->state());
      provider->endScenario();
    } catch (std::exception const& e) {
      VLOG(-1) << "Worker with id " << ind << " and rank "
               << distributed::globalContext()->rank
               << " got exception: " << e.what();
      // trainer->forceStopEpisode(myHandle);
      // force a new scenario
      provider = nullptr;
    }
  }
  if (evalMode && log) {
    LOG(INFO) << "Total_winrate " << won / double(num_episodes);
  }
  return won / double(num_episodes);
}

int main(int argc, char** argv) {
  // feenableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  ForkServer::startForkServer();
  SimpleUnitFeaturizer::init();
  LOG(INFO) << "Init distributed...";

  if (FLAGS_num_workers < FLAGS_batch_size) {
    LOG(FATAL) << "The number of worker " << FLAGS_num_workers
               << " is to small to fill the batches " << FLAGS_batch_size;
  }
  if (FLAGS_scenario == "marine") {
    kUnitToSpawnMine = tc::BW::UnitType::Terran_Marine;
    kUnitToSpawnThem = tc::BW::UnitType::Terran_Marine;
  } else if (FLAGS_scenario == "wraith") {
    kUnitToSpawnMine = tc::BW::UnitType::Terran_Wraith;
    kUnitToSpawnThem = tc::BW::UnitType::Terran_Wraith;
  } else if (FLAGS_scenario == "corsair") {
    kUnitToSpawnMine = tc::BW::UnitType::Protoss_Corsair;
    kUnitToSpawnThem = tc::BW::UnitType::Protoss_Corsair;
  } else if (FLAGS_scenario == "muta") {
    kUnitToSpawnMine = tc::BW::UnitType::Zerg_Mutalisk;
    kUnitToSpawnThem = tc::BW::UnitType::Zerg_Mutalisk;
  } else if (FLAGS_scenario == "scout") {
    kUnitToSpawnMine = tc::BW::UnitType::Protoss_Scout;
    kUnitToSpawnThem = tc::BW::UnitType::Protoss_Scout;
  } else if (FLAGS_scenario == "zergtank") {
    kUnitToSpawnMine = tc::BW::UnitType::Zerg_Zergling;
    kUnitToSpawnThem = tc::BW::UnitType::Terran_Siege_Tank_Siege_Mode;
  } else if (FLAGS_scenario == "zergfb") {
    kUnitToSpawnMine = tc::BW::UnitType::Zerg_Zergling;
    kUnitToSpawnThem = tc::BW::UnitType::Terran_Firebat;
  } else if (FLAGS_scenario == "dragzeal") {
    kUnitToSpawnMine = tc::BW::UnitType::Protoss_Zealot;
    kUnitToSpawnThem = tc::BW::UnitType::Protoss_Zealot;
  } else if (FLAGS_scenario == "zerghydra") {
    kUnitToSpawnMine = tc::BW::UnitType::Zerg_Zergling;
    kUnitToSpawnThem = tc::BW::UnitType::Zerg_Zergling;
  } else {
    LOG(FATAL) << "Unknown scenario " << FLAGS_scenario;
  }
  LOG(INFO) << "Playing " << FLAGS_scenario << " with " << FLAGS_scenario_size
            << " units";

  std::string model_type = FLAGS_model_type;
  std::transform(
      model_type.begin(), model_type.end(), model_type.begin(), ::tolower);
  if (model_type == "argmax_dm") {
    kModelType = ModelType::Argmax_DM;
  } else if (model_type == "argmax_pem") {
    kModelType = ModelType::Argmax_PEM;
  } else if (model_type == "lp_dm") {
    kModelType = ModelType::LP_DM;
  } else if (model_type == "lp_pem") {
    kModelType = ModelType::LP_PEM;
  } else if (model_type == "quad_dm") {
    kModelType = ModelType::Quad_DM;
  } else if (model_type == "quad_pem") {
    kModelType = ModelType::Quad_PEM;
  } else {
    LOG(FATAL) << "Unknown model type " << FLAGS_model_type;
  }

  cherrypi::init();
  dist::init();
  dist::setGPUToLocalRank();
  common::Rand::setSeed(FLAGS_seed + distributed::globalContext()->rank);
  LOG(INFO) << "Distributed init done";
  LOG(INFO) << "Using seed " << FLAGS_seed;

  Parameters::init();
  std::shared_ptr<MetricsContext> metrics;
  metrics = std::make_shared<MetricsContext>();

  auto model_plain =
      TargetingModel()
          .model_type(kModelType)
          .inFeatures(cherrypi::SimpleUnitFeaturizer::kNumChannels)
          .inPairFeatures(
              FLAGS_use_pairwise_feats
                  ? cherrypi::TargetingModule::kNumPairFeatures
                  : 0);
  model_plain.metrics_ = metrics;

  auto model = model_plain.make();

  if (dist::globalContext()->size > 1) {
    LOG(INFO) << "Broadcasting parameters";
    for (auto& p : model->parameters()) {
      dist::broadcast(p);
    }
  }

  if (!FLAGS_cpu_only) {
    model->to(torch::kCUDA);
  }

  auto optim = selectOptimizer(model);
  auto update_lr = [](ag::Optimizer optim, float lr) {
    if (FLAGS_optim == "adam") {
      std::static_pointer_cast<torch::optim::Adam>(optim)
          ->options.learning_rate_ = lr;
    } else if (FLAGS_optim == "sgd") {
      std::static_pointer_cast<torch::optim::SGD>(optim)
          ->options.learning_rate_ = lr;
    } else if (FLAGS_optim == "rmsprop") {
      std::static_pointer_cast<torch::optim::RMSprop>(optim)
          ->options.learning_rate_ = lr;
    } else {
      LOG(FATAL) << "Unsupported optimizer " << FLAGS_optim;
    }
  };
  update_lr(optim, Parameters::get_float("lr"));

  auto batcher =
      std::make_unique<CustomBatcher>(model, FLAGS_batch_size, -1, false);

  std::shared_ptr<SyncTrainer> trainer = std::make_shared<ContinuousA2C>(
      model,
      optim,
      std::make_unique<CustomGaussianSampler>(),
      std::move(batcher),
      FLAGS_returns_length,
      1,
      FLAGS_batch_size,
      FLAGS_discount,
      FLAGS_ratio_clamp,
      0.01,
      Parameters::get_float("policy_ratio"),
      true,
      true,
      true,
      (FLAGS_clip_grad ? 5. : -1.));

  trainer->setMetricsContext(metrics);

  std::shared_ptr<visdom::Visdom> vs;

  if (dist::globalContext()->rank == 0) {
    VLOG(0) << "Training run started with " << dist::globalContext()->size
            << " workers";

    for (auto const& it : utils::gflagsValues()) {
      VLOG(0) << it.first << ": " << it.second;
    }
  }

  Checkpointer chk(trainer);
  const int epochLength = 50;
  chk = chk.epochLength(FLAGS_epoch_size)
            .printMetricsSummary(true)
            .aggregateMetrics(true)
            .epochLength(epochLength)
            .reduceMax(false)
            .compareMetric("winrate")
            .visdom(vs)
            .visdomKeys({"cumulated_reward", "policy_loss", "value_loss"});

  auto checkpointer = std::make_unique<Checkpointer>(chk);
  const auto model_path = checkpointer->getModelPath();

  if (std::experimental::filesystem::exists(model_path)) {
    LOG(INFO) << "Found existing model at " << model_path
              << " going to load it";
    ag::load(model_path, trainer);
  }

  if (FLAGS_eval) {
    trainer->setTrain(false);
    auto evaluator = trainer->makeEvaluator(
        10000, std::make_unique<CustomGaussianSampler>());
    if (common::fsutils::exists(FLAGS_eval_policy)) {
      LOG(INFO) << "Loading model from " << FLAGS_eval_policy;
      ag::load(FLAGS_eval_policy, evaluator);
    } else if (FLAGS_eval_policy == "random") {
      kPolicy = Targeting::Random;
    } else if (FLAGS_eval_policy == "random_nc") {
      kPolicy = Targeting::Random_NoChange;
    } else if (FLAGS_eval_policy == "weakest_closest") {
      kPolicy = Targeting::Weakest_Closest;
    } else if (FLAGS_eval_policy == "weakest_closest_NOK") {
      kPolicy = Targeting::Weakest_Closest_NOK;
    } else if (FLAGS_eval_policy == "weakest_closest_NOK_NC") {
      kPolicy = Targeting::Weakest_Closest_NOK_NC;
    } else if (FLAGS_eval_policy == "weakest_closest_NOK_smart") {
      kPolicy = Targeting::Weakest_Closest_NOK_smart;
    } else if (FLAGS_eval_policy == "closest") {
      kPolicy = Targeting::Closest;
    } else if (FLAGS_eval_policy == "noop") {
      kPolicy = Targeting::Noop;
    } else if (FLAGS_eval_policy == "even_split") {
      kPolicy = Targeting::Even_Split;
    } else {
      LOG(FATAL) << "invalid eval policy = " << FLAGS_eval_policy;
    }
  }
  auto wrlog = std::make_shared<WRLogger>(metrics);
  std::vector<std::thread> ths;
  for (size_t i = 0; i < FLAGS_num_workers; ++i) {
    int nbEpisodes = FLAGS_num_episodes;
    if (FLAGS_eval) {
      nbEpisodes /= FLAGS_num_workers;
    }
    ths.emplace_back(
        runMainLoop,
        trainer,
        i,
        wrlog,
        FLAGS_dump_replay,
        FLAGS_eval,
        nbEpisodes,
        true);
  }
  int count = 0;
  while (!FLAGS_eval) {
    if (!trainer->update()) {
      continue;
    }
    count++;
    checkpointer->updateDone(count);
    if (FLAGS_warmup >= 0) {
      float new_lr = FLAGS_lr *
          std::min(pow(count, -0.5),
                   count * pow(FLAGS_warmup * epochLength, -1.5));
      update_lr(optim, new_lr);
    }
  }
  for (size_t i = 0; i < FLAGS_num_workers; ++i) {
    ths[i].join();
  }
  if (FLAGS_eval) {
    wrlog->print_final();
  }

  return 0;
}
