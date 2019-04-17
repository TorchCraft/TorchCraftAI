/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "buildorderfixed.h"
#include "utils.h"

namespace {
// delay high enough to consider build order as stalled
// Note: current build order techs can take a very long time
// to be researched due to the lack of resources on the map,
// disabling the check for now through setting the max delay to 0
constexpr cherrypi::FrameNum kMaxBuildOrderDelayFrames = 0;
} // namespace

namespace cherrypi {
namespace test {

void BuildOrderFixedModule::step(State* state) {
  int triggerUpcId;
  if (!consumeTriggerUpc(state, &triggerUpcId)) {
    return;
  }
  updateScheduledActions(state);
  checkAndSubmitActions(state);
  postActionsOnBlackboard(triggerUpcId, state);
}

std::list<const BuildType*> BuildOrderFixedModule::scheduledActions() const {
  std::list<const BuildType*> scheduled;
  for (const auto& action : scheduledActionsNotCompleted_) {
    scheduled.push_back(action.buildType);
  }
  return scheduled;
}

std::list<const BuildType*> BuildOrderFixedModule::completedActions() const {
  return completedActions_;
}

bool BuildOrderFixedModule::consumeTriggerUpc(State* state, int* triggerUpcId) {
  Blackboard* board = state->board();
  bool found = false;
  for (auto& upcs : board->upcsWithSharpCommand(Command::Create)) {
    if (upcs.second->state.is<UPCTuple::Empty>()) {
      *triggerUpcId = upcs.first;
      found = true;
      break;
    }
  }
  if (found) {
    board->consumeUPC(*triggerUpcId, this);
  }
  return found;
}

void BuildOrderFixedModule::updateScheduledActions(State* state) {
  for (auto action_it = scheduledActionsNotCompleted_.begin();
       action_it != scheduledActionsNotCompleted_.end();) {
    action_it->progressTracker->update(state);
    switch (action_it->progressTracker->status()) {
      case TaskStatus::Ongoing:
        VLOG(2) << "action submitted and is in progress: "
                << "action = " << utils::buildTypeString(action_it->buildType);
        ++action_it;
        break;
      case TaskStatus::Success:
        VLOG(1) << "action succeeded: "
                << "action = " << utils::buildTypeString(action_it->buildType);
        completedActions_.push_back(action_it->buildType);
        action_it = scheduledActionsNotCompleted_.erase(action_it);
        break;
      case TaskStatus::Failure:
        VLOG(1) << "action failed: "
                << "action = " << utils::buildTypeString(action_it->buildType);
        // setting upc to null will regenerate UPC and resubmit the action
        action_it->upc.reset();
        ++action_it;
        break;
      case TaskStatus::Unknown:
        VLOG(2) << "action progress could not be evaluated: "
                << "action = " << utils::buildTypeString(action_it->buildType);
        ++action_it;
        break;
      default:
        VLOG(2) << "unknown task status: "
                << "action = " << utils::buildTypeString(action_it->buildType);
        ++action_it;
        break;
    }
  }
}

void BuildOrderFixedModule::checkAndSubmitActions(State* state) {
  lest::env& lest_env = lestEnv_;
  if (!buildOrder_.empty()) {
    const BuildType* nxtBuildType = buildOrder_.front();
    VLOG(1) << "next build order " << utils::buildTypeString(nxtBuildType)
            << ", current frame = " << state->currentFrame()
            << ", last order frame = " << lastOrderFrame_;
    if ((kMaxBuildOrderDelayFrames > 0) && (lastOrderFrame_ >= 0)) {
      EXPECT(
          state->currentFrame() - lastOrderFrame_ < kMaxBuildOrderDelayFrames);
    }
    if (enoughResources(state, nxtBuildType) &&
        utils::prerequisitesReady(state, nxtBuildType)) {
      Action action;
      action.buildType = nxtBuildType;
      scheduledActionsNotCompleted_.push_back(action);
      buildOrder_.pop_front();
      lastOrderFrame_ = state->currentFrame();
    }
  }
}

void BuildOrderFixedModule::postActionsOnBlackboard(
    int triggerUpcId,
    State* state) {
  Blackboard* board = state->board();
  UnitsInfo& unitsInfo = state->unitsInfo();
  for (auto& action : scheduledActionsNotCompleted_) {
    if (!action.upc) {
      action.upc = std::make_shared<UPCTuple>();
      auto buildType = action.buildType;
      if (!buildType) {
        LOG(ERROR) << "action build type is null";
        continue;
      }
      auto builderBuildType = buildType->builder;
      if (!builderBuildType) {
        LOG(ERROR) << "action builder build type is null for action "
                   << utils::buildTypeString(buildType);
        continue;
      }
      const auto& potentialBuilders =
          unitsInfo.myCompletedUnitsOfType(builderBuildType);
      const float unitWeight = std::min(1.0f / potentialBuilders.size(), 0.5f);
      for (auto* punit : potentialBuilders) {
        action.upc->unit[punit] = unitWeight;
      }
      action.upc->scale = 1;
      action.upc->command[Command::Create] = 1;
      action.upc->state = UPCTuple::BuildTypeMap{{buildType, 1}};

      auto upcId = board->postUPC(std::move(action.upc), triggerUpcId, this);
      action.upc = board->upcWithId(upcId);
      action.progressTracker = std::make_shared<ProxyTask>(upcId, triggerUpcId);
    }
  }
}

bool BuildOrderFixedModule::enoughResources(
    cherrypi::State* state,
    const cherrypi::BuildType* buildType) {
  auto resources = state->resources();
  return (buildType->mineralCost <= resources.ore) &&
      (buildType->gasCost <= resources.gas) &&
      (buildType->supplyRequired <= resources.total_psi - resources.used_psi);
}

bool BuildOrderFixedModule::checkPrerequisites(
    cherrypi::State* state,
    const cherrypi::BuildType* buildType) {
  const auto& prerequisites = buildType->prerequisites;
  for (auto prerequisite : prerequisites) {
    if (!prerequisite) {
      LOG(ERROR) << "prerequisite is null for action "
                 << utils::buildTypeString(buildType);
      continue;
    }
    if (prerequisite->isUnit()) {
      const auto& units =
          state->unitsInfo().myCompletedUnitsOfType(prerequisite);
      if (units.empty()) {
        return false;
      }
    } else if (prerequisite->isUpgrade()) {
      if (state->getUpgradeLevel(prerequisite) < prerequisite->level) {
        return false;
      }
    } else if (prerequisite->isTech()) {
      if (!state->hasResearched(buildType)) {
        return false;
      }
    }
  }
  return true;
}

} // namespace test
} // namespace cherrypi
