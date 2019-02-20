/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "cherrypi.h"
#include "module.h"
#include "state.h"

#include <deque>
#include <list>
#include <unordered_map>
#include <vector>

namespace cherrypi {

/**
 * Shared data among all builder controllers.
 */
struct BuilderControllerData {
  tc::Resources res;

  int lastIncomeHistoryUpdate = 0;
  std::deque<int> mineralsHistory;
  std::deque<int> gasHistory;
  double currentMineralsPerFrame = 0.0;
  double currentGasPerFrame = 0.0;

  std::unordered_map<Unit*, std::tuple<int, const BuildType*, Position>>
      recentAssignedBuilders;
};

/**
 * A general-purpose unit production module.
 *
 * This module consumes a Create UPC with a sharp createType and attempts to
 * create it. Units are optional; by default, an appropriate and not-so-busy
 * worker or producer will be selected. Positions are required for buildings
 * that need to be placed by a worker unit.
 *
 * A build task will be created for every UPC consumed, regardless of current
 * resources, and it will continually be attempted to be built. The exception is
 * that tasks fail if a building that needed to be placed by a worker unit was
 * requested but the desired build location is no longer valid.  Units will be
 * created in the order UPCs are consumed, unless we have spare resources which
 * may allow later UPCs to be fulfilled first.
 */
class BuilderModule : public Module {
 public:
  BuilderModule() {}
  virtual ~BuilderModule() = default;

  virtual void step(State* s) override;

  // TODO: Get rid of remaining module state.
  std::shared_ptr<BuilderControllerData> bcdata_;
};

} // namespace cherrypi
