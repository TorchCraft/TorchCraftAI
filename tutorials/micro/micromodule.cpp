/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "micromodule.h"

#include "common/autograd.h"
#include "cpid/batcher.h"
#include "cpid/distributed.h"
#include "cpid/estrainer.h"
#include "cpid/evaluator.h"
#include "cpid/sampler.h"
#include "cpid/zeroordertrainer.h"

#include "utils.h"

#include "common.h"
#include "flags.h"
#include "model.h"

using namespace cherrypi;

namespace microbattles {

MicroModule::MicroModule(
    unsigned int threadId,
    std::shared_ptr<TrainingSetup> training,
    std::shared_ptr<cpid::Trainer> trainer,
    std::function<std::unique_ptr<Reward>()>& reward)
    : Module(),
      threadId_(threadId),
      training_(training),
      trainer_(trainer),
      reward_(reward()) {
  setName("MicroLearner");
  featurizer_ = training->model->getFeaturizer();
}

namespace {
std::vector<std::pair<Position, Position>> redline_;
std::vector<std::pair<Position, Position>> whiteline_;
} // namespace

void MicroModule::act(State* state) {
  if (threadId_ == 0) {
    redline_.clear();
    whiteline_.clear();
  }

  auto stateTensor = featurizer_->featurize(state);
  std::vector<torch::Tensor> stateVarList;
  for (auto& t : stateTensor) {
    stateVarList.push_back(t.to(defaultDevice()));
  }

  // Perform batch forward pass and assign all actions
  torch::NoGradGuard guard; // Disable grads during eval
  auto modelOut =
      trainer_->forward(ag::Variant(stateVarList), gameUID_).getTensorList();

  auto actions = [&]() {
    if (training_->trainer->is<cpid::ESTrainer>()) {
      return std::dynamic_pointer_cast<MicroModel>(training_->model)
          ->decodeOutput(state, stateVarList, modelOut);
    } else {
      throw std::runtime_error("Can not decode output of this trainer");
    }
  }();
  for (auto& action : actions) {
    auto upc = [&]() -> std::shared_ptr<UPCTuple> {
      if (action.action != MicroModel::MicroAction::Attack) {
        attacks_[action.unit] = nullptr;
      }
      switch (action.action) {
        case MicroModel::MicroAction::Attack:
          if (threadId_ == 0) {
            redline_.push_back(
                {Position{action.unit}, Position{action.target_u}});
          }
          if (attacks_[action.unit] != action.target_u) {
            attacks_[action.unit] = action.target_u;
            return utils::makeSharpUPC(
                action.unit, action.target_u, cp::Command::Delete);
          } else {
            return nullptr;
          }
        case MicroModel::MicroAction::Move:
          if (threadId_ == 0) {
            whiteline_.push_back(
                {Position{action.unit}, Position{action.target_p}});
          }
          return cp::utils::makeSharpUPC(
              action.unit, action.target_p, cp::Command::Move);
        case MicroModel::MicroAction::None:
          return cp::utils::makeSharpUPC(
              action.unit, action.unit, cp::Command::Move);
        default:
          throw std::runtime_error(
              fmt::format(
                  "Didn't have a handler for MicroAction {}", action.action));
      }
    }();

    if (upc != nullptr) {
      state->board()->postUPC(std::move(upc), kRootUpcId, this);
    }
  }

  reward_->stepReward(state);
  frameReward_ = reward_->reward;
  std::shared_ptr<cpid::ReplayBufferFrame> frame;
  if (frame != nullptr) {
    trainer_->step(gameUID_, frame, false);
  }
}

void MicroModule::step(State* state) {
  if (!started_) {
    return;
  }
  try {
    if (reward_->terminate(state)) {
      doLastFrame(state);
      return;
    }
    if (threadId_ == 0 && FLAGS_illustrate) {
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
      for (auto& pair : redline_)
        utils::drawLine(state, pair.first, pair.second, tc::BW::Color::Red);
      for (auto& pair : whiteline_)
        utils::drawLine(state, pair.first, pair.second, tc::BW::Color::White);
      auto rewardString = fmt::format("Reward: {}", reward_->reward);
      utils::drawTextScreen(state, 0, 0, rewardString);
      VLOG(3) << rewardString;
    }
    if (currentFrame_ % FLAGS_frame_skip == 0) {
      act(state);
    }
  } catch (std::exception const& trainingException) {
    VLOG(0) << "Caught exception in MicroModule: " << trainingException.what();
  } catch (...) {
    VLOG(0) << "Caught unknown exception in MicroModule";
  }
  currentFrame_++;
}

void MicroModule::onGameStart(State* state) {
  gameUID_ = cpid::genGameUID(cpid::distributed::globalContext()->rank);
  // Register this game as a new episode in the trainer.
  // startEpisode() will return true if we're good to go.
  while (!trainer_->startEpisode(gameUID_)) {
    if (trainer_->isDone()) {
      throw std::runtime_error(fmt::format("{} trainer is done", gameUID_));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    continue;
  }
  // Store the current episode information in an EpisodeHandle. If the handle
  // goes out of scope and the episode is not finished yet, the destructor
  // will call `trainer->forceStopEpisode()`.
  episode_ = std::make_unique<cpid::EpisodeHandle>(trainer_, gameUID_);

  reward_->begin(state);
  currentFrame_ = 0;
  started_ = true;
  std::tie(lastAllyCount_, lastEnemyCount_, lastAllyHp_, lastEnemyHp_) =
      getUnitCountsHealth(state);
  std::tie(firstAllyCount_, firstEnemyCount_, firstAllyHp_, firstEnemyHp_) =
      getUnitCountsHealth(state);
}

void MicroModule::doLastFrame(State* state) {
  if (!started_) {
    return;
  }
  if (!aborted_) {
    reward_->stepReward(state);
    frameReward_ = reward_->reward;
    if (!FLAGS_evaluate) {
      trainer_->step(
          gameUID_,
          std::make_shared<cpid::RewardBufferFrame>(frameReward_),
          true);
    }
  }
  episode_ = nullptr;
  started_ = false;
  aborted_ = false;
}

void MicroModule::onGameEnd(State* state) {
  doLastFrame(state);
}
} // namespace microbattles
