/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "autobuild.h"
#include "cherrypi.h"

namespace cherrypi {

/**
 * Loads and uses a build order from the blackboard.
 */
class GenericAutoBuildModule : public AutoBuildModule {
 public:
  virtual ~GenericAutoBuildModule() = default;

  virtual std::shared_ptr<AutoBuildTask> createTask(
      State* state,
      int srcUpcId,
      std::shared_ptr<UPCTuple> srcUpc) override;

  bool switchToBuildOrder(State* state, std::string name);

 private:
  std::string activeBuild_;
};

} // namespace cherrypi
