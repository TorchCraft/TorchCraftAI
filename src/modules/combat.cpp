/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "combat.h"

#include "commandtrackers.h"
#include "player.h"
#include "state.h"
#include "utils.h"

#include "bwem/bwem.h"

#include <glog/logging.h>

namespace cherrypi {

REGISTER_SUBCLASS_0(Module, CombatModule);

namespace {

/**
 * Stores information about a sets of unit engaging in an attack or defend
 * action.
 *
 * This task does not allocate the units in the squad itself (as they can belong
 * to one task only). The actual allocation happens in downstream
 * micro-management tasks, and this task returns the units via proxiedUnits()
 * instead.
 */
class SquadTask : public MultiProxyTask {
 public:
  struct Target {
    int x;
    int y;
    Unit* unit; // optional unit ID

    Target(int x, int y, Unit* unit) : x(x), y(y), unit(unit) {}
  };

  std::unordered_set<Unit*> squadUnits;
  std::vector<Target> targets;
  std::unordered_set<Unit*> nearbyEnemies;
  std::shared_ptr<Tracker> moveTracker;
  std::shared_ptr<Tracker> attackTracker;
  bool moving = false;
  bool fighting = false;
  bool hasAirUnits = false;
  bool hasGroundUnits = false;

  SquadTask(
      int upcId,
      std::unordered_set<Unit*> units,
      std::vector<Target> targets)
      : MultiProxyTask({}, upcId),
        squadUnits(std::move(units)),
        targets(std::move(targets)) {}

  void setSquadUpcs(std::vector<int> upcs) {
    // Reset proxied tasks
    MultiProxyTask::targets_.clear();
    MultiProxyTask::targets_.insert(
        MultiProxyTask::targets_.begin(), upcs.size(), nullptr);
    MultiProxyTask::targetUpcIds_ = std::move(upcs);
  }

  void update(State* state) override {
    MultiProxyTask::update(state);
    squadUnits = proxiedUnits();

    // Set to failed if there are no more units to take care of.
    if (squadUnits.empty()) {
      VLOG(1) << "Squad for " << utils::upcString(upcId())
              << " has no more units, marking as failed";
      setStatus(TaskStatus::Failure);
      return;
    }

    // Update target list
    updateTargets(state);
    if (targets.empty()) {
      VLOG(1) << "Squad for " << utils::upcString(upcId())
              << " has no more targets, marking as succeeded";
      setStatus(TaskStatus::Success);
      return;
    }

    hasAirUnits =
        std::find_if(squadUnits.begin(), squadUnits.end(), [](Unit const* u) {
          return u->type->isFlyer;
        }) != squadUnits.end();
    hasGroundUnits =
        std::find_if(squadUnits.begin(), squadUnits.end(), [](Unit const* u) {
          return !u->type->isFlyer;
        }) != squadUnits.end();

    moving = moveTracker &&
        (moveTracker->status() == TrackerStatus::Pending ||
         moveTracker->status() == TrackerStatus::Ongoing);
    fighting = attackTracker &&
        (attackTracker->status() == TrackerStatus::Pending ||
         attackTracker->status() == TrackerStatus::Ongoing);
    VLOG(2) << "squad update: moving = " << moving
            << ", fighting = " << fighting;
  }

  void updateTargets(State* state) {
    for (size_t i = 0; i < targets.size(); i++) {
      if (targets[i].unit == nullptr) {
        continue;
      }
      if (targets[i].unit->dead) {
        std::swap(targets[i], targets.back());
        targets.pop_back();
        --i;
        continue;
      }
      targets[i].x = targets[i].unit->x;
      targets[i].y = targets[i].unit->y;
    }
  }

  // Returns location of preferred target
  Position targetLocation() const {
    if (targets.size() == 1) {
      return {targets[0].x, targets[0].y};
    }
    auto center = utils::centerOfUnits(squadUnits);
    auto it =
        utils::getClosest(center.x, center.y, targets.begin(), targets.end());
    if (it != targets.end()) {
      return Position(it->x, it->y);
    }
    return center;
  }

  std::unordered_set<Unit*> findNearbyEnemyUnits(State* state) const {
    auto& enemyUnits = state->unitsInfo().enemyUnits();
    std::unordered_set<Unit*> nearby;
    for (auto unit : squadUnits) {
      // 400 from UAlbertaBot
      auto range = 400 / tc::BW::XYPixelsPerWalktile;
      for (auto enemy :
           utils::filterUnitsByDistance(enemyUnits, unit->x, unit->y, range)) {
        // XXX What if it's gone??
        if (!enemy->dead && !enemy->gone) {
          nearby.insert(enemy);
        }
      }
    }
    return nearby;
  }

  // Threats: all units that can attack units from our squad composition
  // Non-threats: (opposite), statically all non-attacking buildings
  bool isThreat(Unit const* enemy) const {
    if (hasAirUnits && enemy->type->hasAirWeapon) {
      return true;
    }
    if (hasGroundUnits && enemy->type->hasGroundWeapon) {
      return true;
    }
    return false;
  }

  std::vector<std::shared_ptr<UPCTuple>> targetNewEnemies(
      State* state,
      std::unordered_set<Unit*> const& enemies) const {
    // At this point, we just prioritize threats vs. non-threats.
    // Further prioritization is done at the unit-level.
    std::unordered_set<Unit*> threats;
    std::unordered_set<Unit*> nonThreats;
    for (Unit* enemy : enemies) {
      if (isThreat(enemy)) {
        threats.insert(enemy);
      } else {
        nonThreats.insert(enemy);
      }
    }

    std::vector<std::shared_ptr<UPCTuple>> upcs;
    for (Unit* unit : squadUnits) {
      auto upc = std::make_shared<UPCTuple>();
      UPCTuple::UnitMap map;
      for (Unit* threat : threats) {
        map[threat] = 1.0;
      }
      for (Unit* nonThreat : nonThreats) {
        map[nonThreat] = 0.5;
      }

      upc->unit[unit] = 1;
      upc->position = std::move(map);
      upc->command[Command::Delete] = 1;
      upcs.emplace_back(std::move(upc));
    }
    return upcs;
  }
};

} // namespace

void CombatModule::step(State* state) {
  auto board = state->board();

  // Form new squads based on new UPCs
  auto myUpcs = board->upcsFrom(this);
  for (auto& upcs : board->upcsWithCommand(Command::Delete, 0.5)) {
    if (myUpcs.find(upcs.first) != myUpcs.end()) {
      continue;
    }
    if (upcs.second->unit.empty()) {
      continue;
    }
    if (formNewSquad(state, upcs.second, upcs.first)) {
      board->consumeUPCs({upcs.first}, this);
    }
  }

  // Update existing squads
  for (auto& task : board->tasksOfModule(this)) {
    updateTask(state, task);
  }
}

bool CombatModule::formNewSquad(
    State* state,
    std::shared_ptr<UPCTuple> sourceUpc,
    int sourceUpcId) {
  // Form a squad task with all units with non-zero probability
  std::unordered_set<Unit*> units;
  for (auto& uprob : sourceUpc->unit) {
    if (uprob.second > 0) {
      units.insert(uprob.first);
    }
  }
  if (units.empty()) {
    VLOG(1) << "No units to take care of in " << utils::upcString(sourceUpcId);
    return false;
  }

  std::vector<SquadTask::Target> targets;
  if (sourceUpc->position.is<UPCTuple::UnitMap>()) {
    for (auto it : sourceUpc->position.get_unchecked<UPCTuple::UnitMap>()) {
      if (it.second > 0) {
        targets.emplace_back(it.first->x, it.first->y, it.first);
      }
    }
    VLOG(2) << "Targetting " << targets.size() << " units";
  } else if (sourceUpc->position.is<Position>()) {
    auto pos = sourceUpc->position.get_unchecked<Position>();
    targets.emplace_back(
        pos.x * sourceUpc->scale, pos.y * sourceUpc->scale, nullptr);
    VLOG(2) << "Targeting single position at " << targets.back().x << ","
            << targets.back().y;
  } else if (sourceUpc->position.is<torch::Tensor>()) {
    auto argmax = utils::argmax(
        sourceUpc->position.get_unchecked<torch::Tensor>(), sourceUpc->scale);
    targets.emplace_back(std::get<0>(argmax), std::get<1>(argmax), nullptr);
    VLOG(2) << "Targeting position argmax at " << targets.back().x << ","
            << targets.back().y;
  } else {
    VLOG(0) << "No targets to attack in " << utils::upcString(sourceUpcId);
    return false;
  }

  size_t numUnits = units.size();
  auto task =
      std::make_shared<SquadTask>(sourceUpcId, units, std::move(targets));
  state->board()->postTask(task, this);
  task->setStatus(TaskStatus::Unknown);

  VLOG(1) << "Formed squad for " << utils::upcString(sourceUpcId) << " with "
          << numUnits << " units";
  VLOG(1) << "Now managing " << state->board()->tasksOfModule(this).size()
          << " squads";
  return true;
}

void CombatModule::updateTask(State* state, std::shared_ptr<Task> task) {
  auto squad = std::static_pointer_cast<SquadTask>(task);
  auto board = state->board();

  if (squad->status() == TaskStatus::Success) {
    VLOG(2) << "Squad for " << utils::upcString(squad->upcId())
            << " has succeeded";
    board->markTaskForRemoval(squad);
    return;
  } else if (squad->status() == TaskStatus::Failure) {
    VLOG(2) << "Squad for " << utils::upcString(squad->upcId())
            << " has failed";
    board->markTaskForRemoval(squad);
    return;
  } else if (squad->status() == TaskStatus::Cancelled) {
    VLOG(2) << "Squad for UPC " << squad->upcId() << " has been cancelled";
    board->markTaskForRemoval(squad);
    return;
  }

  // Squad is moving. Check for nearby enemies
  auto nearbyEnemies = squad->findNearbyEnemyUnits(state);
  if (nearbyEnemies != squad->nearbyEnemies) {
    squad->nearbyEnemies = nearbyEnemies;
    if (!nearbyEnemies.empty()) {
      // Cancel trackers, we're now targetting new enemies
      if (squad->moveTracker) {
        squad->moveTracker->cancel();
      }
      squad->moving = false;
      if (squad->attackTracker) {
        squad->attackTracker->cancel();
      }

      VLOG(1) << "Fight against " << nearbyEnemies.size() << " nearby enemies";

      auto upcs = squad->targetNewEnemies(state, nearbyEnemies);
      std::vector<int> upcIds;
      for (auto upc : upcs) {
        auto id = board->postUPC(std::move(upc), squad->upcId(), this);
        upcIds.push_back(id);
      }
      upcs.clear();

      squad->attackTracker =
          state->addTracker<AttackTracker>(squad->squadUnits, nearbyEnemies);
      squad->fighting = true;
      squad->setSquadUpcs(std::move(upcIds));
    } else {
      VLOG(1) << "No more nearbyEnemies, fighting status is "
              << squad->fighting;
      squad->fighting = false;
    }
  }

  if (!squad->fighting && !squad->moving) {
    // Let's move to the primary location
    auto target = squad->targetLocation();
// NOTE: Moving via choke points only makes sense if we detect that units
// have reached the target location and/or we maintain the list of choke
// points.
#if 0
    auto map = state->map();
    auto current = utils::centerOfUnits(squad->squadUnits);
    int pathLength;
    auto path = map->GetPath(
        BWAPI::Position(BWAPI::WalkPosition(current.first, current.second)),
        BWAPI::Position(BWAPI::WalkPosition(target.first, target.second)),
        &pathLength);
    if (path.empty()) {
      if (pathLength < 0) {
        VLOG(0) << "No path from unit center " << current.first << ","
                  << current.second << " to target location " << target.first << ","
                  << target.second;
        squad->setStatus(TaskStatus::Failure);
        board->markTaskForRemoval(squad);
        return;
      }
      // Otherwise, go directly to target location
    } else {
      int i = 0;
      auto nextChokePoint = path[i++]->Center();
      while (i < path.size() && utils::distance(current.first, current.second, nextChokePoint.x, nextChokePoint.y) < 8.0f) {
        nextChokePoint = path[i++]->Center();
      }
      target.first = nextChokePoint.x;
      target.second = nextChokePoint.y;
    }
#endif

    std::vector<int> upcIds;
    for (auto& unit : squad->squadUnits) {
      auto upc = std::make_shared<UPCTuple>();
      upc->position = target;
      upc->unit[unit] = 1;
      upc->command[Command::Delete] = 0.5;
      upc->command[Command::Move] = 0.5;
      auto id = board->postUPC(std::move(upc), squad->upcId(), this);
      upcIds.push_back(id);
    }

    if (squad->moveTracker) {
      squad->moveTracker->cancel();
    }
    squad->moveTracker = state->addTracker<MovementTracker>(
        squad->squadUnits, target.x, target.y);
    squad->moving = true;
    squad->setSquadUpcs(std::move(upcIds));
  }
}

} // namespace cherrypi
