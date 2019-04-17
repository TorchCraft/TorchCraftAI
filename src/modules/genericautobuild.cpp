/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "genericautobuild.h"
#include "cherrypi.h"
#include <algorithm>
#include <stdexcept>

#include "buildorders/base.h"

#include <gflags/gflags.h>

DECLARE_string(build);

namespace cherrypi {

REGISTER_SUBCLASS_0(Module, GenericAutoBuildModule);

std::shared_ptr<AutoBuildTask> GenericAutoBuildModule::createTask(
    State* state,
    int srcUpcId,
    std::shared_ptr<UPCTuple> srcUpc) {
  if (!srcUpc->state.is<std::string>()) {
    return nullptr;
  }
  std::string buildorder = srcUpc->state.get<std::string>();
  if (!switchToBuildOrder(state, buildorder)) {
    return nullptr;
  }
  return buildorders::createTask(srcUpcId, buildorder, state, this);
}

bool GenericAutoBuildModule::switchToBuildOrder(
    State* state,
    std::string name) {
  if (activeBuild_ == name) {
    return false;
  }
  if (activeBuild_.empty()) {
    VLOG(0) << "Running build " << name;
  } else {
    VLOG(0) << "Build switched from " << activeBuild_ << " to " << name;
  }
  auto board = state->board();
  board->post(Blackboard::kBuildOrderKey, name);
  activeBuild_ = name;

  // Cancel the current build order task. A new one will be created in
  // AutoBuildModule::checkForNewUPCs
  for (auto& task : state->board()->tasksOfModule(this)) {
    if (std::dynamic_pointer_cast<AutoBuildTask>(task)) {
      task->cancel(state);
    }
  }

  // Reset some blackboard keys that are used by the build orders. This helps
  // the transition because some builds depend on the default values.
  board->remove(Blackboard::kMinScoutFrameKey);
  board->remove("TacticsAttack");
  board->remove("GathererMinGasGatherers");
  board->remove("GathererMaxGasGatherers");
  return true;
}
} // namespace cherrypi
