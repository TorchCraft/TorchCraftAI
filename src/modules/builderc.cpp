/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "modules/builderc.h"
#include "modules/builderhelper.h"

#include "commandtrackers.h"
#include "movefilters.h"
#include "utils.h"

#include <glog/logging.h>

namespace cherrypi {

namespace {

auto const kMaxBuildAttempts = 3;

/// Computes bounding box distance of given unit from build location
int pxDistanceOfUnit(Unit* unit, BuildType const* type, Position const& pos) {
  return utils::pxDistanceBB(
      unit->unit.pixel_x - unit->type->dimensionLeft,
      unit->unit.pixel_y - unit->type->dimensionUp,
      unit->unit.pixel_x + unit->type->dimensionRight,
      unit->unit.pixel_y + unit->type->dimensionDown,
      pos.x * tc::BW::XYPixelsPerWalktile,
      pos.y * tc::BW::XYPixelsPerWalktile,
      (pos.x + type->tileWidth * tc::BW::XYWalktilesPerBuildtile) *
          tc::BW::XYPixelsPerWalktile,
      (pos.y + type->tileHeight * tc::BW::XYWalktilesPerBuildtile) *
          tc::BW::XYPixelsPerWalktile);
}

} // namespace

BuilderControllerBase::BuilderControllerBase(
    Module* module,
    BuildType const* type,
    std::unordered_map<Unit*, float> unitProbs,
    std::shared_ptr<BuilderControllerData> bcdata)
    : Controller(module),
      type_(type),
      unitProbs_(std::move(unitProbs)),
      bcdata_(std::move(bcdata)) {}

void BuilderControllerBase::grabUnit(State* state, Unit* unit) {
  auto* board = state->board();
  auto ctask =
      std::dynamic_pointer_cast<ControllerTask>(board->taskForId(upcId_));
  if (ctask == nullptr) {
    LOG(WARNING) << "No associated controller task? That's weird.";
    return;
  }
  if (ctask->finished()) {
    // Prevent spawning new tasks if the task is finished, since
    // it would keep the build task alive (important when tasks are cancelled).
    VLOG(1) << "Task " << utils::upcString(upcId_)
            << " is finished, cowardly refusing to grab another unit";
    return;
  }

  // We'll want to grab a builder or move a unit out of the way.  Take control
  // of unit by posting and consuming a UPC with it. This way we'll receive a
  // new UPC Id, which lets us bind the unit to a new (do-nothing) task.
  auto upc = std::make_shared<UPCTuple>();
  upc->unit[unit] = 1;
  upc->command[Command::Create] = 0.5;
  upc->command[Command::Move] = 0.5;
  auto newId = board->postUPC(std::move(upc), upcId_, module_);
  board->consumeUPC(newId, module_);
  // Instantiate another controller task so that unit management wrt controller
  // is unified.
  board->postTask(
      std::make_shared<ControllerTask>(
          newId, std::unordered_set<Unit*>{unit}, state, ctask->controller()),
      module_,
      true);

  Controller::addUnit(state, unit, newId);
  VLOG(1) << "BuilderControllerBase " << utils::upcString(upcId_)
          << ": took control of unit " << utils::unitString(unit) << " via "
          << utils::upcString(newId);
}

void BuilderControllerBase::releaseUnit(State* state, Unit* unit) {
  auto task = state->board()->taskWithUnitOfModule(unit, module_);
  if (task == nullptr) {
    auto taskData = state->board()->taskDataWithUnit(unit);
    auto has = units_.find(unit) != units_.end();
    VLOG(has ? 0 : 1) << "BuilderControllerBase " << utils::upcString(upcId_)
                      << ": cannot release unit " << utils::unitString(unit)
                      << ": not owned by our tasks but by "
                      << (taskData.owner ? taskData.owner->name() : "nobody")
                      << " and controller thinks we "
                      << (has ? "own it" : "don't own it");
    units_.erase(unit);
    upcs_.erase(unit);
    return;
  }

  Controller::removeUnit(state, unit, task->upcId());
  task->removeUnit(unit);
  state->board()->updateTasksByUnit(task.get());

  VLOG(1) << "BuilderControllerBase " << utils::upcString(upcId_)
          << ": release unit " << utils::unitString(unit) << " from "
          << utils::upcString(task->upcId());
}

/// Returns scoring function for selecting a unit to build another
/// (non-building) unit
auto BuilderControllerBase::defaultUnitBuilderScore(State* state) {
  return [this, state](Unit* u) {
    if (u->type != type_->builder) {
      return kdInfty;
    }
    if (!u->active()) {
      return kdInfty;
    }
    if (type_->isAddon && u->addon) {
      return kdInfty;
    }

    double score = 0.0;
    if (builder_ == u) {
      score -= 10.0;
    } else if (state->board()->taskDataWithUnit(u).owner == module_) {
      // We're already building something with this one
      return kdInfty;
    }
    score += u->remainingBuildTrainTime + u->remainingUpgradeResearchTime;
    return score;
  };
}

/// Returns scoring function for selecting a unit to build a Larva-based unit.
auto BuilderControllerBase::larvaBuilderScore(
    State* state,
    bool preferSaturation) {
  // Compute hatchery counts of Larvae
  std::unordered_map<int, int> larvaCount;
  for (Unit* u :
       state->unitsInfo().myCompletedUnitsOfType(buildtypes::Zerg_Larva)) {
    if (u->associatedUnit) {
      larvaCount[u->associatedUnit->id] += 1;
    }
  }

  return [this, state, preferSaturation, larvaCount = std::move(larvaCount)](
             Unit* u) {
    if (u->type != type_->builder) {
      return kdInfty;
    }
    if (!u->active()) {
      return kdInfty;
    }

    double score = 0.0;
    if (builder_ == u) {
      score -= DFOASG(10, 5);
    } else {
      auto taskData = state->board()->taskDataWithUnit(u);
      if (taskData.owner == module_) {
        // We're already building something with this one
        return kdInfty;
      }

      // Better build at a Hatchery with lots of Larva so that we'll get more
      if (u->associatedUnit && u->associatedUnit->type->producesLarva) {
        auto iterator = larvaCount.find(u->associatedUnit->id);
        if (iterator != larvaCount.end()) {
          double larva = iterator->second;
          larva += utils::clamp(
              double(state->currentFrame() - u->lastLarvaSpawn) / kLarvaFrames,
              0.0,
              1.0);
          double bonus = 4 - larva;
          bonus = bonus * bonus;
          score += bonus; // (1,16)
        }
      }

      // Build at bases where we have low saturation
      auto& areaInfo = state->areaInfo();
      auto baseIdx = areaInfo.myClosestBaseIdx(u->pos());
      if (baseIdx >= 0) {
        double saturation = areaInfo.myBase(baseIdx)->saturation;
        // 4, so it will pick a 2-Larva Hatchery over 1 Larva, but not 3 over 2
        score += DFOASG(4, 2) *
            (preferSaturation ? 1.0 - saturation : saturation); // (0, 5)
      }
    }
    score += u->remainingBuildTrainTime + u->remainingUpgradeResearchTime;
    return score;
  };
}

/// Returns scoring function for selecting a unit to morph a hatchery or lair
auto BuilderControllerBase::hatcheryTechBuilderScore(State* state) {
  return [this, state](Unit* u) {
    if (u->type != type_->builder) {
      return kdInfty;
    }
    if (!u->active()) {
      return kdInfty;
    }

    double score = 0.0;
    if (builder_ == u) {
      score -= 10.0;
    } else {
      auto taskData = state->board()->taskDataWithUnit(u);
      if (taskData.owner == module_) {
        // We're already building something with this one
        return kdInfty;
      }
    }

    // Prefer Lair and Hive in early bases
    score += 10 * state->areaInfo().myClosestBaseIdx(u->pos());

    if (u->morphing()) {
      score += u->remainingBuildTrainTime;
    }
    score += u->remainingUpgradeResearchTime;
    return score;
  };
}

bool BuilderControllerBase::cancelled(State* state) const {
  // XXX This is a hotfix since step() is currently sometimes called for
  // cancelled tasks. This should not be the case and somebody should check for
  // this beforehand (BuilderModule?). For now just do a manual check to avoid
  // regressions.
  auto* board = state->board();
  auto ctask =
      std::dynamic_pointer_cast<ControllerTask>(board->taskForId(upcId_));
  if (ctask == nullptr) {
    LOG(WARNING) << "No associated controller task? That's weird.";
    return false;
  }
  return ctask->status() == TaskStatus::Cancelled;
}

bool BuilderControllerBase::findBuilder(State* state, Position const& pos) {
  auto board = state->board();

  if (type_->isBuilding && type_->builder->isWorker) {
    if (pos.x != -1 || pos.y != -1) {
      // Try to find a builder close to the targeted location
      auto builderScore = [&](Unit* u) {
        if (u->type != type_->builder) {
          return kdInfty;
        }
        if (!u->active()) {
          return kdInfty;
        }

        double r = 0.0;
        if (builder_ == u) {
          r -= DFOASG(10, 5);
        } else {
          auto taskData = board->taskDataWithUnit(u);
          if (taskData.owner == module_) {
            // We're already building something with this one
            return kdInfty;
          }
          if (taskData.task && taskData.owner &&
              (taskData.owner->name().find("Scouting") != std::string::npos ||
               taskData.owner->name().find("Harass") != std::string::npos)) {
            return kdInfty;
          }
          if (!u->idle() && !u->unit.orders.empty()) {
            if (u->unit.orders.front().type == tc::BW::Order::MoveToMinerals) {
              r += 15.0;
            } else if (
                u->unit.orders.front().type == tc::BW::Order::ReturnMinerals) {
              r += 60.0;
            } else if (
                u->unit.orders.front().type == tc::BW::Order::MoveToGas) {
              r += 75.0;
            } else if (
                u->unit.orders.front().type == tc::BW::Order::ReturnGas) {
              r += 90.0;
            } else {
              r += 150.0;
            }
          }
          auto i = bcdata_->recentAssignedBuilders.find(u);
          if (i != bcdata_->recentAssignedBuilders.end()) {
            if (std::get<1>(i->second) == type_ &&
                utils::distance(
                    std::get<2>(i->second).x,
                    std::get<2>(i->second).y,
                    pos.x,
                    pos.y) <= DFOASG(48, 24)) {
              r -= DFOASG(1000.0, 500);
            }
          }
        }
        r += utils::distance(u, pos) / u->topSpeed;
        return r;
      };

      if (builder_ && builderScore(builder_) == kdInfty) {
        builder_->busyUntil = 0;
        builder_ = nullptr;
      }

      if (!builder_) {
        std::vector<Unit*> candidates;
        for (auto it : unitProbs_) {
          if (it.second > 0 && it.first->type == type_->builder) {
            candidates.push_back(it.first);
          }
        }
        if (!candidates.empty()) {
          builder_ = utils::getBestScoreCopy(
              std::move(candidates), builderScore, kdInfty);
        } else {
          builder_ = utils::getBestScoreCopy(
              state->unitsInfo().myCompletedUnitsOfType(type_->builder),
              builderScore,
              kdInfty);
        }
      }
    } else if (builder_) {
      builder_ = nullptr;
    }

  } else {
    std::function<double(Unit*)> builderScore;
    if (type_ == buildtypes::Zerg_Drone) {
      // Build new drones at unsaturated bases that contain lots of larva.
      builderScore = larvaBuilderScore(state, false);
    } else if (type_->builder == buildtypes::Zerg_Larva) {
      // Build other Zerg units at saturated bases that contain lots of larva.
      builderScore = larvaBuilderScore(state, true);
    } else if (
        type_ == buildtypes::Zerg_Lair || type_ == buildtypes::Zerg_Hive) {
      builderScore = hatcheryTechBuilderScore(state);
    } else {
      builderScore = defaultUnitBuilderScore(state);
    }

    if (builder_ && builderScore(builder_) == kdInfty) {
      builder_->busyUntil = 0;
      builder_ = nullptr;
    }

    if (!builder_) {
      std::vector<Unit*> candidates;
      for (auto it : unitProbs_) {
        if (it.second > 0 && it.first->type == type_->builder) {
          candidates.push_back(it.first);
        }
      }

      if (!candidates.empty()) {
        builder_ = utils::getBestScoreCopy(
            std::move(candidates), builderScore, kdInfty);
      } else {
        builder_ = utils::getBestScoreCopy(
            state->unitsInfo().myCompletedUnitsOfType(type_->builder),
            builderScore,
            kdInfty);
      }
    }
  }

  return builder_ != nullptr;
}

WorkerBuilderController::WorkerBuilderController(
    Module* module,
    BuildType const* type,
    std::unordered_map<Unit*, float> unitProbs,
    std::shared_ptr<BuilderControllerData> bcdata,
    Position pos)
    : BuilderControllerBase(
          module,
          type,
          std::move(unitProbs),
          std::move(bcdata)),
      pos_(std::move(pos)) {
  if (!type->isBuilding) {
    throw std::runtime_error("Building expected, got " + type->name);
  }
  if (type->builder == nullptr) {
    throw std::runtime_error("Don't know how to build " + type->name);
  }
  if (!type->builder->isWorker) {
    throw std::runtime_error("No worker required to build " + type->name);
  }
}

void WorkerBuilderController::step(State* state) {
  auto board = state->board();
  auto frame = state->currentFrame();
  if (succeeded_ || failed_ || cancelled(state)) {
    return;
  }

  // Regularly check if building location is still valid
  if (!building_ && frame - lastCheckLocation_ >= 11) {
    lastCheckLocation_ = frame;
    // Ignore reserved tiles in this check since reservations for buildings are
    // not handled by this module.
    if (!builderhelpers::canBuildAt(state, type_, pos_, true)) {
      VLOG(1) << logPrefix()
              << " location is no longer valid; marking task as failed";
      failed_ = true;
      return;
    }
  }

  if (moving_ && tracker_) {
    // Worker is moving to build location
    switch (tracker_->status()) {
      case TrackerStatus::Success:
        // This task didn't succeed yet, we need to start the building now
        // (this is done in BuilderModule).
        VLOG_IF(1, (tracker_->status() != trackerStatus_))
            << logPrefix() << " movement tracker reported success, resetting";
        lastUpdate_ = 0;
        moving_ = false;
        tracker_ = nullptr;
        break;
      case TrackerStatus::Cancelled:
        VLOG_IF(2, (tracker_->status() != trackerStatus_))
            << logPrefix() << " tracker cancelled but task not cancelled"
            << " marking task as failed";
        failed_ = true;
      case TrackerStatus::Timeout:
      case TrackerStatus::Failure:
        moving_ = false;
        tracker_ = nullptr;
        VLOG(1) << logPrefix() << " movement tracker reported timeout/failure";
        break;
      case TrackerStatus::Pending:
      case TrackerStatus::Ongoing:
        VLOG_IF(2, (tracker_->status() != trackerStatus_))
            << logPrefix()
            << " movement tracker reported pending/ongoing, "
               "status->ongoing";
        break;
      default:
        break;
    }
  } else if (!moving_ && tracker_) {
    switch (tracker_->status()) {
      case TrackerStatus::Pending:
        VLOG_IF(2, (tracker_->status() != trackerStatus_))
            << logPrefix() << " tracker reported pending, status->ongoing";
        break;
      case TrackerStatus::Ongoing:
        VLOG_IF(2, (tracker_->status() != trackerStatus_))
            << logPrefix() << " tracker reported ongoing, status->ongoing";
        constructionStarted_ = true;
        break;
      case TrackerStatus::Success:
        VLOG(1) << logPrefix() << " success, finished task";
        building_ = false;
        succeeded_ = true;
        break;
      case TrackerStatus::Timeout:
      case TrackerStatus::Failure:
        if (buildAttempts_ < kMaxBuildAttempts) {
          VLOG(1) << logPrefix() << " building tracker "
                  << (tracker_->status() == TrackerStatus::Timeout ? "timed out"
                                                                   : "failed")
                  << ", scheduling retry";
          lastUpdate_ = 0;
        } else {
          VLOG(1) << logPrefix() << " building tracker "
                  << (tracker_->status() == TrackerStatus::Timeout ? "timed out"
                                                                   : "failed")
                  << ", giving up";
          failed_ = true;
        }
        tracker_ = nullptr;
        building_ = false;
        break;
      case TrackerStatus::Cancelled:
        LOG(ERROR) << logPrefix() << " canceled tracker without canceled task ";
        failed_ = true;
        break;
      default:
        break;
    }
  }
  if (tracker_) {
    trackerStatus_ = tracker_->status();
  }

  if (succeeded_ || failed_) {
    return;
  }

  std::vector<uint8_t> visited;
  uint8_t visitedN = 0;

  auto findMoveAwayPos = [&](Unit* u, Position source, float distance) {
    if (visited.empty()) {
      visited.resize(TilesInfo::tilesWidth * TilesInfo::tilesHeight);
    }
    const int mapWidth = state->mapWidth();
    const int mapHeight = state->mapHeight();
    bool flying = u->flying();

    uint8_t visitedValue = ++visitedN;

    auto* tilesData = state->tilesInfo().tiles.data();

    Position startPos(u->x, u->y);

    std::deque<const Tile*> open;
    open.push_back(&state->tilesInfo().getTile(u->x, u->y));
    while (!open.empty()) {
      const Tile* tile = open.front();
      open.pop_front();

      if (utils::distance(tile->x, tile->y, source.x, source.y) >= distance) {
        return Position(tile->x, tile->y);
      }

      auto add = [&](const Tile* ntile) {
        if (!flying && (!tile->entirelyWalkable || tile->building)) {
          return;
        }
        auto& v = visited[ntile - tilesData];
        if (v == visitedValue) {
          return;
        }
        v = visitedValue;
        if (utils::distance(ntile->x, ntile->y, startPos.x, startPos.y) <=
            4 * 20) {
          open.push_back(ntile);
        }
      };

      if (tile->x > 0) {
        add(tile - 1);
      }
      if (tile->y > 0) {
        add(tile - TilesInfo::tilesWidth);
      }
      if (tile->x < mapWidth - tc::BW::XYWalktilesPerBuildtile) {
        add(tile + 1);
      }
      if (tile->y < mapHeight - tc::BW::XYWalktilesPerBuildtile) {
        add(tile + TilesInfo::tilesHeight);
      }
    }

    return Position();
  };

  if (builder_ && VLOG_IS_ON(2)) {
    utils::drawLine(state, builder_, pos_);
    utils::drawText(state, pos_, type_->name);
  }

  if (lastMoveUnitsInTheWay_ && frame - lastMoveUnitsInTheWay_ >= 30) {
    lastMoveUnitsInTheWay_ = 0;
    for (auto* u : movedUnits_) {
      releaseUnit(state, u);
    }
    movedUnits_.clear();
  }

  if (!constructionStarted_) {
    bcdata_->res.ore -= type_->mineralCost;
    bcdata_->res.gas -= type_->gasCost;
  }

  // Update required?
  if (lastUpdate_ > 0 && frame - lastUpdate_ < 4) {
    return;
  }
  lastUpdate_ = frame;

  bool moveOnly = false;
  if (!constructionStarted_) {
    if (type_->mineralCost && bcdata_->res.ore < 0) {
      moveOnly = true;
    }
    if (type_->gasCost && bcdata_->res.gas < 0) {
      moveOnly = true;
    }
    if (type_->supplyRequired &&
        bcdata_->res.used_psi + type_->supplyRequired >
            bcdata_->res.total_psi) {
      moveOnly = true;
    }
    if (!moveOnly) {
      if (!utils::prerequisitesReady(state, type_)) {
        moveOnly = true;
      }
    }
  }

  if (builder_ && (pos_.x != -1 || pos_.y != -1)) {
    bcdata_->recentAssignedBuilders[builder_] =
        std::make_tuple(frame, type_, pos_);
  }

  // Find a builder
  if (!builder_ && !building_) {
    findBuilder(state, pos_);
    if (builder_) {
      if (moveOnly && (pos_.x != -1 || pos_.y != -1)) {
        double t = 0.0;
        if (type_->mineralCost) {
          t = std::max(t, -bcdata_->res.ore / bcdata_->currentMineralsPerFrame);
        }
        if (type_->gasCost) {
          t = std::max(t, -bcdata_->res.gas / bcdata_->currentGasPerFrame);
        }
        if (t > utils::distance(builder_, pos_) / builder_->topSpeed) {
          builder_ = nullptr;
        }
      }
      if (builder_) {
        VLOG(1) << logPrefix()
                << " found builder: " << utils::unitString(builder_);
        grabUnit(state, builder_);
      }
    }
    if (builder_ == nullptr) {
      VLOG(1) << logPrefix() << " could not determine builder right now";
    }
  }

  if (type_->isBuilding) {
    if (builder_ && (pos_.x != -1 || pos_.y != -1)) {
      if (!detector_) {
        detector_ = utils::getBestScoreCopy(
            state->unitsInfo().myUnits(),
            [&](Unit* u) {
              if (!u->type->isDetector || u->type->isBuilding || !u->active() ||
                  board->taskWithUnit(u)) {
                return kdInfty;
              }
              return (double)utils::distance(u, pos_);
            },
            kdInfty);
        if (detector_) {
          grabUnit(state, detector_);
        }
      } else {
        auto tgt = movefilters::safeMoveTo(state, detector_, pos_);
        if (tgt.x < 0 || tgt.y < 0) {
          VLOG(1) << "detector stuck";
          tgt = pos_;
        } else if (tgt.distanceTo(detector_->getMovingTarget()) > 4) {
          // condition made to avoid sending too many commands
          addUpc(detector_, tgt, Command::Move);
        }
      }

      // hack: we should not rely on movement tracker
      // re-send move every now and then if not close to destination
      float distFromBuilderToDestThreshold = 4.0f * 2;
      if (type_->isRefinery) {
        distFromBuilderToDestThreshold = 4.0f * 6;
      }
      Position targetPosition(
          std::min(
              pos_.x + type_->tileWidth * tc::BW::XYWalktilesPerBuildtile / 2,
              state->mapWidth() - 1),
          std::min(
              pos_.y + type_->tileHeight * tc::BW::XYWalktilesPerBuildtile / 2,
              state->mapHeight() - 1));
      auto distFromBuilderToDest = utils::distance(builder_, targetPosition);

      if (tracker_ == nullptr ||
          distFromBuilderToDest >= distFromBuilderToDestThreshold) {
        if (distFromBuilderToDest >= distFromBuilderToDestThreshold) {
          auto tgt = movefilters::safeMoveTo(state, builder_, targetPosition);
          if (tgt.x < 0 || tgt.y < 0) {
            VLOG(1) << "builder stuck";
            tgt = targetPosition; // well, that won't do anything
          }
          if (tgt.distanceTo(builder_->getMovingTarget()) > 4) {
            addUpc(builder_, tgt, Command::Move);
            if (!tracker_) {
              tracker_ = state->addTracker<MovementTracker>(
                  std::initializer_list<Unit*>{builder_},
                  targetPosition.x,
                  targetPosition.y,
                  distFromBuilderToDestThreshold);
              trackerStatus_ = tracker_->status();
              moving_ = true;
              VLOG(3) << logPrefix() << " using MovementTracker, distance="
                      << distFromBuilderToDest
                      << ", threshold=" << distFromBuilderToDestThreshold;
            }
          }
        } else if (!moveOnly) { // and tracker_ == nullptr
          // Any units we need to get out of the way to build here?
          Unit* killUnit = nullptr;
          int movedUnits = 0;
          for (Unit* e : state->unitsInfo().visibleUnits()) {
            if (!e->flying() && !e->invincible() && e != builder_ &&
                e->detected() && !e->type->isBuilding) {
              auto d = pxDistanceOfUnit(e, type_, pos_);
              if (e->isMine && !e->type->isNonUsable && d <= 16) {
                Position target = findMoveAwayPos(e, Position(builder_), 16);

                lastMoveUnitsInTheWay_ = frame;
                // Grab that unit and move it away
                movedUnits_.insert(e);
                grabUnit(state, e);
                if (e->burrowed()) {
                  state->board()->postCommand(
                      tc::Client::Command(
                          tc::BW::Command::CommandUnit,
                          e->id,
                          tc::BW::UnitCommandType::Unburrow),
                      upcId_);
                }
                addUpc(e, target, Command::Move);
                VLOG(1) << logPrefix() << " moving " << utils::unitString(e)
                        << " out of the way";
                ++movedUnits;
                continue;
              }
              if (d <= 0) {
                VLOG(1) << logPrefix() << " going to kill blocking unit "
                        << utils::unitString(e);
                killUnit = e;
                break;
              }
            }
          }

          if (killUnit) {
            for (auto* u : movedUnits_) {
              releaseUnit(state, u);
            }
            movedUnits_.clear();
            addUpc(builder_, killUnit, Command::Delete);
          } else {
            if (movedUnits) {
              ++moveAttempts_;
            }
            if (!movedUnits || moveAttempts_ >= 12) {
              ++buildAttempts_;
            }
            if (buildAttempts_ > kMaxBuildAttempts) {
              // Block tiles at build location for some time, maybe it will
              // work then.
              buildAttempts_ = 0;
              for (int y = pos_.y; y !=
                   pos_.y + tc::BW::XYWalktilesPerBuildtile * type_->tileHeight;
                   ++y) {
                for (int x = pos_.x; x !=
                     pos_.x +
                         tc::BW::XYWalktilesPerBuildtile * type_->tileWidth;
                     ++x) {
                  Tile* t = state->tilesInfo().tryGetTile(x, y);
                  if (t) {
                    t->blockedUntil = std::max(
                        t->blockedUntil, state->currentFrame() + 15 * 30);
                  }
                }
              }
            }

            tracker_ = state->addTracker<BuildTracker>(builder_, type_, 15);
            trackerStatus_ = tracker_->status();
            building_ = true;
            addUpc(builder_, pos_, Command::Create, type_);
          }

          VLOG(3) << logPrefix() << " using BuildTracker, distance = "
                  << utils::distance(builder_, pos_);
        }
      }
    }
  }

  postUpcs(state);
}

void WorkerBuilderController::removeUnit(State* state, Unit* unit, UpcId id) {
  if (unit == builder_) {
    builder_ = nullptr;
  }
  if (unit == detector_) {
    detector_ = nullptr;
  }
  BuilderControllerBase::removeUnit(state, unit, id);
}

std::string WorkerBuilderController::logPrefix() const {
  std::ostringstream oss;
  oss << "WorkerBuilderController for task " << utils::upcString(upcId_) << " ("
      << utils::buildTypeString(type_) << "):";
  return oss.str();
}

BuilderController::BuilderController(
    Module* module,
    BuildType const* type,
    std::unordered_map<Unit*, float> unitProbs,
    std::shared_ptr<BuilderControllerData> bcdata)
    : BuilderControllerBase(
          module,
          type,
          std::move(unitProbs),
          std::move(bcdata)) {}

void BuilderController::step(State* state) {
  auto frame = state->currentFrame();
  if (succeeded_ || failed_ || cancelled(state)) {
    return;
  }

  if (tracker_) {
    switch (tracker_->status()) {
      case TrackerStatus::Pending:
        VLOG_IF(2, (tracker_->status() != trackerStatus_))
            << logPrefix() << " tracker reported pending, status->ongoing";
        break;
      case TrackerStatus::Ongoing:
        VLOG_IF(2, (tracker_->status() != trackerStatus_))
            << logPrefix() << " tracker reported ongoing, status->ongoing";
        constructionStarted_ = true;
        break;
      case TrackerStatus::Success:
        VLOG(1) << logPrefix() << " success, finished task";
        succeeded_ = true;
        break;
      case TrackerStatus::Timeout:
      case TrackerStatus::Failure:
        VLOG(1) << logPrefix() << " building tracker "
                << (tracker_->status() == TrackerStatus::Timeout ? "timed out"
                                                                 : "failed")
                << ", scheduling retry";
        lastUpdate_ = 0;
        tracker_ = nullptr;
        break;
      case TrackerStatus::Cancelled:
        LOG(ERROR) << logPrefix() << " canceled tracker without canceled task ";
        failed_ = true;
        break;
      default:
        break;
    }
  }
  if (tracker_) {
    trackerStatus_ = tracker_->status();
  }

  if (succeeded_ || failed_) {
    return;
  }

  if (!constructionStarted_) {
    bcdata_->res.ore -= type_->mineralCost;
    bcdata_->res.gas -= type_->gasCost;
  }

  // Update required?
  if (lastUpdate_ > 0 && frame - lastUpdate_ < 4) {
    return;
  }
  lastUpdate_ = frame;

  bool canBuild = true;
  if (!constructionStarted_) {
    if (type_->mineralCost && bcdata_->res.ore < 0) {
      canBuild = false;
    }
    if (type_->gasCost && bcdata_->res.gas < 0) {
      canBuild = false;
    }
    if (type_->supplyRequired &&
        bcdata_->res.used_psi + type_->supplyRequired >
            bcdata_->res.total_psi) {
      canBuild = false;
    }
    if (canBuild) {
      if (!utils::prerequisitesReady(state, type_)) {
        canBuild = false;
      }
    }
  }

  if (!canBuild) {
    return;
  }

  if (builder_ == nullptr && tracker_ == nullptr) {
    findBuilder(state);
    if (builder_) {
      VLOG(1) << logPrefix()
              << " found builder: " << utils::unitString(builder_);
      grabUnit(state, builder_);
    } else {
      VLOG(1) << logPrefix() << " could not determine builder right now";
    }
  }

  if (builder_ && tracker_ == nullptr) {
    addUpc(builder_, builder_->pos(), Command::Create, type_);

    if (type_->isUpgrade()) {
      tracker_ = state->addTracker<UpgradeTracker>(builder_, type_, 15);
    } else if (type_->isTech()) {
      tracker_ = state->addTracker<ResearchTracker>(builder_, type_, 15);
    } else {
      tracker_ = state->addTracker<BuildTracker>(builder_, type_, 15);
    }
    trackerStatus_ = tracker_->status();
  }

  postUpcs(state);
}

void BuilderController::removeUnit(State* state, Unit* unit, UpcId id) {
  if (unit == builder_) {
    builder_ = nullptr;
  }
  BuilderControllerBase::removeUnit(state, unit, id);
}

std::string BuilderController::logPrefix() const {
  std::ostringstream oss;
  oss << "BuilderController for task " << utils::upcString(upcId_) << " ("
      << utils::buildTypeString(type_) << "):";
  return oss.str();
}

} // namespace cherrypi
