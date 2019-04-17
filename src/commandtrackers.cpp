/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "commandtrackers.h"

#include "buildtype.h"
#include "state.h"
#include "unitsinfo.h"
#include "utils.h"

namespace cherrypi {

namespace {
auto const& ustr = utils::unitString;
} // namespace

MovementTracker::MovementTracker(
    std::unordered_set<Unit*> units,
    int targetX,
    int targetY,
    float mind,
    int timeout)
    : Tracker(timeout),
      units_(std::move(units)),
      target_(targetX, targetY),
      mind_(mind) {
  status_ = TrackerStatus::Pending;
}

bool MovementTracker::updatePending(State* s) {
  // Remove dead units
  for (auto it = units_.begin(); it != units_.end();) {
    if ((*it)->dead) {
      VLOG(3) << "MovementTracker, unit " << ustr(*it) << " dead";
      units_.erase(it++);
    } else {
      ++it;
    }
  }
  if (units_.empty()) {
    status_ = TrackerStatus::Failure;
    VLOG(1) << "MovementTracker, no units, pending -> failure";
    return true;
  }

  for (auto unit : units_) {
    if (unit->unit.velocityX != 0 || unit->unit.velocityY != 0) {
      status_ = TrackerStatus::Ongoing;
      VLOG(1) << "MovementTracker, unit " << ustr(unit)
              << " has positive velocity (" << unit->unit.velocityX << ", "
              << unit->unit.velocityY << "), status -> ongoing";
      return true;
    }
  }
  return false;
}

bool MovementTracker::updateOngoing(State* s) {
  // Remove dead units
  for (auto it = units_.begin(); it != units_.end();) {
    if ((*it)->dead) {
      VLOG(3) << "MovementTracker, unit " << ustr(*it) << " dead";
      units_.erase(it++);
    } else {
      ++it;
    }
  }
  if (units_.empty()) {
    status_ = TrackerStatus::Failure;
    VLOG(1) << "MovementTracker, no units, ongoing -> failure";
    return true;
  }

  auto center = utils::centerOfUnits(units_);
  VLOG(4) << "distance = " << center.distanceTo(target_);
  if (center.distanceTo(target_) < mind_) {
    status_ = TrackerStatus::Success;
    if (VLOG_IS_ON(2)) {
      std::ostringstream oss;
      for (auto* unit : units_) {
        oss << ustr(unit) << ", ";
      }
      VLOG(2) << "MovementTracker, units " << oss.str() << "arrived -> success";
    }
    return true;
  }

  bool foundMoving = false;
  for (auto unit : units_) {
    if (unit->unit.velocityX != 0 && unit->unit.velocityY != 0) {
      foundMoving = true;
      break;
    }
  }
  if (!foundMoving) {
    time_ = 0;
    status_ = TrackerStatus::Pending;
    VLOG(1) << "MovementTracker, no units moving, ongoing -> pending";
    return true;
  }

  return false;
}

BuildTracker::BuildTracker(Unit* unit, BuildType const* type, int timeout)
    : Tracker(timeout), unit_(unit), type_(type) {
  status_ = TrackerStatus::Pending;
}

bool BuildTracker::updatePending(State* state) {
  if (startedPendingFrame_ < 0) {
    startedPendingFrame_ = state->currentFrame();
  }

  if (unit_->dead) {
    if (unit_->type != buildtypes::Zerg_Drone) {
      status_ = TrackerStatus::Failure;
      VLOG(1) << "BuildTracker, " << ustr(unit_) << " died -> failure";
      return true;
    } else if (state->currentFrame() > unit_->lastSeen + kMorphTimeout) {
      status_ = TrackerStatus::Failure;
      VLOG(1) << "BuildTracker, " << ustr(unit_)
              << " died and timeout reached for moprhing -> failure";
      return true;
    } else {
      findTargetForDrone(state);
      if (target_ != nullptr) {
        startedPendingFrame_ = -1;
        status_ = TrackerStatus::Ongoing;
        VLOG(1) << "BuildTracker, " << ustr(unit_)
                << " died, target=" << ustr(target_) << ", status -> ongoing";
        return true;
      }
      VLOG(2) << "BuildTracker, " << ustr(unit_)
              << " died, target not found, status unchanged";
      return false;
    }
  }

  // Check if build command has been picked up
  auto ctype = +tc::BW::UnitCommandType::Build;
  if (unit_->morphing() && !unit_->type->isBuilding) {
    ctype = +tc::BW::UnitCommandType::Morph;
  } else if (type_->isAddon) {
    ctype = +tc::BW::UnitCommandType::Build_Addon;
  } else if (!type_->isBuilding) {
    ctype = +tc::BW::UnitCommandType::Train;
  }
  if (utils::isExecutingCommand(unit_, ctype)) {
    VLOG(3) << ustr(unit_) << " started " << ctype._to_string();
    findTarget(state);
    if (target_ != nullptr) {
      startedPendingFrame_ = -1;
      status_ = TrackerStatus::Ongoing;
      VLOG(1) << "BuildTracker, " << ustr(unit_) << "target=" << ustr(target_)
              << ", status -> ongoing";
      return true;
    } else if (VLOG_IS_ON(3)) {
      std::ostringstream oss;
      for (auto order : unit_->unit.orders) {
        auto ov = tc::BW::Order::_from_integral_nothrow(order.type);
        oss << "(frame=" << order.first_frame
            << ", type=" << (ov ? ov->_to_string() : "???")
            << ", targetId=" << order.targetId << ", targetX=" << order.targetX
            << ", targetY=" << order.targetY << ") ";
      }
      VLOG(3) << "BuildTracker, " << ustr(unit_)
              << " did not find target yet, orders are " << oss.str();
    }
  } else {
    // If the unit is not executing the build command after 6 frames, something
    // has gone wrong.
    if (state->currentFrame() - startedPendingFrame_ > kNotBuildingTimeout) {
      std::ostringstream oss;
      for (auto order : unit_->unit.orders) {
        auto ov = tc::BW::Order::_from_integral_nothrow(order.type);
        oss << "(frame=" << order.first_frame
            << ", type=" << (ov ? ov->_to_string() : "???")
            << ", targetId=" << order.targetId << ", targetX=" << order.targetX
            << ", targetY=" << order.targetY << ") ";
      }

      VLOG(1) << "BuildTracker, " << ustr(unit_) << "target=" << ustr(target_)
              << ", pending and not executing order for "
              << state->currentFrame() - startedPendingFrame_
              << " frames, status -> timeout";
      VLOG(1) << "BuildTracker, " << ustr(unit_) << "target=" << ustr(target_)
              << ", orders are " << oss.str();
      status_ = TrackerStatus::Timeout;
    }
  }

  return false;
}

bool BuildTracker::updateOngoing(State* state) {
  auto board = state->board();

  // drone dies when building a Zerg_Extractor, take into account
  bool const unitDeadNotBuildingExtractor =
      unit_->dead && (type_ != buildtypes::Zerg_Extractor);
  bool const unitTypeIncoherent = (unit_->type != type_->builder) &&
      (unit_->type != type_) &&
      (!unit_->morphing() || (unit_->constructingType != type_));
  if (unitDeadNotBuildingExtractor || !unit_->isMine || unitTypeIncoherent) {
    status_ = TrackerStatus::Failure;
    VLOG(1) << ustr(unit_) << " died -> failure";
    return true;
  }

  if (target_ == nullptr) {
    findTarget(state);
  }

  if (target_) {
    if (target_->completed()) {
      VLOG(1) << ustr(unit_) << " completed " << ustr(target_) << " -> success";
      status_ = TrackerStatus::Success;
      board->untrack(target_->id);
      return true;
    } else if (target_->dead) {
      VLOG(1) << "target " << ustr(target_) << " of " << ustr(unit_)
              << " died -> failure";
      status_ = TrackerStatus::Failure;
      board->untrack(target_->id);
      return true;
    }
  }

  auto ctype = +tc::BW::UnitCommandType::Build;
  if (unit_->morphing() && !unit_->type->isBuilding) {
    ctype = +tc::BW::UnitCommandType::Morph;
  } else if (type_->isAddon) {
    ctype = +tc::BW::UnitCommandType::Build_Addon;
  } else if (unit_->type->isBuilding && unit_ != target_) {
    ctype = +tc::BW::UnitCommandType::Train;
  }
  if (!utils::isExecutingCommand(unit_, ctype)) {
    status_ = TrackerStatus::Failure;
    VLOG(1) << ustr(unit_) << " not doing " << ctype._to_string()
            << " any more -> failure";
    return true;
  }

  return false;
}

void BuildTracker::findTargetForDrone(State* state) {
  for (auto* unit : state->unitsInfo().myUnitsOfType(type_)) {
    if (unit->beingConstructed() && unit->morphing() &&
        (utils::distance(unit_, unit) < kMorphDistanceThreshold)) {
      target_ = unit;
    }
  }
}

void BuildTracker::findTarget(State* state) {
  if (target_ != nullptr) {
    return;
  }

  if (unit_->morphing()) {
    if (unit_->type == type_ || unit_->constructingType == type_) {
      target_ = unit_;
      VLOG(1) << "Found target for " << ustr(unit_) << ": " << ustr(target_);
    }
  } else if (unit_->type->isWorker) {
    auto orderTypes = tc::BW::commandToOrders(+tc::BW::UnitCommandType::Build);
    for (auto order : unit_->unit.orders) {
      if (std::find(orderTypes.begin(), orderTypes.end(), order.type) !=
          orderTypes.end()) {
        auto target = state->unitsInfo().getUnit(order.targetId);
        if (target && target->type == type_) {
          target_ = target;
          VLOG(1) << "Found target for " << ustr(unit_) << ": "
                  << ustr(target_);
          break;
        }
      }
    }
  } else {
    auto board = state->board();
    for (auto& unit : state->unitsInfo().getNewUnits()) {
      if (unit->isMine && unit->type == type_ && !unit->completed() &&
          !board->isTracked(unit->id)) {
        target_ = unit;
        VLOG(1) << "Found target for " << ustr(unit_) << ": " << ustr(target_);
        // XXX This is an ugly hack right now. How to find out which unit is
        // produced where?
        board->track(unit->id);
        break;
      }
    }
  }
}

UpgradeTracker::UpgradeTracker(Unit* unit, BuildType const* type, int timeout)
    : Tracker(timeout), unit_(unit), type_(type) {
  status_ = TrackerStatus::Pending;
}

bool UpgradeTracker::updatePending(State* state) {
  if (unit_->dead) {
    status_ = TrackerStatus::Failure;
    VLOG(1) << ustr(unit_) << " died -> failure";
    return true;
  }
  if (unit_->upgrading() && (unit_->upgradingType == type_)) {
    status_ = TrackerStatus::Ongoing;
    return true;
  }
  if (utils::isExecutingCommand(unit_, +tc::BW::UnitCommandType::Upgrade)) {
    VLOG(3) << ustr(unit_) << " started upgrade";
    status_ = TrackerStatus::Ongoing;
    return true;
  }
  return false;
}

bool UpgradeTracker::updateOngoing(State* state) {
  if (unit_->dead) {
    status_ = TrackerStatus::Failure;
    VLOG(1) << ustr(unit_) << " died -> failure";
    return true;
  }
  // Check if upgrade is available already
  if (state->getUpgradeLevel(type_) == type_->level) {
    VLOG(3) << "Upgrade " << type_ << " complete";
    status_ = TrackerStatus::Success;
    return true;
  }
  if (unit_->upgrading() && (unit_->upgradingType == type_)) {
    return false;
  }
  if (!utils::isExecutingCommand(unit_, +tc::BW::UnitCommandType::Upgrade)) {
    status_ = TrackerStatus::Failure;
    VLOG(1) << ustr(unit_) << " not upgrading any more -> failure";
    return true;
  }
  return false;
}

ResearchTracker::ResearchTracker(Unit* unit, BuildType const* type, int timeout)
    : Tracker(timeout), unit_(unit), type_(type) {
  status_ = TrackerStatus::Pending;
}

bool ResearchTracker::updatePending(State* state) {
  if (unit_->dead) {
    status_ = TrackerStatus::Failure;
    VLOG(1) << ustr(unit_) << " died -> failure";
    return true;
  }
  if (unit_->researching() && (unit_->researchingType == type_)) {
    VLOG(3) << ustr(unit_) << " started research";
    status_ = TrackerStatus::Ongoing;
    return true;
  }
  if (utils::isExecutingCommand(unit_, +tc::BW::UnitCommandType::Research)) {
    VLOG(3) << ustr(unit_) << " started research";
    status_ = TrackerStatus::Ongoing;
    return true;
  }
  return false;
}

bool ResearchTracker::updateOngoing(State* state) {
  if (unit_->dead) {
    status_ = TrackerStatus::Failure;
    VLOG(1) << ustr(unit_) << " died -> failure";
    return true;
  }
  if (state->hasResearched(type_)) {
    VLOG(3) << "Technology " << type_ << " researched";
    status_ = TrackerStatus::Success;
    return true;
  }
  if (unit_->researching() && (unit_->researchingType == type_)) {
    return false;
  }
  if (!utils::isExecutingCommand(unit_, +tc::BW::UnitCommandType::Research)) {
    status_ = TrackerStatus::Failure;
    VLOG(1) << ustr(unit_) << " not researching any more -> failure";
    return true;
  }
  return false;
}

AttackTracker::AttackTracker(
    std::unordered_set<Unit*> units,
    std::unordered_set<Unit*> enemies,
    int timeout)
    : Tracker(timeout), units_(std::move(units)), enemies_(std::move(enemies)) {
  // Skip NotTracking since we have a clear set of units already
  status_ = TrackerStatus::Pending;
}

bool AttackTracker::updateNotTracking(State* state) {
  return false;
}

bool AttackTracker::updatePending(State* state) {
  updateEnemies();
  if (enemies_.empty()) {
    status_ = TrackerStatus::Success;
    return true;
  }
  if (units_.empty()) {
    status_ = TrackerStatus::Failure;
    return true;
  }

  // Check if any unit is firing any bullets to verify that we're indeed
  // attacking.
  // TODO This is rather crude maybe? And doesn't consider units without bullets
  // like Zealots and Zerglings.
  for (auto unit : units_) {
    if (unit->unit.groundCD > 0 || unit->unit.airCD > 0) {
      status_ = TrackerStatus::Ongoing;
      return true;
    }
  }
  return false;
}

bool AttackTracker::updateOngoing(State* state) {
  updateEnemies();
  if (enemies_.empty()) {
    status_ = TrackerStatus::Success;
    return true;
  }
  if (units_.empty()) {
    status_ = TrackerStatus::Failure;
    return true;
  }

  bool foundFiring = false;
  for (auto unit : units_) {
    if (unit->unit.groundCD > 0 || unit->unit.airCD > 0) {
      foundFiring = true;
      break;
    }
  }

  if (foundFiring) {
    time_ = 0;
    return false;
  } else {
    if (++time_ > timeout_) {
      status_ = TrackerStatus::Timeout;
      return true;
    }
  }
  return false;
}

void AttackTracker::updateEnemies() {
  for (auto it = enemies_.begin(); it != enemies_.end();) {
    if ((*it)->dead) {
      enemies_.erase(it++);
    } else {
      ++it;
    }
  }
}

} // namespace cherrypi
