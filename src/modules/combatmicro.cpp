/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "combatmicro.h"

#include "blackboard.h"
#include "common/rand.h"
#include "state.h"
#include "task.h"
#include "utils.h"

#include <glog/logging.h>

namespace cherrypi {

REGISTER_SUBCLASS_0(Module, CombatMicroModule);

namespace {

int kSoftOverkill = 10; // TODO change

/**
 * A per-unit task for combat micro-management
 */
class MicroTask : public Task {
 public:
  std::shared_ptr<UPCTuple> upc;
  // Shorthand to avoid using the units set (which contains just this unit).
  Unit* unit = nullptr;
  Unit* newTargetCandidate = nullptr;
  Unit* oorTarget = nullptr; // Out of range
  Unit* currentTarget = nullptr;
  bool kiting = false;
  int lastCommandFrame = -100;

  MicroTask(int upcId, Unit* unit) : Task(upcId, {unit}), unit(unit) {}

  void update(State* state) override {
    removeDeadOrReassignedUnits(state);
    if (units().empty()) {
      VLOG(1) << utils::unitString(unit)
              << " died or was reassigned, marking task "
              << utils::upcString(upcId()) << " as failed";
      setStatus(TaskStatus::Failure);
      return;
    }

    if (currentTarget != nullptr && currentTarget->dead) {
      currentTarget = nullptr;
    }
  }

  void copyTargetsFrom(MicroTask* other) {
    newTargetCandidate = other->newTargetCandidate;
    oorTarget = other->oorTarget;
    currentTarget = other->currentTarget;
  }

  Unit* targetInRange() const {
    // TODO: This is tailored to ground units
    if (newTargetCandidate && newTargetCandidate->inRangeOf(unit)) {
      return newTargetCandidate;
    }
    if (oorTarget && oorTarget->inRangeOf(unit)) {
      return oorTarget;
    }
    return nullptr;
  }

  Unit* closestTarget() const {
    if (newTargetCandidate == nullptr) {
      return oorTarget;
    } else if (oorTarget == nullptr) {
      return newTargetCandidate;
    }

    if (utils::distance(unit, newTargetCandidate) <
        utils::distance(unit, oorTarget)) {
      return newTargetCandidate;
    }
    return oorTarget;
  }

  std::shared_ptr<UPCTuple> makeUpc(State* state) {
    auto target = currentTarget ? currentTarget : oorTarget;

    // If we can kite, do some kiting
    if (unit->canKite(target)) {
      kiting = true;
      return makeKitingUpc(state, target);
    }
    kiting = false;

    auto frame = state->currentFrame();
    if (upc->commandProb(Command::Move) > 0 && !target && !kiting) {
      // If target should move and isn't attacking or kiting, move it.
      lastCommandFrame = frame;
      return utils::makeSharpUPC(*upc, unit, Command::Move);
    }

    if (!target) {
      VLOG(2) << "Nothing to do for " << utils::unitString(unit)
              << " from task " << utils::upcString(upcId())
              << "; set status to success";
      setStatus(TaskStatus::Success);
      return nullptr;
    }
    if (target) {
      lastCommandFrame = frame;
      if (target->inRangeOf(unit)) {
        return utils::makeSharpUPC(unit, target, Command::Delete);
      } else {
        return utils::makeSharpUPC(unit, target, Command::Move);
      }
    }
    return nullptr;
  }

  std::shared_ptr<UPCTuple> makeKitingUpc(State* state, Unit* target) const {
    auto dist = utils::distance(unit, target);
    auto wrange =
        target->flying() ? unit->unit.airRange : unit->unit.groundRange;
    auto cd = target->flying() ? unit->unit.airCD : unit->unit.groundCD;
    if (cd == 0 && !target->gone && target->inRangeOf(unit) &&
        unit->atTopSpeed()) {
      // Attack
      return utils::makeSharpUPC(unit, target, Command::Delete);
    }

    auto cond = !unit->atTopSpeed() ||
        std::max(
            0.0,
            (double(dist) - wrange) * tc::BW::XYPixelsPerWalktile /
                unit->topSpeed) < cd;
    if (cond || target->gone) {
      if (unit->threateningEnemies.size() == 0) {
        // Hover around unit if not being attacked
        auto angle = common::Rand::rand() % 20 + 85;
        angle *= common::Rand::rand() % 2 ? 1 : -1;
        auto fleePos = utils::getMovePos(state, unit, target, angle, false);
        return utils::makeSharpUPC(unit, std::move(fleePos), Command::Move);
      } else {
        // Flee otherwise
        auto center = utils::centerOfUnits(unit->threateningEnemies);
        auto fleePos = utils::getMovePos(state, unit, center, 180, false);
        return utils::makeSharpUPC(unit, std::move(fleePos), Command::Move);
      }
    } else {
      auto fleePos = utils::getMovePos(state, unit, target, 0, false);
      return utils::makeSharpUPC(unit, std::move(fleePos), Command::Move);
    }
  }
};

} // namespace

void CombatMicroModule::step(State* state) {
  auto board = state->board();

  // Incorporate any new UPCs into the current set of tasks
  for (auto const& upct : board->upcsWithSharpCommand(Command::Delete)) {
    if (upct.second->unit.size() != 1) {
      continue;
    }
    consumeUPC(state, upct.first, upct.second);
  }
  for (auto const& upct : board->upcsWithCommand(Command::Move, 0.5)) {
    if (upct.second->unit.size() != 1) {
      continue;
    }
    if (upct.second->commandProb(Command::Delete) < 0.5) {
      continue;
    }
    consumeUPC(state, upct.first, upct.second);
  }

  updateTasks(state);
}

void CombatMicroModule::consumeUPC(
    State* state,
    int upcId,
    std::shared_ptr<UPCTuple> upc) {
  auto board = state->board();
  Unit* unit = upc->unit.begin()->first;
  auto task = std::make_shared<MicroTask>(upcId, unit);
  task->upc = upc;
  auto prevTask = board->taskWithUnitOfModule(unit, this);
  if (prevTask) {
    assert(std::dynamic_pointer_cast<MicroTask>(prevTask) != nullptr);
    task->copyTargetsFrom(std::static_pointer_cast<MicroTask>(prevTask).get());
    board->markTaskForRemoval(prevTask);
    prevTask->setStatus(TaskStatus::Failure);
  }
  board->consumeUPCs({upcId}, this);
  board->postTask(std::move(task), this, true);
}

void CombatMicroModule::updateTasks(State* state) {
  auto board = state->board();
  std::unordered_map<Unit*, HealthInfo> targetHealth;

  for (auto task : board->tasksOfModule(this)) {
    if (task->finished()) {
      continue;
    }

    auto mtask = std::static_pointer_cast<MicroTask>(task);
    mtask->setStatus(TaskStatus::Ongoing);

    bool needsUpdate = mtask->currentTarget == nullptr ||
        (state->currentFrame() - mtask->lastCommandFrame > 12);
    if (needsUpdate || mtask->kiting) {
      updateTarget(task, &targetHealth);
    }

    auto upc = mtask->makeUpc(state);
    if (upc) {
      board->postUPC(std::move(upc), task->upcId(), this);
      VLOG(2) << "Posted micro UPC for " << utils::unitString(mtask->unit)
              << " from task " << utils::upcString(mtask->upcId());
    }
  }
}

inline double EHPScoreHeuristic(Unit const* me, Unit const* o) {
  return o->type->gScore / me->computeEHP(o);
}

inline double HPScoreHeuristic(Unit const* me, Unit const* o) {
  // HP is almost always more useful than shield
  return -(o->unit.health * 1.3 + o->unit.shield);
}

inline double UnityScoreHeuristic(Unit const* me, Unit const* o) {
  // Essentially defaults to distance as below
  return 1;
}

// Assign units greedily to the targets in their respective UPCs, while taking
// overkill into account. We'll simply sort the targets in the UPC by
// probability, and then assign a primary and secondary target to each unit. For
// targets that are within firing range, we'll also try to avoid overkill.
void CombatMicroModule::updateTarget(
    std::shared_ptr<Task> task,
    std::unordered_map<Unit*, HealthInfo>* targetHealth) {
  auto mtask = std::static_pointer_cast<MicroTask>(task);
  auto unit = mtask->unit;
  auto& upc = *(mtask->upc.get());
  auto heuristic = EHPScoreHeuristic;

  // TODO Fix this:
  // Always target scourges, since they are very high threat. Should make the
  // actually add their potential damage to you in the targetting heuristic
  for (auto other : unit->threateningEnemies) {
    if (other->type == buildtypes::Zerg_Scourge) {
      mtask->currentTarget = other;
      return;
    }
  }

  // Order targets by probability and use distance from this unit as a
  // tie-breaker.
  std::vector<Unit*> sortedTargets;
  if (upc.position.is<UPCTuple::UnitMap>()) {
    for (auto& pair : upc.position.get_unchecked<UPCTuple::UnitMap>()) {
      if (!pair.first->dead) {
        sortedTargets.emplace_back(pair.first);
      }
    }
  }

  if (sortedTargets.empty()) {
    VLOG(2) << "No targets for " << utils::unitString(mtask->unit)
            << " in task " << utils::upcString(mtask->upcId());
    mtask->newTargetCandidate = nullptr;
    mtask->oorTarget = nullptr;
    return;
  }

  std::sort(sortedTargets.begin(), sortedTargets.end(), [&](Unit* a, Unit* b) {
    auto& map = upc.position.get<UPCTuple::UnitMap>();
    auto itA = map.find(a);
    auto itB = map.find(b);
    // Higher probabilities should end up first in target list
    if (itA->second != itB->second) {
      return itA->second > itB->second;
    }
    // Attack units with highest (value / your_hit) first
    auto aScore = heuristic(unit, a);
    auto bScore = heuristic(unit, b);
    if (aScore != bScore) {
      return aScore > bScore;
    }
    // Otherwise: use distance
    return utils::distance(unit, a) < utils::distance(unit, b);
  });

  // Fill in missing enemy health information
  for (auto target : sortedTargets) {
    // TODO 2: take in-air bullets into account
    if (targetHealth->find(target) == targetHealth->end()) {
      targetHealth->emplace(
          std::make_pair(
              target, HealthInfo{target->unit.health, target->unit.shield}));
    }
  }
  if (mtask->currentTarget &&
      targetHealth->find(mtask->currentTarget) == targetHealth->end()) {
    targetHealth->emplace(
        std::make_pair(
            mtask->currentTarget,
            HealthInfo{mtask->currentTarget->unit.health,
                       mtask->currentTarget->unit.shield}));
  }

  mtask->newTargetCandidate = nullptr; // the one we'll attack
  mtask->oorTarget = nullptr; // the one we'll have as a goal
  if (mtask->currentTarget != nullptr &&
      targetHealth->find(mtask->currentTarget)->second.hp <= -kSoftOverkill &&
      mtask->currentTarget->inRangeOf(mtask->unit) &&
      std::find(
          sortedTargets.begin(), sortedTargets.end(), mtask->currentTarget) !=
          sortedTargets.end()) {
    mtask->newTargetCandidate = mtask->currentTarget; // the one we'll attack
  }

  for (Unit* target : sortedTargets) {
    if (mtask->newTargetCandidate && mtask->oorTarget) {
      break;
    }

    // Restrict primary targets to targets of high importance.
    // Note: inRangeOf() will also do some quick checks if the attack is
    // possible (i.e. air units require an air weapon).
    if (mtask->newTargetCandidate == nullptr &&
        upc.position.get<UPCTuple::UnitMap>()[target] >
            0.9f) { // set the primary
      // Check for overkill
      auto health = targetHealth->find(target);
      if (health == targetHealth->end()) {
        //' Should not happen
        LOG(WARNING) << "Missing target health entry for unit "
                     << utils::unitString(target);
        continue;
      }
      if (health->second.hp <= -kSoftOverkill) {
        VLOG(3) << "Skipping unit " << utils::unitString(target)
                << " to avoid overkill";
        continue;
      }

      // "Mark" this unit as being attacked by subtracting the expected damage
      // from its health entry.
      mtask->newTargetCandidate = target;
      VLOG(2) << "Target candidate for " << utils::unitString(mtask->unit)
              << " from " << utils::upcString(mtask->upcId()) << ": "
              << utils::unitString(target);

    } else if (
        mtask->oorTarget == nullptr &&
        !target->inRangeOf(mtask->unit)) { // set the secondary
      mtask->oorTarget = target;
      VLOG(2) << "OOR target for " << utils::unitString(mtask->unit) << " from "
              << utils::upcString(mtask->upcId()) << ": "
              << utils::unitString(target);
    }
  }
  assert(
      mtask->oorTarget == nullptr || !mtask->oorTarget->inRangeOf(mtask->unit));

  // If we didn't find an important target in range, simply pick the first one
  // from the list.
  if (mtask->newTargetCandidate == nullptr) {
    for (Unit* target : sortedTargets) {
      if (target->inRangeOf(mtask->unit)) {
        mtask->newTargetCandidate = target;
        VLOG(2) << "Did not find important target for "
                << utils::unitString(mtask->unit) << " from "
                << utils::upcString(mtask->upcId()) << ". Choosing "
                << utils::unitString(target);
        break;
      }
    }
  }

  mtask->currentTarget = mtask->newTargetCandidate;
  if (mtask->currentTarget != nullptr) {
    int hpDamage, shieldDamage;
    auto health = targetHealth->find(mtask->currentTarget);
    unit->computeDamageTo(
        mtask->currentTarget, &hpDamage, &shieldDamage, health->second.shield);
    health->second.hp -= hpDamage;
    health->second.shield -= shieldDamage;
  }
}

} // namespace cherrypi
