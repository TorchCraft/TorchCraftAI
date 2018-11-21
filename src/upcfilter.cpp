/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "upcfilter.h"

#include "state.h"
#include "utils.h"

#include <algorithm>

namespace cherrypi {

bool AssignedUnitsFilter::filter(
    State* state,
    std::shared_ptr<UPCTuple> upc,
    Module* origin) {
  auto board = state->board();

  if (upc->commandProb(Command::Gather) == 1) {
    // This is a gather UPC. We'll set any units that are currently assigned to
    // a task to zero probability, provided that the origin of this UPC is not
    // the origin of that task.
    for (auto it : upc->unit) {
      if (it.second <= 0) {
        continue;
      }
      auto td = board->taskDataWithUnit(it.first);
      if (td.task == nullptr || td.owner == origin) {
        continue;
      }

      // Remove unit
      upc->unit[it.first] = 0;
      VLOG(1) << "Removed unit " << utils::unitString(it.first)
              << " from gather UPC since it is already assigned";
    }
  }

  return true;
};

namespace {
template <typename T>
bool fixProba(
    T& map,
    std::function<std::string(decltype(map.begin()->first))> printer) {
  for (auto& u : map) {
    if (u.second < 0 || u.second > 1) {
      LOG(WARNING) << "Probability value for " << printer(u.first)
                   << " is invalid (" << u.second << "). Clamping to [0,1]";
      u.second = std::max(0.f, std::min(1.f, u.second));
    } else if (std::isnan(u.second)) {
      LOG(WARNING) << "Probability value for " << printer(u.first)
                   << " is invalid (" << u.second << "). Can't fix!";
      return false;
    }
  }
  return true;
}
}; // namespace

bool SanityFilter::filter(
    State* state,
    std::shared_ptr<UPCTuple> upc,
    Module* origin) {
  if (upc->unit.count(nullptr) != 0) {
    LOG(WARNING) << "Removed null ptr unit from UPC";
    upc->unit.erase(nullptr);
  }

  if (upc->position.is<UPCTuple::UnitMap>()) {
    auto& map = upc->position.get_unchecked<UPCTuple::UnitMap>();
    if (map.count(nullptr) != 0) {
      LOG(WARNING) << "Remove nullptr unit(s) from UPCTuple::position";
      map.erase(nullptr);
      if (map.empty()) {
        upc->position = UPCTuple::Empty();
      }
    }
  }

  if (upc->state.is<UPCTuple::BuildTypeMap>()) {
    auto& map = upc->state.get_unchecked<UPCTuple::BuildTypeMap>();
    if (map.count(nullptr) != 0) {
      LOG(WARNING) << "Remove nullptr unit(s) from UPCTuple::state";
      map.erase(nullptr);
      if (map.empty()) {
        upc->state = UPCTuple::Empty();
      }
    }
  }

  // check various probabilities
  bool valid = true;
  valid &= fixProba(upc->unit, [](Unit* u) {
    return std::string("unit ") + utils::unitString(u);
  });
  if (upc->position.is<UPCTuple::UnitMap>()) {
    valid &=
        fixProba(upc->position.get_unchecked<UPCTuple::UnitMap>(), [](Unit* u) {
          return std::string("unit position") + utils::unitString(u);
        });
  }
  valid &= fixProba(upc->command, [](Command c) {
    return std::string("Command ") + utils::commandString(c);
  });
  if (upc->state.is<UPCTuple::BuildTypeMap>()) {
    valid &= fixProba(
        upc->state.get_unchecked<UPCTuple::BuildTypeMap>(),
        [](BuildType const* b) {
          return std::string("build type ") + utils::buildTypeString(b);
        });
  }

  if (!valid) {
    LOG(WARNING) << "Unable to fix UPC probabilities, dropping";
    return false;
  }
  return true;
};

} // namespace cherrypi
