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

#include <visdom/visdom.h>

using namespace cherrypi;

namespace microbattles {

DEFINE_double(draw_penalty, 5.0, "Negative penalty for scenario ending without a winner");

MicroModule::MicroModule(
    std::shared_ptr<TrainingSetup> training,
    std::shared_ptr<cpid::Trainer> trainer,
    std::unique_ptr<cherrypi::Reward>&& reward)
    : Module(),
      setup_(training),
      trainer_(trainer),
      reward_(std::move(reward)) {
  setName("MicroLearner");
  featurizer_ = setup_->model->getFeaturizer();
}

void MicroModule::step(State* state) {
  if (!started_ || !handle_) {
    return;
  }

  try {
    if (reward_->terminate(state)) {
      trainerStep(state, true);
      return;
    }

    if (currentFrame_ % FLAGS_plot_every == 0) {
      updateHeatMapToVisdom();
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
  currentFrame_++;
}

void MicroModule::forward(State* state) {
  lines_.clear();
  actionPerUnit_.clear();
  torch::NoGradGuard g_;
  auto stateTensor = featurizer_->featurize(state);
  plotHeatmaps(state, stateTensor);
  stateTensor = common::applyTransform(stateTensor, [](torch::Tensor const& t) {
    return t.to(defaultDevice());
  });

  // Perform batch forward pass and assign all actions
  auto modelOut = trainer_->forward(stateTensor, handle_);
  modelOut = trainer_->sample(modelOut);
  plotHeatmaps(state, modelOut);

  auto actions = std::dynamic_pointer_cast<PFMicroActionModel>(setup_->model)
                     ->decodeOutput(state, stateTensor, modelOut);

  if (setup_->trainerTakesPreviousActionAndState) {
    trainerStep(state, false);
    lastFeatures_ = stateTensor;
    lastModelOut_ = modelOut;
  } else {
    lastFeatures_ = stateTensor;
    lastModelOut_ = modelOut;
    trainerStep(state, false);
  }
  if (auto tracer = state->board()->getTraceDumper()) {
    if (setup_->modelProvidesValueKey) {
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
    tracer->dumpGameValue(state, "frame reward", frameReward_);
  }

  for (auto& action : actions) {
    auto upc = actionToUPC(action);
    if (upc != nullptr) {
      actionPerUnit_[action.unit] = MicroAction();
      actionPerUnit_[action.unit].upc = upc;
      actionPerUnit_[action.unit].isFinal = true;
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
  if (currentFrame_ == 0 || currentFrame_ % FLAGS_frame_skip != 0) {
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
    PFMicroActionModel::PFMicroAction& action) {
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
    case PFMicroActionModel::PFMicroAction::AttackMove:
      addLine(action.unit, Position{action.target_p}, tc::BW::Color::Red);
      // XXX guard against spamming same command like attack?
      return utils::makeSharpUPC(
          action.unit, action.target_p, cp::Command::Delete);
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
      numericMetricsByUnit_["darkSwarmNum"][action.unit->id]++;
      return cp::utils::makeSharpUPC(
          action.unit,
          action.target_p,
          cp::Command::Cast,
          buildtypes::Dark_Swarm);
    case PFMicroActionModel::PFMicroAction::Plague:
      if (action.unit->unit.energy < 150) {
        return nullptr;
      }
      addLine(action.unit, Position{action.target_u}, tc::BW::Color::Yellow);
      numericMetricsByUnit_["plagueNum"][action.unit->id]++;
      return cp::utils::makeSharpUPC(
          action.unit, action.target_p, cp::Command::Cast, buildtypes::Plague);
    default:
      throw std::runtime_error(fmt::format(
          "Didn't have a handler for PFMicroAction {}", action.action));
  }
}

void MicroModule::plotHeatmaps(cp::State* state, ag::Variant output, int downsample) {
  auto cvis = state->board()->getTraceDumper();
  if (!output.isDict() || output.getDict().count("heatmaps") == 0) {
    return;
  }

  auto const& heatmaps = output.getDict().at("heatmaps").getDict();
  if (generateHeatmaps_) {
    for (auto const& p : heatmaps) {
      auto tensorCpu = p.second.get().to(at::kCPU);
      LOG_IF(FATAL, tensorCpu.dim() != 2)
          << "Heatmap " << p.first << " is not 2D. Shape is "
          << tensorCpu.sizes();
      heatmap_[p.first] = tensorCpu;
    }
  }
  if (cvis) {
    cvis->dumpTerrainHeatmaps(
        state,
        heatmaps,
        {0, 0},
        {tc::BW::XYPixelsPerBuildtile*downsample, tc::BW::XYPixelsPerBuildtile*downsample});
  }
}

void MicroModule::updateHeatMapToVisdom() {
  auto displayHeatmap = [&](torch::Tensor map, std::string const& name) {
    map = map.clone().to(at::kFloat);
    map = map.masked_fill_(map == kfInfty, -1);
    if (setup_->vs) {
      setup_->vs->heatmap(
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
  for (auto& c : circles_) {
    if (c.unit) {
      utils::drawCircle(state, c.unit, c.r, c.color);
    } else {
      utils::drawCircle(state, c.p, c.r, c.color);
    }
  }
  auto rewardString = fmt::format("Reward: {}", reward_->reward);
  utils::drawTextScreen(state, 0, 0, rewardString);
  VLOG(3) << rewardString;
}

void MicroModule::onGameStart(State* state) {
  reward_->begin(state);
  lastFeatures_.reset();
  lastModelOut_.reset();
  currentFrame_ = 0;
  started_ = true;
  totalReward_ = 0;
  std::tie(lastAllyCount_, lastEnemyCount_, lastAllyHp_, lastEnemyHp_) =
      getUnitCountsHealth(state);
  std::tie(firstAllyCount_, firstEnemyCount_, firstAllyHp_, firstEnemyHp_) =
      getUnitCountsHealth(state);
}

void MicroModule::trainerStep(cp::State* state, bool isFinal) {
  if (isFinal) {
    started_ = false;
  }

  if (isFinal) {
    if (auto tracer = state->board()->getTraceDumper()) {
      CVIS_LOG(state) << "Final state reward: " << frameReward_;
      CVIS_LOG(state) << "Units left: " << state->unitsInfo().myUnits().size();
      CVIS_LOG(state) << "Enemy left: "
                      << state->unitsInfo().enemyUnits().size();
    }
  }
  if (isFinal && state->unitsInfo().myUnits().size() > 0 && state->unitsInfo().enemyUnits().size() > 0) {
    reward_->stepDrawReward(state);
  }
  else if (!FLAGS_sparse_reward || isFinal) {
    reward_->stepReward(state);
  }
  frameReward_ = reward_->reward * FLAGS_reward_scale;
  if (FLAGS_sparse_reward && isFinal) {
    frameReward_ = frameReward_ - FLAGS_time_penalty * (currentFrame_ / FLAGS_frame_skip);
  } else {
    frameReward_ = frameReward_ - FLAGS_time_penalty;
  }
  totalReward_ += frameReward_;
  VLOG(3) << "reward " << frameReward_ << ", terminal " << isFinal;

  if (handle_) {
    std::shared_ptr<cpid::ReplayBufferFrame> frame;
    if (lastFeatures_ && lastModelOut_) {
      frame = trainer_->makeFrame(
          lastModelOut_.value(), lastFeatures_.value(), frameReward_);
    } else if (!setup_->trainerTakesPreviousActionAndState) {
      frame = trainer_->makeFrame({}, {}, frameReward_);
    }

    if (frame) {
      trainer_->step(handle_, std::move(frame), isFinal);
    }
    lastFeatures_.reset();
    lastModelOut_.reset();
  }
}

void MicroModule::onGameEnd(State* state) {
  trainerStep(state, true);
}
} // namespace microbattles
