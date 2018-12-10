/* * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "squadtask.h"

#include "../upctocommand.h"
#include "commandtrackers.h"
#include "common/rand.h"
#include "movefilters.h"
#include "player.h"
#include "state.h"
#include "utils.h"

#include "bwem/bwem.h"

#include <functional>
#include <glog/logging.h>

namespace cherrypi {

void SquadTask::update(State* state) {
  removeDeadOrReassignedUnits(state);

  // Update the task status if no more units
  if (units().empty()) {
    VLOG(4) << "All units died or were reassigned. Marking task "
            << utils::upcString(upcId()) << " as failed";
    setStatus(TaskStatus::Failure);
    return;
  }

  // Remove all dead targets
  for (size_t i = 0; i < targets.size(); ++i) {
    if (targets[i]->dead) {
      std::swap(targets[i], targets.back());
      targets.pop_back();
      --i;
    }
  }

  // Update squad properties
  hasAirUnits = hasGroundUnits = false;
  for (const auto unit : units()) {
    if (unit->flying()) {
      hasAirUnits = true;
    } else {
      hasGroundUnits = true;
    }
    if (
        // TODO: Wraith + Cloak tech
        unit->type == buildtypes::Terran_Vulture_Spider_Mine ||
        unit->type == buildtypes::Zerg_Lurker ||
        unit->type == buildtypes::Zerg_Lurker_Egg ||
        unit->type == buildtypes::Protoss_Dark_Templar ||
        unit->type == buildtypes::Protoss_Arbiter) {
      hasCloakedFighters = true;
    }
  }

  // Units within 75 walktiles of the centroid are grouped
  // TODO Dynamic based on # of units in group
  center_ = utils::centerOfUnits(units());
  targets_ = getGroupTargets(state);
  threats_ = getGroupThreats(state);

  // If no more targets and we're not targeting a location, declare victory
  if (!targetingLocation && targets_.empty()) {
    VLOG(4) << "Squad for " << utils::upcString(upcId())
            << " has no more targets. Marking as succeeded";
    setStatus(TaskStatus::Success);
    return;
  }

  for (auto& bullet : state->tcstate()->frame->bullets) {
    if (bullet.type == tc::BW::BulletType::Psionic_Storm) {
      storms_.emplace_back(bullet.x, bullet.y);
    }
  }
}

// Gets the targets the group should attack
std::vector<Unit*> SquadTask::getGroupTargets(State* state) const {
  return targets;
}

// Gets all the threats to the group
std::vector<Unit*> SquadTask::getGroupThreats(State* state) const {
  std::vector<Unit*> ret;
  for (const auto unit : utils::findNearbyEnemyUnits(state, units())) {
    if (isThreat(unit)) {
      ret.push_back(std::move(unit));
    }
  }
  return ret;
}

/// Select which units are valid targets for this Squad
void SquadTask::pickTargets(State* state) {
  if (units().empty()) {
    return;
  }

  for (auto unit : units()) {
    auto& agent = agents_->at(unit);
    agent.legalTargets = targets_;
    agent.target = nullptr;
    agent.prevTargetInRange = agent.targetInRange;
    agent.targetInRange = false;
  }

  int latency = state->latencyFrames();

  struct sortedUnit {
    int targetsInRange = 0;
    Unit* unit = nullptr;
    size_t index = 0;
    bool hasTarget = false;
    bool operator<(const sortedUnit& n) const {
      return targetsInRange < n.targetsInRange;
    }
  };

  size_t i = 0;

  std::vector<sortedUnit> sortedUnits;
  sortedUnits.reserve(units().size());
  for (Unit* u : units()) {
    sortedUnits.push_back({0, u, i, false});
    ++i;
  }

  auto canAttack = [&](Unit* unit, Unit* target) {
    if (!unit->canAttack(target)) {
      return false;
    }
    if (unit->type == buildtypes::Zerg_Scourge) {
      if (target->type == buildtypes::Protoss_Interceptor) {
        return false;
      }
    }
    return true;
  };

  // Figure out how much damage we can deal to enemy units right now.
  std::vector<float> unitTargetDamageNow(units().size() * targets_.size());
  i = 0;
  for (Unit* target : targets_) {
    Vec2 targetPos = target->posf() + target->velocity() * latency;
    size_t offset = i;
    for (Unit* unit : units()) {
      if (!canAttack(unit, target)) {
        ++i;
        continue;
      }
      Vec2 unitPos = unit->posf() + unit->velocity() * latency;
      float range = unit->rangeAgainst(target) + DFOASG(0.25, 0.25);

      if (isIrrelevantTarget(target)) {
        // Pretend like we're never in range of irrelevant targets.
        range = -1;
      }

      float distance = utils::distanceBB(unit, unitPos, target, targetPos);
      float damage = 0.0f;
      if (distance <= range) {
        ++sortedUnits[i - offset].targetsInRange;
        float hpDamage = (float)unit->computeHPDamage(target);
        float shieldDamage = (float)unit->computeShieldDamage(target);
        damage = (hpDamage * target->unit.health +
                  shieldDamage * target->unit.shield) /
            (target->unit.health + target->unit.shield);
        damage /= unit->maxCdAgainst(target);
      }
      unitTargetDamageNow[i] = damage;
      ++i;
    }
  }
  std::sort(sortedUnits.begin(), sortedUnits.end());

  auto targetImportance = [&](Unit* target) {
    float r = 1.0f;
    if (target->constructing()) {
      r += DFOASG(0.75f, 0.5f);
    }
    if (target->repairing()) {
      r += DFOASG(1.5f, 0.75f);
    }
    if (!target->completed()) {
      r += DFOASG(0.5f, 0.5f);
    }
    if (isImportantTarget(target)) {
      r += DFOASG(1.5f, 0.75f);
    }
    if (isRelevantDetector(target)) {
      r += DFOASG(0.5f, 0.5f);
    }
    if (!isThreat(target)) {
      r /= 2.0f;
    }
    if (isIrrelevantTarget(target)) {
      r /= 128.0f;
    }
    if (target->type->isBuilding && target->type != buildtypes::Terran_Bunker &&
        !target->type->hasAirWeapon && !target->type->hasGroundWeapon) {
      r /= 64.0f;
    }
    return r;
  };

  auto unitTargetCompatibility = [&](Unit* unit, Unit* target) {
    float r = 1.0f;
    r += unit->damageMultiplier(target) - target->damageMultiplier(unit);
    if (target->type == buildtypes::Terran_Vulture) {
      if (unit->type == buildtypes::Zerg_Zergling ||
          unit->type == buildtypes::Protoss_Zealot) {
        r /= DFOASG(4.0f, 2.0f);
      }
    }
    if (unit->type == buildtypes::Zerg_Mutalisk &&
        (target->canAttack(unit) || isImportantTarget(target)) &&
        utils::distanceBB(unit, target) <= 4 * 6) {
      r += 1000.0f / utils::distance(unit, target);
    }
    if (!unit->canAttack(target)) {
      r /= 1000.0f;
    }
    return r;
  };

  struct sortedTarget {
    float score = 0.0f;
    size_t index = 0;
    bool dead = true;
    int health = 0;
    int shield = 0;
    int nAttacking = 0;
    float splitCounter = 0;
    bool operator<(const sortedTarget& n) const {
      return score < n.score;
    }
  };

  bool anyoneRepairing = false;
  bool anyMedics = false;
  for (Unit* target : targets_) {
    if (target->repairing()) {
      anyoneRepairing = true;
    }
    if (target->type == buildtypes::Terran_Medic) {
      anyMedics = true;
    }
  }

  // Sort targets by how fast we can kill them if we focus fire, weighted by how
  // important the target is.
  std::vector<sortedTarget> sortedTargets;
  sortedTargets.reserve(targets_.size());
  i = 0;
  for (Unit* target : targets_) {
    float incomingDamage = 0.0f;
    for (Unit* unit : units()) {
      (void)unit;
      incomingDamage += unitTargetDamageNow[i];
      ++i;
    }
    float ttl = (target->unit.health + target->unit.shield) / incomingDamage;
    float ti = targetImportance(target);
    if (ttl > DFOASG(24 * 3, 24 * 3) && ti < 0.25f) {
      ttl = kfInfty;
      for (Unit* unit : units()) {
        (void)unit;
        --i;
        unitTargetDamageNow[i] = 0;
      }
      i += units().size();
    }
    float score = ttl / ti;
    int health = target->unit.health + 2;
    if (anyoneRepairing && !target->type->isBiological) {
      health += 15;
    }
    if (anyMedics && target->type->isBiological) {
      health += 15;
    }
    sortedTargets.push_back(
        {score, i / units().size() - 1, false, health, target->unit.shield});
  }
  std::sort(sortedTargets.begin(), sortedTargets.end());

  // Allocate any targets that are currently in range, in the sorted order.
  for (auto& vTarget : sortedTargets) {
    if (vTarget.dead) {
      continue;
    }
    Unit* target = targets_.at(vTarget.index);
    size_t offset = units().size() * vTarget.index;
    for (auto& vUnit : sortedUnits) {
      if (vUnit.hasTarget) {
        continue;
      }
      Unit* unit = vUnit.unit;
      size_t index = offset + vUnit.index;
      if (unitTargetDamageNow.at(index)) {
        vUnit.hasTarget = true;
        auto& agent = agents_->at(unit);
        agent.target = target;
        agent.targetInRange = true;
        int hpDamage = 0;
        int shieldDamage = 0;
        unit->computeDamageTo(target, &hpDamage, &shieldDamage);
        auto ei = enemyStates_->find(target);
        if (ei != enemyStates_->end()) {
          ei->second.damages += hpDamage + shieldDamage;
        }
        vTarget.health -= hpDamage;
        vTarget.shield -= shieldDamage;
        ++vTarget.nAttacking;
        if (vTarget.health <= 0) {
          vTarget.dead = true;
          break;
        }
      }
    }
  }

  struct sortedPair {
    float score = kfInfty;
    sortedUnit* unit = nullptr;
    sortedTarget* target = nullptr;
    bool operator<(const sortedPair& n) const {
      return score < n.score;
    }
  };

  std::vector<sortedPair> pairScore(sortedUnits.size() * sortedTargets.size());

  // Give targets to any units that didn't have any targets in range.
  i = 0;
  for (auto& vTarget : sortedTargets) {
    Unit* target = targets_.at(vTarget.index);
    if (vTarget.dead) {
      for (auto& vUnit : sortedUnits) {
        pairScore[i] = {
            utils::distance(target, vUnit.unit) / 1e-4f, &vUnit, &vTarget};
        ++i;
      }
      continue;
    }
    for (auto& vUnit : sortedUnits) {
      Unit* unit = vUnit.unit;
      if (vUnit.hasTarget || !canAttack(unit, target)) {
        ++i;
        continue;
      }
      float score = DFOASG(8.0f, 4.0f) + utils::distance(target, unit) -
          unit->rangeAgainst(target);
      score /= targetImportance(target) * unitTargetCompatibility(unit, target);
      pairScore[i] = {score, &vUnit, &vTarget};
      ++i;
    }
  }

  std::sort(pairScore.begin(), pairScore.end());

  float targetSplit = (float)sortedTargets.size() / sortedUnits.size() *
      DFOASG(1.0f, 0.5f) / DFOASG(1.0f, 0.5f);

  auto shouldSplitAgainst = [&](Unit* unit, Unit* target) {
    if (target->velocity().x + target->velocity().y > DFOASG(0.15f, 0.15f)) {
      if (target->velocity().dot(target->posf() - unit->posf()) > 0) {
        return true;
      }
    }
    return !unit->flying() && !target->canAttack(unit) &&
        !target->type->isBuilding;
  };

  auto maxAttacking = [&](Unit* unit, Unit* target) {
    if (!unit->canAttack(target)) {
      return 0;
    }
    if (target->type->isBuilding || unit->flying()) {
      return 6;
    }
    float range = unit->rangeAgainst(target);
    if (range < 8) {
      float theirRange = target->rangeAgainst(unit);
      if (theirRange > 0) {
        return (int)(DFOASG(3, 1.5) * std::sqrt(theirRange / range));
      }
      return (int)DFOASG(3, 1.5);
    }
    return (int)DFOASG(6, 3);
  };

  for (auto& v : pairScore) {
    if (!v.unit || v.unit->hasTarget) {
      continue;
    }
    Unit* unit = v.unit->unit;
    Unit* target = targets_.at(v.target->index);
    bool split = shouldSplitAgainst(unit, target);
    if (!split) {
      if (v.target->nAttacking >= maxAttacking(unit, target)) {
        split = true;
      } else {
        v.target->splitCounter -= 1.0f;
        ++v.target->nAttacking;
      }
    }
    if (split) {
      if (v.target->splitCounter < 1.0f) {
        v.target->splitCounter += targetSplit;
        if (v.target->splitCounter < 1.0f) {
          continue;
        }
      }
      v.target->splitCounter -= 1.0f;
      ++v.target->nAttacking;
    }
    v.unit->hasTarget = true;
    agents_->at(unit).target = target;
  }

  for (auto& v : pairScore) {
    if (!v.unit || v.unit->hasTarget) {
      continue;
    }
    Unit* unit = v.unit->unit;
    Unit* target = targets_.at(v.target->index);
    v.unit->hasTarget = true;
    agents_->at(unit).target = target;
  }
}

namespace {

thread_local std::vector<uint8_t> visited;
thread_local uint8_t visitedN = 0;

template <typename callbackT>
const Tile* findNearbyTile(
    State* state,
    Position source,
    float maxDistance,
    callbackT&& callback) {
  const Tile* sourceTile = state->tilesInfo().tryGetTile(source.x, source.y);
  if (!sourceTile) {
    return sourceTile;
  }
  visited.resize(TilesInfo::tilesWidth * TilesInfo::tilesHeight);
  uint8_t visitedValue = ++visitedN;

  auto& tilesInfo = state->tilesInfo();
  auto* tilesData = tilesInfo.tiles.data();

  const int mapWidth = state->mapWidth();
  const int mapHeight = state->mapHeight();

  std::deque<const Tile*> open;
  open.push_back(sourceTile);
  visited[sourceTile - tilesData] = visitedValue;
  while (!open.empty()) {
    const Tile* tile = open.front();
    open.pop_front();

    if (tile->entirelyWalkable && !tile->building && callback(tile)) {
      return tile;
    }

    auto add = [&](const Tile* ntile) {
      if ((!ntile->entirelyWalkable || ntile->building) && tile != sourceTile) {
        return;
      }

      float sourceDistance =
          utils::distance(ntile->x, ntile->y, source.x, source.y);
      if (sourceDistance >= maxDistance) {
        return;
      }

      auto& v = visited[ntile - tilesData];
      if (v == visitedValue) {
        return;
      }
      v = visitedValue;
      open.push_back(ntile);
    };

    if (tile->x > 0) {
      add(tile - 1);
      if (tile->y > 0) {
        add(tile - 1 - TilesInfo::tilesWidth);
        add(tile - TilesInfo::tilesWidth);
      }
      if (tile->y < mapHeight - tc::BW::XYWalktilesPerBuildtile) {
        add(tile - 1 + TilesInfo::tilesHeight);
        add(tile + TilesInfo::tilesHeight);
      }
    } else {
      if (tile->y > 0) {
        add(tile - TilesInfo::tilesWidth);
      }
      if (tile->y < mapHeight - tc::BW::XYWalktilesPerBuildtile) {
        add(tile + TilesInfo::tilesHeight);
      }
    }
    if (tile->x < mapWidth - tc::BW::XYWalktilesPerBuildtile) {
      add(tile + 1);
      if (tile->y > 0) {
        add(tile + 1 - TilesInfo::tilesWidth);
      }
      if (tile->y < mapHeight - tc::BW::XYWalktilesPerBuildtile) {
        add(tile + 1 + TilesInfo::tilesHeight);
      }
    }
  }
  return (const Tile*)nullptr;
};
}

/// Calculate a combat formation position for all Agents
void SquadTask::formation(State* state) {
  for (auto unit : units()) {
    auto& agent = agents_->at(unit);
    agent.formationPosition = kInvalidPosition;
  }

  for (Unit* e : targets_) {
    for (Unit* u : state->unitsInfo().myUnits()) {
      if (u->inRangeOf(e, state->latencyFrames())) {
        return;
      }
    }
  }

  float nearestNearestThreatUnitDistance = kfInfty;
  Unit* nearestNearestThreat = nullptr;

  struct SortedUnit {
    float threatDistance = 0.0f;
    Unit* unit = nullptr;
    Unit* threat = nullptr;

    bool operator<(const SortedUnit& n) const {
      return threatDistance < n.threatDistance;
    }
  };

  std::vector<SortedUnit> sortedUnits;
  sortedUnits.reserve(squadUnits().size());

  for (Unit* u : squadUnits()) {
    Unit* nearestThreat = nullptr;
    float nearestThreatWeaponDistance = kfInfty;
    float nearestThreatUnitDistance = kfInfty;
    for (Unit* e : targets_) {
      if (isThreat(e)) {
        float d = utils::distance(u, e);
        float weaponDistance =
            std::max(d - std::max(e->unit.airRange, e->unit.groundRange), 0.0f);
        if (weaponDistance <= 4.0f * 8 &&
            weaponDistance < nearestThreatWeaponDistance) {
          nearestThreatWeaponDistance = weaponDistance;
          nearestThreatUnitDistance = d;
          nearestThreat = e;
        }
      }
    }
    if (nearestThreatUnitDistance < nearestNearestThreatUnitDistance) {
      nearestNearestThreatUnitDistance = nearestThreatUnitDistance;
      nearestNearestThreat = nearestThreat;
    }

    sortedUnits.push_back({nearestThreatUnitDistance, u, nearestThreat});
  }

  if (nearestNearestThreat && nearestNearestThreatUnitDistance <= 4 * 20) {
    float formationDistance = nearestNearestThreatUnitDistance;

    for (Unit* unit : units()) {
      auto& agent = agents_->at(unit);
      if (agent.formationCounter >= 8) {
        formationDistance -= 4.0f;
        for (Unit* unit2 : units()) {
          auto& agent2 = agents_->at(unit2);
          agent2.formationCounter = 0;
        }
        break;
      }
    }

    std::sort(sortedUnits.begin(), sortedUnits.end());

    std::vector<uint8_t> spotTaken(
        TilesInfo::tilesWidth * TilesInfo::tilesHeight);

    auto& tilesInfo = state->tilesInfo();
    auto* tilesData = tilesInfo.tiles.data();

    int inPosition = 0;
    int outOfPosition = 0;
    for (size_t i = 0; i != sortedUnits.size(); ++i) {
      auto& v = sortedUnits[i];

      Unit* unit = v.unit;
      Unit* threat = v.threat;
      if (threat &&
          (threat->velocity().length() < 0.1f ||
           threat->velocity().dot(unit->posf() - threat->posf()) > 0)) {
        auto& agent = agents_->at(v.unit);
        if (!unit->flying() && !agent.targetInRange &&
            (!agent.target || !agent.target->type->isWorker)) {
          int nSpots = 0;
          const Tile* bestTile = nullptr;
          int bestN = std::numeric_limits<int>::max();
          findNearbyTile(state, unit->pos(), 4.0f * 4, [&](const Tile* tile) {
            float d = utils::distance(Position(tile), threat->pos());
            if (d >= formationDistance && d < formationDistance + 4.0f) {
              ++nSpots;
              int n = spotTaken[tile - tilesData];
              if (n < bestN) {
                bestN = n;
                bestTile = tile;
              }
            }
            return nSpots >= 16;
          });
          if (bestTile) {
            Position targetPos = Position(bestTile) + Position(2, 2);
            if (utils::distance(unit, targetPos) <= 4.0f) {
              ++agent.formationCounter;
            }
            agent.formationPosition = targetPos;
            ++spotTaken[bestTile - tilesData];
          }
        }
      }
    }

    for (Unit* unit : units()) {
      auto& agent = agents_->at(unit);
      if (agent.formationPosition != kInvalidPosition) {
        bool anyoneThere = false;
        for (Unit* unit2 : units()) {
          if (!unit2->flying()) {
            if (utils::distance(unit2, agent.formationPosition) <= 4.0f) {
              anyoneThere = true;
              break;
            }
          }
        }
        if (anyoneThere) {
          ++inPosition;
        } else {
          ++outOfPosition;
        }
      }
    }

    if (inPosition >= outOfPosition * 3 && false) {
      for (Unit* unit : units()) {
        auto& agent = agents_->at(unit);
        agent.formationPosition = kInvalidPosition;
        agent.formationCounter = 0;
      }
    }
  }
}

/// Get micro decisions for all units
std::vector<std::shared_ptr<UPCTuple>> SquadTask::makeUPCs(State* state) {
  // This prevents a variety of issues caused by update()
  // being called out of sync with makeUPCs.
  // Maybe everything should just live in makeUPCs.
  update(state);

  VLOG(2) << "Squad " << utils::upcString(upcId()) << " of " << units().size()
          << " at (" << center_ << ")"
          << " to (" << targetX << ", " << targetY << ")"
          << " delete:" << delProb << " flee:" << fleeProb;
  VLOG(3) << "Units: " << utils::unitsString(units());
  VLOG(3) << "Targets: " << utils::unitsString(targets_);

  // Update all agents
  for (auto unit : units()) {
    auto& agent = agents_->at(unit);
    agent.state = state;
    agent.task = this;
    agent.unit = unit;
  }

  const auto weAreFighting = delProb > 0;

  // Choose targets
  pickTargets(state);

  formation(state);

  // Choose UPC actions
  std::vector<std::shared_ptr<UPCTuple>> upcs;
  for (auto unit : units()) {
    auto& agent = agents_->at(unit);
    auto upc = weAreFighting ? agent.microDelete() : agent.microFlee();
    if (upc != nullptr) {
      upcs.push_back(std::move(upc));
    }
  }

  return upcs;
}

/// Can we ignore this target?
bool SquadTask::isIrrelevantTarget(Unit const* u) const {
  return (
      (u->type == buildtypes::Zerg_Larva) || (u->type == buildtypes::Zerg_Egg));
}

/// Is this unit helping detect allied cloaked fighters?
bool SquadTask::isRelevantDetector(Unit const* u) const {
  return hasCloakedFighters &&
      (u->type->isDetector || u->type == buildtypes::Terran_Comsat_Station ||
       u->type == buildtypes::Terran_Vulture_Spider_Mine ||
       u->type == buildtypes::Zerg_Spore_Colony);
}

/// Can this unit hurt us?
bool SquadTask::isThreat(Unit const* u) const {
  return (
      isRelevantDetector(u) || (hasGroundUnits && u->type->hasGroundWeapon)
      // It could hold a Reaver!
      ||
      (hasGroundUnits && u->type == buildtypes::Protoss_Shuttle) ||
      (hasGroundUnits && u->type == buildtypes::Protoss_Reaver) ||
      (hasAirUnits && u->type->hasAirWeapon) ||
      (u->type == buildtypes::Terran_Bunker)
      // TODO: if we have biological units
      ||
      (u->type == buildtypes::Terran_Science_Vessel) ||
      (u->type == buildtypes::Protoss_High_Templar)
      // TODO: If we have biological
      // and/or expensive units
      // and/or casters with energy
      ||
      (u->type == buildtypes::Protoss_Dark_Archon) ||
      (u->type == buildtypes::Zerg_Defiler)
      // Consider Carriers
      );
}

/// Should we prioritze this target?
bool SquadTask::isImportantTarget(Unit const* u) const {
  return (u->type == buildtypes::Terran_Dropship ||
          u->type == buildtypes::Terran_Medic ||
          u->type == buildtypes::Terran_Siege_Tank_Siege_Mode ||
          u->type == buildtypes::Terran_Siege_Tank_Tank_Mode ||
          u->type == buildtypes::Terran_Science_Vessel ||
          u->type == buildtypes::Terran_Bunker ||
          u->type == buildtypes::Protoss_Carrier ||
          u->type == buildtypes::Protoss_Reaver ||
          u->type == buildtypes::Protoss_High_Templar ||
          u->type == buildtypes::Protoss_Dark_Templar ||
          u->type == buildtypes::Protoss_Shield_Battery) ||
      u->type == buildtypes::Protoss_Shuttle ||
      u->type == buildtypes::Protoss_Photon_Cannon ||
      u->type == buildtypes::Zerg_Sunken_Colony ||
      u->type == buildtypes::Zerg_Spore_Colony ||
      u->type == buildtypes::Zerg_Queen || u->type == buildtypes::Zerg_Defiler;
};

} // namespace cherrypi
