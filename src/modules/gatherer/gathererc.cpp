/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "modules/gatherer/gathererc.h"
#include "blackboard.h"
#include "movefilters.h"
#include "utils.h"

#include <bwem/map.h>
#include <common/rand.h>
#include <fmt/format.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

namespace cherrypi {

void GathererController::addUnit(State* state, Unit* unit, UpcId upcId) {
  VLOG(1) << fmt::format(
      "{} starts gathering via {}.",
      utils::unitString(unit),
      utils::upcString(upcId));
  assignments.addUnit(unit);
  SharedController::addUnit(state, unit, upcId);
}

void GathererController::removeUnit(State* state, Unit* unit, UpcId upcId) {
  VLOG(1) << fmt::format("{} stops gathering.", utils::unitString(unit));
  assignments.removeUnit(unit);
  SharedController::removeUnit(state, unit, upcId);
}

bool GathererController::keepUnit(State* state, Unit* unit) const {
  if (unit->type->isWorker) {
    VLOG(5) << fmt::format("Gatherer keeps {}", utils::unitString(unit));
    return SharedController::keepUnit(state, unit);
  }
  VLOG(3) << fmt::format("Gatherer rejects {}", utils::unitString(unit));
  return false;
}

} // namespace cherrypi
