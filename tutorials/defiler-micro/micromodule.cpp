/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "micromodule.h"

#include "common/autograd.h"
#include "cpid/evaluator.h"
#include "modules/cherryvisdumper.h"

#include "utils.h"

#include "common.h"
#include "flags.h"
#include "model.h"
#include "modeldefiler.h"

#include <visdom/visdom.h>

using namespace cherrypi;

namespace microbattles {

std::shared_ptr<MicroModule> findMicroModule(std::shared_ptr<BasePlayer> bot) {
  // We need to locate at least one micro module for the training script to
  // work.
  if (bot->findModule<MicroModule>()) {
    // Look directly for a micro module
    return bot->findModule<MicroModule>();
  } else if (bot->findModule<SquadCombatModule>()) {
    // if not found, look inside squad combat, they might be added as a micro
    // model inside
    return std::dynamic_pointer_cast<MicroModule>(
        bot->findModule<SquadCombatModule>()->getModel("defilerModel"));
  }
  throw std::runtime_error("No micro module is found!");
}

MicroModule::MicroModule(
    std::shared_ptr<TrainingSetup> training,
    std::shared_ptr<cpid::Trainer> trainer,
    std::unique_ptr<cherrypi::Reward>&& reward)
    : Module(), setup(training), reward_(std::move(reward)), trainer(trainer) {
  setName("MicroLearner");
  featurizer_ = setup->model->getFeaturizer();
}

void MicroModule::onGameStart(State* state) {
  reward_->begin(state);
  lastFeatures_.reset();
  lastModelOut_.reset();
  episodeStartFrame_ = state->currentFrame();
  started_ = true;
  firstAllyCount = reward_->initialAllyCount;
  firstEnemyCount = reward_->initialEnemyCount;
  firstAllyHp = reward_->initialAllyHp;
  firstEnemyHp = reward_->initialEnemyHp;
}

void MicroModule::step(State* state) {
  if (!started_ || !handle) {
    return;
  }

  try {
    if (reward_->terminate(state)) {
      trainerStep(state, true);
      return;
    }

    if (illustrate_) {
      illustrate(state);
    }

    act(state);

  } catch (std::exception const& trainingException) {
    VLOG(0) << "Caught exception in MicroModule: " << trainingException.what();
  } catch (...) {
    VLOG(0) << "Caught unknown exception in MicroModule";
  }
}

void MicroModule::forward(State* state) {
  if ((!started_ || !handle) && !inFullGame) {
    return;
  }

  lastForwardFrame_ = episodeCurrentFrame(state);
  lines_.clear();
  actionPerUnit_.clear();
  torch::NoGradGuard g_;
  auto stateTensor = featurizer_->featurize(state);
  plotHeatmaps(state, stateTensor);
  stateTensor = common::applyTransform(stateTensor, [](torch::Tensor const& t) {
    return t.to(defaultDevice());
  });
  // Perform batch forward pass and assign all actions
  auto modelOut = inFullGame ? setup->model->forward(stateTensor)
                             : trainer->forward(stateTensor, handle);
  modelOut = trainer->sample(modelOut);
  plotHeatmaps(state, modelOut);

  auto actions = std::dynamic_pointer_cast<PFMicroActionModel>(setup->model)
                     ->decodeOutput(state, stateTensor, modelOut);

  if (setup->trainerTakesPreviousActionAndState) {
    trainerStep(state, false);
    lastFeatures_ = stateTensor;
    lastModelOut_ = modelOut;
  } else {
    lastFeatures_ = stateTensor;
    lastModelOut_ = modelOut;
    trainerStep(state, false);
  }
  if (auto tracer = state->board()->getTraceDumper()) {
    if (setup->modelProvidesValueKey) {
      tracer->dumpGameValue(
          state,
          "predicted value",
          modelOut.getDict()
              .at(cpid::kValueKey)
              .get()
              .to(torch::kCPU)
              .view({1})
              .item<float>());
    }
    tracer->dumpGameValue(state, "frame reward", frameReward);
  }
  if (FLAGS_print_rewards) {
    frameRewards.push_back(frameReward);
  }

  for (auto& action : actions) {
    auto upc = actionToUPC(action, state);
    if (upc != nullptr) {
      if (unitActionValidUntil_[action.unit] < episodeCurrentFrame(state)) {
        unitActionValidUntil_[action.unit] =
            common::Rand::sample(actionLastingTimeDist_) * 24 +
            episodeCurrentFrame(state);
        actionPerUnit_[action.unit] = MicroAction();
        actionPerUnit_[action.unit].upc = upc;
        actionPerUnit_[action.unit].isFinal = true;
        CVIS_LOG_UNIT(state, action.unit)
            << "Action issued for unit to cast " << action.action
            << " to pos x:" << action.target_p.x << " y:" << action.target_p.y
            << " until " << unitActionValidUntil_[action.unit];
      } else {
        actionPerUnit_[action.unit] = MicroAction();
        actionPerUnit_[action.unit].upc = nullptr;
        actionPerUnit_[action.unit].isFinal = true;
      }
    }
  }
}

MicroAction MicroModule::decode(Unit* unit) {
  if (actionPerUnit_.find(unit) != actionPerUnit_.end()) {
    return actionPerUnit_[unit];
  }
  return MicroAction();
}

void MicroModule::act(State* state) {
  if (episodeCurrentFrame(state) == 0 ||
      episodeCurrentFrame(state) - lastForwardFrame_ < FLAGS_frame_skip) {
    return;
  }

  forward(state);
  for (auto const& pair : actionPerUnit_) {
    auto upc = pair.second.getFinalUPC();
    if (upc != nullptr) {
      state->board()->postUPC(std::move(upc), kRootUpcId, this);
    }
  }
}

std::shared_ptr<UPCTuple> MicroModule::actionToUPC(
    PFMicroActionModel::PFMicroAction& action,
    cp::State* state) {
  if (action.action != PFMicroActionModel::PFMicroAction::Attack) {
    attacks_[action.unit] = nullptr;
  }
  switch (action.action) {
    case PFMicroActionModel::PFMicroAction::Attack:
      addLine(action.unit, Position{action.target_u}, tc::BW::Color::Red);
      if (attacks_[action.unit] != action.target_u) {
        attacks_[action.unit] = action.target_u;
        return utils::makeSharpUPC(
            action.unit, action.target_u, cp::Command::Delete);
      } else {
        return nullptr;
      }
    case PFMicroActionModel::PFMicroAction::Move:
      addLine(action.unit, Position{action.target_p}, tc::BW::Color::White);
      return cp::utils::makeSharpUPC(
          action.unit, action.target_p, cp::Command::Move);
    case PFMicroActionModel::PFMicroAction::None:
      return cp::utils::makeSharpUPC(
          action.unit, action.unit, cp::Command::Move);
    case PFMicroActionModel::PFMicroAction::DarkSwarm:
      if (action.unit->unit.energy < 100) {
        return nullptr;
      }
      addLine(action.unit, Position{action.target_p}, tc::BW::Color::Black);
      numericMetricsByUnit["darkSwarmNum"][action.unit->id]++;
      return cp::utils::makeSharpUPC(
          action.unit,
          action.target_p,
          cp::Command::Cast,
          buildtypes::Dark_Swarm);
    case PFMicroActionModel::PFMicroAction::Plague:
      if (action.unit->unit.energy < 150 ||
          !state->hasResearched(buildtypes::Plague)) {
        return nullptr;
      }
      addLine(action.unit, Position{action.target_p}, tc::BW::Color::Yellow);
      numericMetricsByUnit["plagueNum"][action.unit->id]++;
      return cp::utils::makeSharpUPC(
          action.unit, action.target_p, cp::Command::Cast, buildtypes::Plague);
    default:
      throw std::runtime_error(fmt::format(
          "Didn't have a handler for PFMicroAction {}", action.action));
  }
}

void MicroModule::plotHeatmaps(cp::State* state, ag::Variant output) {
  auto cvis = state->board()->getTraceDumper();
  if (!output.isDict()) {
    return;
  }

  for (auto const& p : output.getDict()) {
    if (!p.second.isDict()) {
      continue;
    }
    auto parts = common::stringSplit(p.first, '_');
    if (parts.size() >= 3) {
      continue;
    }
    if (parts[0] != "heatmaps") {
      continue;
    }
    float scaling = -1;
    if (parts.size() == 1) {
      scaling = tc::BW::XYPixelsPerBuildtile;
    } else {
      scaling = std::atof(parts[1].c_str()) * tc::BW::XYPixelsPerBuildtile;
    }
    for (auto const& p2 : p.second.getDict()) {
      auto tensorCpu = p2.second.get().to(at::kCPU);
      LOG_IF(FATAL, tensorCpu.dim() != 2)
          << "Heatmap " << p2.first << " is not 2D. Shape is "
          << tensorCpu.sizes();
      heatmap_[p2.first] = tensorCpu;
    }
    if (cvis) {
      cvis->dumpTerrainHeatmaps(
          state, p.second.getDict(), {0, 0}, {scaling, scaling});
    }
  }
}

void MicroModule::updateHeatMapToVisdom() {
  auto displayHeatmap = [&](torch::Tensor map, std::string const& name) {
    map = map.clone().to(at::kFloat);
    map = map.masked_fill_(map == kfInfty, -1);
    if (setup->vs) {
      setup->vs->heatmap(
          common::flip(map, 0),
          name,
          FLAGS_visdom_env,
          visdom::makeOpts({{"title", name}}));
    }
  };
  for (auto const& pair : heatmap_) {
    displayHeatmap(pair.second, pair.first);
  }
}

void MicroModule::illustrate(State* state) {
  constexpr auto c = 2;
  auto middleX = kMapWidth / 2;
  auto middleY = kMapHeight / 2;
  utils::drawLine(
      state,
      {middleX, middleY - c},
      {middleX, middleY + c},
      tc::BW::Color::Green);
  utils::drawLine(
      state,
      {middleX - c, middleY},
      {middleX + c, middleY},
      tc::BW::Color::Green);
  for (auto& l : lines_) {
    if (l.unit) {
      utils::drawLine(state, l.unit, l.p2, l.color);
    } else {
      utils::drawLine(state, l.p1, l.p2, l.color);
    }
  }
  auto rewardString = fmt::format("Reward: {}", reward_->reward);
  utils::drawTextScreen(state, 0, 0, rewardString);
  VLOG(3) << rewardString;
}

void MicroModule::trainerStep(cp::State* state, bool isFinal) {
  if (!started_ || inFullGame) {
    // Should only be called after started and not in full game (eg loaded
    // inside bot)
    return;
  }

  if (isFinal) {
    started_ = false;
  }

  if (frameRewards.empty() && FLAGS_train_on_baseline_rewards && !test) {
    throw std::runtime_error("No baseline rewards found for this eposide");
  }

  reward_->stepReward(state);
  frameReward = reward_->reward;
  lastAllyCount = reward_->allyCount;
  lastAllyHp = reward_->allyHp;
  lastEnemyCount = reward_->enemyCount;
  lastEnemyHp = reward_->enemyHp;
  won = reward_->won;

  float baselineReward = (!test && FLAGS_train_on_baseline_rewards &&
                          idxFrames_ < frameRewards.size() - 1)
      ? frameRewards[idxFrames_]
      : 0;
  idxFrames_++;

  if (isFinal) {
    // if episode is longer than baseline, we took the end of game frames
    if (!test && FLAGS_train_on_baseline_rewards) {
      for (int i = idxFrames_; i < frameRewards.size() - 1; i++) {
        baselineReward += frameRewards[i];
      }
      // By doint this, we always have end of game reward for the end of game
      // reward
      baselineReward += frameRewards.back();
    }

    if (auto tracer = state->board()->getTraceDumper()) {
      CVIS_LOG(state) << "Final state reward: " << frameReward;
      CVIS_LOG(state) << "Final baseline reward: " << baselineReward;
      CVIS_LOG(state) << "Delta reward: " << frameReward - baselineReward;
      CVIS_LOG(state) << "Units left: " << lastAllyCount
                      << state->unitsInfo().myUnits();
      CVIS_LOG(state) << "Enemy left: " << lastEnemyCount
                      << state->unitsInfo().enemyUnitsMapHacked();
    }
  }

  if (!std::isfinite(frameReward)) {
    if (isFinal) {
      throw std::runtime_error("The reward of current episode is nan");
    } else {
      return;
    }
  }

  if (auto tracer = state->board()->getTraceDumper()) {
    if (!test && FLAGS_train_on_baseline_rewards) {
      tracer->dumpGameValue(state, "baseline reward", baselineReward);
    }
    tracer->dumpGameValue(state, "game reward", frameReward);
  }

  if (handle) {
    std::shared_ptr<cpid::ReplayBufferFrame> frame;
    if (lastFeatures_ && lastModelOut_) {
      frame = trainer->makeFrame(
          lastModelOut_.value(), lastFeatures_.value(), frameReward);
    } else if (!setup->trainerTakesPreviousActionAndState) {
      frame = trainer->makeFrame({}, {}, frameReward);
    }

    if (frame) {
      trainer->step(handle, std::move(frame), isFinal);
    }
    lastFeatures_.reset();
    lastModelOut_.reset();
  }
}

void MicroModule::onGameEnd(State* state) {
  episodeEndFrame = episodeCurrentFrame(state);
  trainerStep(state, true);
  updateHeatMapToVisdom();
}
} // namespace microbattles
