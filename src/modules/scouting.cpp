/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "modules/scouting.h"

#include "areainfo.h"
#include "builderhelper.h"
#include "commandtrackers.h"
#include "fogofwar.h"
#include "movefilters.h"
#include "state.h"
#include "task.h"
#include "unitsinfo.h"
#include "utils.h"

#include <bwem/map.h>

#include <deque>
#include <queue>

namespace cherrypi {

REGISTER_SUBCLASS_0(Module, ScoutingModule);

namespace {

class SneakyOverlordImpl {
 public:
  SneakyOverlordImpl(State* state) {
    // Precompute the range at which we will try to keep overlords.
    int range = 4 * 8;
    std::unordered_set<Position> set;
    for (int y = -range; y <= range; y += 4) {
      for (int x = -range; x <= range; x += 4) {
        if (utils::distance(Position(0, 0), Position(x, y)) <= range) {
          relativePositions.emplace_back(x, y);
          set.emplace(x, y);
        }
      }
    }
    for (int y = -range - 4; y <= range + 4; y += 4) {
      for (int x = -range - 4; x <= range + 4; x += 4) {
        Position pos(x, y);
        if (set.find(pos) == set.end()) {
          if (set.find(pos + Position(4, 0)) != set.end() ||
              set.find(pos + Position(-4, 0)) != set.end() ||
              set.find(pos + Position(0, 4)) != set.end() ||
              set.find(pos + Position(0, -4)) != set.end()) {
            edgeRelativePositions.push_back(pos);
          }
        }
      }
    }
  }

  size_t posIndex(Position pos) {
    return (size_t)pos.y / (size_t)tc::BW::XYWalktilesPerBuildtile *
        TilesInfo::tilesWidth +
        (size_t)pos.x / (size_t)tc::BW::XYWalktilesPerBuildtile;
  }

  // Update the score map, which is mostly the distance/cost to move from each
  // build tile to some goal.
  void updateScoreMap(State* state, Unit* unit, Position targetPos) {
    std::vector<Position> path;
    Position enemyPos = targetPos;
    Position enemyExpoPos = targetPos;
    Position enemyChokePos = targetPos;
    // If we haven't found the enemy, then just go to the location we are
    // initially assigned.
    if (!state->areaInfo().foundEnemyStartLocation()) {
      if (scoutPos == Position()) {
        scoutPos = targetPos;
      }
      enemyPos = targetPos = enemyExpoPos = enemyChokePos = scoutPos;
      auto* tile = state->tilesInfo().tryGetTile(scoutPos.x, scoutPos.y);
      if (!tile || tile->visible) {
        auto& locs = state->areaInfo().candidateEnemyStartLocations();
        if (!locs.empty()) {
          scoutPos = utils::getBestScoreCopy(locs, [&](Position pos) {
            return utils::distance(pos, Position(unit));
          });
        }
      }
    } else {
      // Find the enemy natural and choke between their main and natural.
      enemyPos = state->areaInfo().enemyStartLocation();
      targetPos = enemyPos;

      Position myPos = state->areaInfo().myStartLocation();
      enemyPos = state->areaInfo().enemyStartLocation();
      path = state->areaInfo().walkPath(enemyPos, myPos);
      path.resize(path.size() / 3);
      if (!path.empty()) {
        float bestBasePathIndexScore = kdInfty;
        size_t bestBasePathIndex = path.size() - 1;
        Position bestBasePos;
        for (auto& area : state->map()->Areas()) {
          for (auto& base : area.Bases()) {
            if (!base.BlockingMinerals().empty()) {
              continue;
            }
            Position pos(
                base.Location().x * tc::BW::XYWalktilesPerBuildtile,
                base.Location().y * tc::BW::XYWalktilesPerBuildtile);
            if (utils::distance(pos, enemyPos) <= 4 * 15) {
              continue;
            }
            if (!builderhelpers::canBuildAt(
                    state, buildtypes::Zerg_Hatchery, pos, true)) {
              bool skip = true;
              if (utils::distance(pos, enemyPos) > 4 * 10) {
                Tile* tile = state->tilesInfo().tryGetTile(pos.x, pos.y);
                if (tile && tile->building && tile->building->isEnemy) {
                  skip = false;
                }
              }
              if (skip) {
                continue;
              }
            }
            auto* area = state->areaInfo().tryGetArea(pos);
            if (!area) {
              continue;
            }
            size_t bestPathPosIndex = 0;
            float bestPathPosScore = kdInfty;
            for (auto* cp : area->area->ChokePoints()) {
              BWAPI::WalkPosition cpPos = cp->Center();
              for (size_t i = 0; i != path.size(); ++i) {
                auto pathPos = path[i];
                float score =
                    utils::distance(cpPos.x, cpPos.y, pathPos.x, pathPos.y);
                if (score < bestPathPosScore) {
                  bestPathPosScore = score;
                  bestPathPosIndex = i;
                }
              }
            }
            float s = bestPathPosIndex * 4 * 12.0f;
            s = s * s + bestPathPosScore * bestPathPosScore;
            if (s < bestBasePathIndexScore) {
              bestBasePathIndexScore = s;
              bestBasePathIndex = bestPathPosIndex;
              bestBasePos = pos;
            }
          }
          enemyExpoPos = bestBasePos;
          enemyChokePos = path[bestBasePathIndex];
        }
      }
    }

    uint8_t inRangeValue = nextInRangeValue++;
    if (inRangeValue == 0) {
      inRange.fill(0);
      inRangeValue = nextInRangeValue++;
    }

    for (unsigned y = 0; y != state->tilesInfo().mapTileHeight(); ++y) {
      auto from = scoreMap.begin() + y * TilesInfo::tilesWidth;
      auto to = from + state->tilesInfo().mapTileWidth();
      std::fill(from, to, 0.0f);
    }

    struct OpenNode {
      Position pos;
      float score = 0.0f;
    };
    struct OpenNodeCmp {
      bool operator()(const OpenNode& a, const OpenNode& b) const {
        return a.score > b.score;
      }
    };

    std::priority_queue<OpenNode, std::vector<OpenNode>, OpenNodeCmp> open;

    // Find the area around each unit that is "in range", or too close for our
    // overlord to move there. Only consider units that can shoot up.
    for (Unit* e : state->unitsInfo().enemyUnits()) {
      if (e->gone) {
        continue;
      }
      if ((!e->type->isBuilding || e->flying()) && !e->type->hasAirWeapon) {
        continue;
      }

      for (Position relPos : relativePositions) {
        Position pos = utils::clampPositionToMap(state, Position(e) + relPos);
        size_t index = posIndex(pos);
        inRange[index] = inRangeValue;
      }
    }

    // How desireable some location is based on when we saw it last.
    auto frameScore = [&](FrameNum frame) {
      FrameNum age = state->currentFrame() - frame;
      FrameNum maxAge = 24 * 60 * 2;
      if (age > maxAge) {
        age = maxAge;
      }
      return (float)(maxAge - age);
    };

    // Add some position as a desirable scout target.
    auto addOpen = [&](Position sourcePos, float score) {
      if (sourcePos == Position()) {
        return;
      }
      if (score == -1) {
        score = frameScore(
            state->tilesInfo().getTile(sourcePos.x, sourcePos.y).lastSeen);
      }
      for (Position relPos : edgeRelativePositions) {
        Position pos = utils::clampPositionToMap(state, sourcePos + relPos);
        size_t index = posIndex(pos);
        if (!inRange[index] && scoreMap[index] == 0.0f) {
          scoreMap[index] = score;
          open.push({pos, score});
        }
      }
    };

    bool isNearestEnemyExpo = true;
    float enemyExpoDistance = utils::distance(unit, enemyExpoPos);
    for (Unit* u : state->unitsInfo().myUnitsOfType(unit->type)) {
      if (utils::distance(u, enemyExpoPos) < enemyExpoDistance) {
        isNearestEnemyExpo = false;
      }
    }

    // One overlord checks our the natural/choke, and one/the rest checks out
    // the main.
    if (isNearestEnemyExpo) {
      addOpen(enemyExpoPos, -1);
      addOpen(enemyChokePos, -1);
    } else {
      addOpen(enemyPos, -1);
    }

    // Try to keep a tab on all enemy buildings.
    for (Unit* e : state->unitsInfo().enemyUnits()) {
      if (e->gone)
        continue;
      if (!e->type->isBuilding || e->flying())
        continue;

      addOpen(Position(e), frameScore(e->lastSeen));
    }

    while (!open.empty()) {
      OpenNode cur = open.top();
      open.pop();

      auto add = [&](Position pos, float dist) {
        pos = utils::clampPositionToMap(state, pos);
        size_t index = posIndex(pos);
        if (scoreMap[index] != 0.0f || inRange[index] == inRangeValue) {
          return;
        }
        float newScore = cur.score + dist;
        scoreMap[index] = newScore;
        open.push({pos, newScore});
      };

      add(cur.pos + Position(4, 0), 4.0f);
      add(cur.pos + Position(-4, 0), 4.0f);
      add(cur.pos + Position(0, 4), 4.0f);
      add(cur.pos + Position(0, -4), 4.0f);
      add(cur.pos + Position(4, 4), 5.656854249f);
      add(cur.pos + Position(-4, 4), 5.656854249f);
      add(cur.pos + Position(-4, -4), 5.656854249f);
      add(cur.pos + Position(4, -4), 5.656854249f);
    }
  }

  bool update(State* state, Unit* unit, Position& location) {
    Position targetPos = location;

    if (state->currentFrame() - lastUpdateScoreMap >= 6) {
      lastUpdateScoreMap = state->currentFrame();
      updateScoreMap(state, unit, targetPos);
    }

    // Move to some nearby position with a low score
    float bestScore = kfInfty;
    int range = 4 * 6;
    bool escape = inRange[posIndex(unit->pos())];
    if (escape) {
      range = 4 * 12;
    }
    Position beginPos = Position(unit) - Position(range, range);
    Position endPos = beginPos + Position(range * 2, range * 2);
    beginPos = utils::clampPositionToMap(state, beginPos);
    endPos = utils::clampPositionToMap(state, endPos);
    int beginTileX = beginPos.x / tc::BW::XYWalktilesPerBuildtile;
    int beginTileY = beginPos.y / tc::BW::XYWalktilesPerBuildtile;
    int endTileX = endPos.x / tc::BW::XYWalktilesPerBuildtile;
    int endTileY = endPos.y / tc::BW::XYWalktilesPerBuildtile;
    for (int y = beginTileY; y != endTileY; ++y) {
      for (int x = beginTileX; x != endTileX; ++x) {
        auto& tile =
            state->tilesInfo()
                .tiles[(unsigned)y * TilesInfo::tilesWidth + (unsigned)x];
        Position pos(tile);
        size_t index = posIndex(pos);
        if (index != posIndex(unit->pos())) {
          if (escape) {
            if (!inRange[index]) {
              float d = utils::distance(pos, unit);
              float s = scoreMap[index];
              s = s * s + d * d;
              if (s < bestScore) {
                bestScore = s;
                targetPos = pos;
              }
            }
          } else {
            float s = scoreMap[index];
            if (s != 0.0f && s < bestScore) {
              bestScore = s;
              targetPos = pos;
            }
          }
        }
      }
    }

    // If there's something that can attack us, just flee from it.
    Vec2 fleeSum;
    int fleeN = 0;
    for (Unit* e : unit->unitsInSightRange) {
      if (e->isEnemy && e->type->hasAirWeapon &&
          utils::distance(unit, e) <= 4 * 9) {
        fleeSum += Vec2(e);
        ++fleeN;
      }
    }
    if (fleeN) {
      fleeSum /= fleeN;
      location = utils::clampPositionToMap(
          state,
          Position(unit) +
              Position((Vec2(unit) - fleeSum).normalize() * 4 * 8));
      return true;
    }

    if (utils::distance(unit, targetPos) < 12) {
      targetPos = Position(unit) +
          Position((Vec2(targetPos) - Vec2(unit)).normalize() * 12);
    }

    location = utils::clampPositionToMap(state, targetPos);
    return true;
  }

 private:
  Position scoutPos;
  std::vector<Position> relativePositions;
  std::vector<Position> edgeRelativePositions;
  std::array<uint8_t, TilesInfo::tilesWidth * TilesInfo::tilesHeight> inRange{};
  uint8_t nextInRangeValue = 1;
  std::array<float, TilesInfo::tilesWidth * TilesInfo::tilesHeight> scoreMap{};
  FrameNum lastUpdateScoreMap = 0;
};

class ScoutingTask : public Task {
 public:
  ScoutingTask(int upcId, Unit* unit, Position location, ScoutingGoal goal)
      : Task(upcId, {unit}), location_(location), goal_(goal) {
    setStatus(TaskStatus::Ongoing);
  }

  void update(State* state) override {
    if (finished()) {
      return;
    }
    auto loc = location();
    auto& tgtArea = state->areaInfo().getArea(loc);
    targetVisited_ =
        (tgtArea.isEnemyBase || !tgtArea.isPossibleEnemyStartLocation ||
         foundBlockedChoke(state));
    if (!proxiedUnits().empty()) { // debug: keep track of reallocations
      auto unit = *proxiedUnits().begin();
      auto taskData = state->board()->taskDataWithUnit(unit);
      if (!taskData.task && !unit->dead) {
        VLOG(3) << "scout " << utils::unitString(unit)
                << " reassigned to no task";
      }
      if (taskData.task && taskData.task.get() != this) {
        VLOG(2) << "scout " << utils::unitString(unit)
                << " reassigned to module " << taskData.owner->name();
      }
      if (unit->dead) {
        VLOG(3) << "scout " << utils::unitString(unit) << " dead ";
      }
    }

    // Now check the failure case. If all my units died
    // then this task failed
    removeDeadOrReassignedUnits(state);
    if (units().empty()) {
      setStatus(TaskStatus::Failure);
      return;
    }
    auto unit = *units().begin();
    for (auto bldg : state->unitsInfo().visibleEnemyUnits()) {
      if (bldg->type->isBuilding &&
          utils::distance(bldg->x, bldg->y, unit->x, unit->y) <=
              unit->sightRange) {
        targetScouted_ = true;
        break;
      }
    }
    if (goal_ == ScoutingGoal::FindEnemyBase) {
      if (state->areaInfo().foundEnemyStartLocation()) {
        targetScouted_ = true;
      }
    }

    if (goal_ == ScoutingGoal::SneakyOverlord) {
      if (!sneakyOverlordImpl_) {
        sneakyOverlordImpl_ = std::make_unique<SneakyOverlordImpl>(state);
      }
      if (!sneakyOverlordImpl_->update(state, unit, location_)) {
        goal_ = ScoutingGoal::FindEnemyBase;
      }
    }
  }

  Unit* getUnit() {
    if (units().empty()) {
      LOG(DFATAL) << "getting the unit of a task without unit";
    }
    return *units().begin();
  }

  Position location() const {
    return location_;
  }

  ScoutingGoal goal() const {
    return goal_;
  }

  bool satisfiesGoal() {
    switch (goal_) {
      case ScoutingGoal::ExploreEnemyBase:
        return targetScouted_;
      case ScoutingGoal::FindEnemyBase:
      case ScoutingGoal::FindEnemyExpand:
        return targetVisited_;
      case ScoutingGoal::SneakyOverlord:
        return false;
      case ScoutingGoal::Automatic:
        LOG(ERROR) << "invalid goal specification "
                   << "in check that the goal is satisfied for scouting task "
                   << upcId();
        setStatus(TaskStatus::Failure);
        return true;
    }
    // cannot be reached -- avoid warning
    return true;
  }

  void resetLocation(Position const& pos) {
    if (pos == location_) {
      VLOG(0) << "reseting a scouting task with the same location";
    }
    location_ = pos;
    targetVisited_ = false;
    targetScouted_ = false;
  }

  bool foundBlockedChoke(State* state) {
    // the chokepoint is considered "blocked" if there is
    // a chokepoint of the target area for which some units attack us
    // what to do if attacked at some other location ? we need a bit of micro
    // for that
    if (units().empty()) {
      LOG(ERROR) << "updating a finished scouting task";
    }
    auto unit = getUnit();
    if (unit->beingAttackedByEnemies.empty()) {
      // if we're not attacked, it's not blocked
      return false;
    }

    // Heuristic value
    const int distanceFromChokePoint = 42;

    auto tgt = location();
    auto targetArea =
        state->map()->GetNearestArea(BWAPI::WalkPosition(tgt.x, tgt.y));
    for (auto other : unit->beingAttackedByEnemies) {
      for (auto& choke : targetArea->ChokePoints()) {
        auto cwtp = choke->Center();
        if (utils::distance(cwtp.x, cwtp.y, other->x, other->y) <
            distanceFromChokePoint) {
          return true;
        }
      }
    }
    return false;
  }

 protected:
  Position location_;
  ScoutingGoal goal_;
  bool targetVisited_ = false;
  bool targetScouted_ = false;
  std::unique_ptr<SneakyOverlordImpl> sneakyOverlordImpl_;
};

} // namespace

void ScoutingModule::setScoutingGoal(ScoutingGoal goal) {
  scoutingGoal_ = goal;
}

ScoutingGoal ScoutingModule::goal(State* state) const {
  if (scoutingGoal_ != ScoutingGoal::Automatic) {
    return scoutingGoal_;
  } else if (!state->areaInfo().foundEnemyStartLocation()) {
    return ScoutingGoal::FindEnemyBase;
  } else if (
      state->areaInfo().foundEnemyStartLocation() &&
      !state->areaInfo().numEnemyBases()) {
    return ScoutingGoal::ExploreEnemyBase;
  } else {
    return ScoutingGoal::FindEnemyExpand;
  }
}

void ScoutingModule::step(State* state) {
  updateLocations(
      state,
      startingLocations_,
      state->areaInfo().candidateEnemyStartLocations());

  auto board = state->board();
  // clean up tasks
  for (auto task : board->tasksOfModule(this)) {
    if (!task->finished()) {
      task->update(state); // check re-assignment at this step
    }
    if (task->finished()) {
      continue;
    }
    auto stask = std::static_pointer_cast<ScoutingTask>(task);
    auto unit = stask->getUnit();
    if (stask->satisfiesGoal()) {
      if (stask->goal() == ScoutingGoal::FindEnemyBase &&
          goal(state) == ScoutingGoal::FindEnemyBase) {
        auto pos = stask->location();
        auto tgt = nextScoutingLocation(state, unit, startingLocations_);
        if (tgt == pos) {
          VLOG(0) << "reseting scouting task with same location with "
                  << startingLocations_.size() << " candidate locations."
                  << " Do we know the enemy start location (check areaInfo)? "
                  << state->areaInfo().foundEnemyStartLocation()
                  << " current scouting goal " << (int)goal(state);
        }
        stask->resetLocation(tgt);
        if (postMoveUPC(state, stask->upcId(), unit, tgt)) {
          VLOG(3) << "starting location " << pos.x << ", " << pos.y
                  << " visited"
                  << " sending scout " << utils::unitString(unit)
                  << " to next location: " << tgt.x << ", " << tgt.y;
          startingLocations_[tgt] = state->currentFrame();
        } else {
          // what to do here
          VLOG(0) << "move to location " << tgt.x << ", " << tgt.y
                  << " for scout " << utils::unitString(unit)
                  << " filtered by the blackboard, canceling task "
                  << stask->upcId();
          stask->cancel(state);
        }
      } else { // no need to update on explore
        stask->setStatus(TaskStatus::Success);
        VLOG(3) << "scouting task " << stask->upcId()
                << " marked as succeedded";
      }
    } else {
      postMoveUPC(
          state,
          stask->upcId(),
          unit,
          stask->location(),
          stask->goal() != ScoutingGoal::SneakyOverlord);
    }
  }

  // consume UPCs
  // all UPCs at a given time will be set using the current module's goal
  // since the UPC does not directly allow for goal specification
  for (auto upcPair : board->upcsWithSharpCommand(Command::Scout)) {
    if (upcPair.second->unit.empty()) {
      LOG(ERROR) << "main scouting UPC without unit specification -- consuming "
                    "but ignoring";
      board->consumeUPC(upcPair.first, this);
      continue;
    }
    Unit* unit = nullptr;
    switch (goal(state)) {
      case ScoutingGoal::FindEnemyBase:
        unit = findUnit(state, upcPair.second->unit, Position(-1, -1));
        break;
      case ScoutingGoal::FindEnemyExpand:
      case ScoutingGoal::ExploreEnemyBase:
        if (!(startingLocations_.size() == 1)) {
          LOG(ERROR)
              << "invalid scouting goal (ExploreEnemyBase/FindEnemyExpand) "
              << " because no enemy location";
          break;
        }
        unit = findUnit(
            state, upcPair.second->unit, startingLocations_.begin()->first);
        break;
      case ScoutingGoal::SneakyOverlord:
      case ScoutingGoal::Automatic:
        LOG(ERROR) << "invalid goal";
    }
    if (!unit) {
      VLOG(3) << "could not find scout for upc " << upcPair.first
              << " -- skipping for now"
              << "number of units of required type: "
              << state->unitsInfo()
                     .myCompletedUnitsOfType(buildtypes::Zerg_Drone)
                     .size();
      continue;
    }
    board->consumeUPC(upcPair.first, this);
    auto taskGoal = goal(state);
    if (unit && unit->type == buildtypes::Zerg_Overlord) {
      taskGoal = ScoutingGoal::SneakyOverlord;
    }
    auto tgt = nextScoutingLocation(state, unit, startingLocations_);
    if (postTask(state, upcPair.first, unit, tgt, taskGoal)) {
      startingLocations_[tgt] = state->currentFrame();
    }
  }

  // clean up finished tasks: send the scouts back to base
  auto myLoc = state->areaInfo().myStartLocation();
  for (auto task : state->board()->tasksOfModule(this)) {
    if (!task->finished()) {
      continue;
    }
    if (!task->proxiedUnits().empty()) {
      auto stask = std::static_pointer_cast<ScoutingTask>(task);
      auto unit = stask->getUnit();
      auto chkTask = board->taskWithUnit(unit);
      // scout not reallocated, send it back
      if (chkTask == task) {
        VLOG(3) << "sending scout " << utils::unitString(unit)
                << " back to base";
        postMoveUPC(state, stask->upcId(), unit, myLoc);
      }
    }
    // manual removal because the status might have changed during the step
    board->markTaskForRemoval(task->upcId());
  }
}

Unit* ScoutingModule::findUnit(
    State* state,
    std::unordered_map<Unit*, float> const& candidates,
    Position const& pos) {
  auto board = state->board();

  // Find some units to scout with, preferring faster units and flying units,
  // and ignoring workers if possible to let them keep on working
  auto map = state->map();
  auto mapSize = state->mapWidth() * state->mapHeight();
  auto unitScore = [&](Unit* u) -> double {
    auto it = candidates.find(u);
    if (it == candidates.end() || it->second <= 0) {
      return kdInfty;
    }
    auto tdata = board->taskDataWithUnit(u);
    if (tdata.owner == this) {
      // scout is free and previously assigned to us
      if (tdata.task->finished()) {
        int pLength = 0;
        if (pos.x > 0 && pos.y > 0) {
          map->GetPath(
              BWAPI::Position(BWAPI::WalkPosition(u->x, u->y)),
              BWAPI::Position(BWAPI::WalkPosition(pos.x, pos.y)),
              &pLength);
        }
        return -2 * mapSize + pLength;
      }
      // We're using this unit already
      return kdInfty;
    }
    if (!u->active()) {
      return -200;
    }
    if (tdata.task && tdata.task->status() == TaskStatus::Success) {
      // The unit just finished a task, it should be free now
      return -100;
    }

    // wait for an available worker if all are currently busy bringing resources
    if (!u->idle() && !u->unit.orders.empty()) {
      if (u->unit.orders.front().type == tc::BW::Order::MoveToMinerals) {
        return 15.0;
      } else if (u->unit.orders.front().type == tc::BW::Order::MoveToGas) {
        return 50.0;
      }
    }
    return 100;
  };

  auto& uinfo = state->unitsInfo();
  return utils::getBestScoreCopy(uinfo.myUnits(), unitScore, kdInfty);
}

bool ScoutingModule::postTask(
    State* state,
    UpcId baseUpcId,
    Unit* unit,
    Position loc,
    ScoutingGoal goal) {
  if (!postMoveUPC(state, baseUpcId, unit, loc)) {
    VLOG(1) << "task for unit " << utils::unitString(unit) << " not created";
    return false;
  }
  auto newTask = std::make_shared<ScoutingTask>(baseUpcId, unit, loc, goal);
  state->board()->postTask(newTask, this, false); // no auto-removal
  VLOG(1) << "new scouting task " << baseUpcId << " with unit "
          << utils::unitString(unit) << "for location " << loc.x << ", "
          << loc.y;
  return true;
}

bool ScoutingModule::postMoveUPC(
    State* state,
    UpcId baseUpcId,
    Unit* unit,
    const Position& loc,
    bool useSafeMove) {
  auto tgt = useSafeMove ? movefilters::safeMoveTo(state, unit, loc) : loc;
  if (tgt.x <= 0 || tgt.y <= 0) {
    LOG(WARNING) << "scout stuck";
  }
  if (tgt.distanceTo(unit->getMovingTarget()) <= 4) {
    return true;
  }
  auto upc = std::make_shared<UPCTuple>();
  upc->unit[unit] = 1;
  upc->command[Command::Move] = 1;
  upc->position = tgt;
  auto upcId = state->board()->postUPC(std::move(upc), baseUpcId, this);
  if (upcId < 0) {
    VLOG(1) << "MoveUPC for unit " << utils::unitString(unit)
            << " filtered by blackboard";
    return false;
  }
  return true;
}

Position ScoutingModule::nextScoutingLocation(
    State* state,
    Unit* unit,
    std::unordered_map<Position, int> const& locations) {
  // next location is latest visited then closest
  Position curPos = unit->pos();
  float minDist = kfInfty;
  auto lastFrame = std::numeric_limits<int>::max();
  auto bestPos = Position(-1, -1);

  for (auto tgtPosPair : locations) {
    auto pos = tgtPosPair.first;
    auto frame = tgtPosPair.second;
    float d = 0.0f;
    // Send overlords to the nearest bases, and drone to base far away
    if (unit->flying()) {
      d = utils::distance(curPos, pos);
    } else {
      state->areaInfo().walkPath(curPos, pos, &d);
      d = -d;
    }
    if (frame < lastFrame || (frame == lastFrame && d < minDist)) {
      minDist = d;
      bestPos = pos;
      lastFrame = frame;
    }
  }
  return bestPos;
}

void ScoutingModule::updateLocations(
    State* state,
    std::unordered_map<Position, int>& locations,
    std::vector<Position> const& candidates) {
  if (locations.empty()) { // intialization
    for (auto pos : candidates) {
      locations.emplace(pos, -1);
    }
  }
  if (locations.size() < 2) {
    return;
  }
  for (auto task : state->board()->tasksOfModule(this)) {
    auto stask = std::static_pointer_cast<ScoutingTask>(task);
    auto it = locations.find(stask->location());
    if (it != locations.end()) {
      it->second = state->currentFrame();
    }
  }
  // clean up startingLocations
  for (auto it = locations.begin(); it != locations.end();) {
    if (std::find(candidates.begin(), candidates.end(), it->first) ==
        candidates.end()) {
      it = locations.erase(it);
    } else {
      ++it;
    }
  }
}

} // namespace cherrypi
