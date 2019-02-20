/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <lest.hpp>
#include <list>

#include "buildtype.h"
#include "module.h"
#include "state.h"
#include "task.h"
#include "upc.h"

namespace cherrypi {
namespace test {

class BuildOrderFixedModule : public Module {
 public:
  BuildOrderFixedModule(
      lest::env& lestEnv,
      const std::list<const BuildType*>& buildOrder)
      : lestEnv_(lestEnv), buildOrder_(buildOrder) {}
  virtual ~BuildOrderFixedModule() = default;

  virtual void step(State* state) override;
  std::list<const BuildType*> scheduledActions() const;
  std::list<const BuildType*> completedActions() const;

 private:
  struct Action {
    std::shared_ptr<UPCTuple> upc;
    std::shared_ptr<ProxyTask> progressTracker;
    const BuildType* buildType;
  };

  bool consumeTriggerUpc(State* state, int* triggerUpcId);
  void updateScheduledActions(State* state);
  void checkAndSubmitActions(State* state);
  void postActionsOnBlackboard(int triggerUpcId, State* state);
  bool enoughResources(
      cherrypi::State* state,
      const cherrypi::BuildType* buildType);
  bool checkPrerequisites(
      cherrypi::State* state,
      const cherrypi::BuildType* buildType);

  lest::env& lestEnv_;
  cherrypi::FrameNum lastOrderFrame_ = -1;
  std::list<const BuildType*> buildOrder_;
  std::list<Action> scheduledActionsNotCompleted_;
  std::list<const BuildType*> completedActions_;
};

} // namespace test
} // namespace cherrypi
