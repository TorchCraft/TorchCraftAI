/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "tactics.h"

#include "combatsim.h"
#include "player.h"
#include "utils.h"

#include <bwem/map.h>
#include <deque>
#include <glog/logging.h>
#include <memory>
#include <vector>

DEFINE_uint64(
    tactics_fight_or_flee_interval,
    3,
    "How often between fight or flee computations");

DEFINE_double(
    relative_vs_absolute,
    0.75,
    "1 is all relative, 0 is all absolute");
DEFINE_double(q_val, 0.2, "1 is all damage, 0 is all death");
namespace cherrypi {

REGISTER_SUBCLASS_0(Module, TacticsModule);

namespace {

// A group where all enemy units are greater than distance away
// from one of our resource depots will have isAggressiveGroup set.
float constexpr kAggressiveGroupDistance = 4 * 14;
// To avoid spending cpu searching the entire map, this is a limit to how far
// away we will search for a position to flee to.
float constexpr kMaxFleeSearchDistance = 4 * 20;
// Any unit within this distance of an enemy unit (and vice versa) will be
// included in the combat simulation to determine fight or flight.
float constexpr kNearbyUnitDistance = 4 * 30;
uint16_t constexpr kDefaultFleeScore = 0xffff;

template <typename Units>
double scoreTeam(Units&& units) {
  double score = 0.0;
  for (Unit* u : units) {
    if (u->type->isNonUsable || u->type == buildtypes::Zerg_Overlord) {
      continue;
    }
    score += u->type->gScore;
    if (u->type->isBuilding &&
        (u->type->hasGroundWeapon || u->type->hasAirWeapon)) {
      score += u->type->gScore;
    }
    if (u->type == buildtypes::Terran_Bunker) {
      score += u->type->gScore * 3;
    }
  }
  return score;
}

template <typename OurUnits, typename TheirUnits>
double winRatio(OurUnits&& ourUnits, TheirUnits&& theirUnits) {
  double ourMult = 0.01f;
  double theirMult = 0.01f;
  int n = 0;
  for (Unit* a : ourUnits) {
    if (a->type->isNonUsable || a->type == buildtypes::Zerg_Overlord) {
      continue;
    }
    for (Unit* b : theirUnits) {
      if (b->type->isNonUsable || b->type == buildtypes::Zerg_Overlord) {
        continue;
      }
      if (a->canAttack(b))
        ourMult += 1.0;
      else
        ourMult += 0.15;
      if (b->canAttack(a))
        theirMult += 1.0;
      else
        theirMult += 0.15;
    }
  }
  ourMult /= std::max(n, 1);
  theirMult /= std::max(n, 1);
  return scoreTeam(ourUnits) * ourMult / (scoreTeam(theirUnits) * theirMult);
}

void updateFleeScore(State* state, std::vector<uint16_t>& fleeScore) {
  std::fill(fleeScore.begin(), fleeScore.end(), kDefaultFleeScore);

  auto& tilesInfo = state->tilesInfo();
  auto* tilesData = tilesInfo.tiles.data();

  const int mapWidth = state->mapWidth();
  const int mapHeight = state->mapHeight();

  struct OpenNode {
    const Tile* tile;
    uint16_t distance;
  };

  std::deque<OpenNode> open;
  for (Unit* u : state->unitsInfo().myResourceDepots()) {
    auto* tile = tilesInfo.tryGetTile(u->x, u->y);
    if (tile) {
      open.push_back({tile, 1});
      fleeScore.at(tile - tilesData) = 0;
    }
  }
  while (!open.empty()) {
    OpenNode curNode = open.front();
    open.pop_front();

    auto add = [&](const Tile* ntile) {
      if (!curNode.tile->entirelyWalkable) {
        return;
      }

      auto& v = fleeScore[ntile - tilesData];
      if (v != kDefaultFleeScore) {
        return;
      }
      v = curNode.distance;
      open.push_back({ntile, (uint16_t)(curNode.distance + 1)});
    };

    const Tile* tile = curNode.tile;

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
}

double convertSimToScore(
    CombatSim sim,
    std::vector<Unit*> allies,
    std::vector<Unit*> enemies) {
  if (allies.empty()) {
    return -1.0;
  } else if (enemies.empty()) {
    return 1.0;
  }

  auto calcValue = [&](const BuildType* type) { return type->subjectiveValue; };

  auto calcTeamValue = [&](const std::vector<Unit*> units) {
    double value = 0;
    for (auto const& u : units) {
      value += calcValue(u->type);
    }
    return value;
  };

  auto calcUnitScore = [&](Unit* su, CombatSim::SimUnit& eu, double q) {
    double startHS = su->unit.health + su->unit.shield;
    double endHS = eu.hp + eu.shields;
    // Have also tried comparing the damage sustained to maximum health for
    // the unit
    auto damageFraction = (startHS - endHS) / startHS;
    auto death = eu.hp <= 0 ? 1.0 : 0.0;

    auto painScore =
        calcValue(eu.type) * (q * damageFraction + (1.0 - q) * death);
    return painScore;
  };

  auto calcTeamScore = [&](
      std::vector<Unit*>& startUnits,
      std::vector<CombatSim::SimUnit>& endUnits,
      double q) {
    double teamScore = 0;
    for (auto i = 0u; i < startUnits.size(); i++) {
      teamScore += calcUnitScore(startUnits[i], endUnits[i], q);
    }
    return teamScore;
  };

  double myTeamValue = calcTeamValue(allies);
  double nmyTeamValue = calcTeamValue(enemies);
  double myPain = calcTeamScore(allies, sim.teams[0].units, FLAGS_q_val);
  double enemyPain = calcTeamScore(enemies, sim.teams[1].units, FLAGS_q_val);
  double relativePain = (myPain / myTeamValue) - (enemyPain / nmyTeamValue);
  double absolutePain = 2 * myPain / (myPain + enemyPain) - 1;
  if (std::isnan(relativePain)) {
    relativePain = 0.0;
  }
  if (std::isnan(absolutePain)) {
    absolutePain = 0.0;
  }
  auto rva = FLAGS_relative_vs_absolute;
  // Have also tried different damage calculations including scaling
  // damage by relative value of teams
  auto damageScore = rva * relativePain + (1 - rva) * absolutePain;
  // Note that we invert because a positive damageScore means that we
  // take more damage than the enemy, but we want a positive final score
  // to mean that we should fight (the inverse of the damageScore meaning)
  return -1 * damageScore;
}

} // namespace

void TacticsState::addEnemyUnitToGroup(
    State* state,
    Unit* u,
    TacticsGroup* group,
    std::vector<uint8_t> const& inBaseArea) {
  int frame = state->currentFrame();
  group->enemyUnits.push_back(u);
  if (!group->targetUnit ||
      u->type->mineralCost + u->type->gasCost >
          group->targetUnit->type->mineralCost +
              group->targetUnit->type->gasCost) {
    group->targetUnit = u;
    group->targetPos = Position(u->x, u->y);
  }
  if (u->flying()) {
    group->hasEnemyAirUnits = true;
  } else {
    group->hasEnemyGroundUnits = true;
  }
  if (u->type->isWorker) {
    group->hasEnemyWorkers = true;
  } else {
    group->enemyIsOnlyWorkers = false;
  }
  if (u->type->isBuilding) {
    group->hasEnemyBuildings = true;
  }
  if (frame - u->lastAttacked <= 30) {
    group->enemiesAreAttacking = true;
  }
  if (u->cloaked() || u->burrowed()) {
    group->hasEnemyCloakedUnits = true;
  }
  if (u->type == buildtypes::Terran_Siege_Tank_Tank_Mode ||
      u->type == buildtypes::Terran_Siege_Tank_Siege_Mode) {
    group->hasEnemyTanks = true;
  }
  if (u->type == buildtypes::Protoss_Reaver) {
    group->hasEnemyReavers = true;
  }
  if (u->type == buildtypes::Terran_Bunker) {
    group->hasEnemyBunkers = true;
  }
  if (u->type == buildtypes::Terran_Bunker ||
      (u->type->isBuilding &&
       (u->type->hasGroundWeapon || u->type->hasAirWeapon))) {
    group->hasEnemyStaticDefence = true;
  }
  if (u->type->hasGroundWeapon) {
    group->hasEnemyAntiGround = true;
  }
  if (u->type->hasAirWeapon) {
    group->hasEnemyAntiAir = true;
  }
  if (!group->enemiesInOurBase) {
    const Tile* tile = state->tilesInfo().tryGetTile(u->x, u->y);
    if (tile) {
      size_t index = tile - state->tilesInfo().tiles.data();
      group->enemiesInOurBase = inBaseArea[index] != 0;
    }
  }

  if (group->isAggressiveGroup) {
    auto& areaInfo = state->areaInfo();
    auto uArea = areaInfo.tryGetArea(u->pos());
    std::unordered_set<Area const*> areas;
    for (int i = 0; i < areaInfo.numMyBases(); i++) {
      auto* baseInfo = areaInfo.myBase(i);
      areas.insert(baseInfo->area);
      for (auto neighbor : baseInfo->area->neighbors) {
        areas.insert(neighbor);
      }
    }
    if (areas.find(uArea) != areas.end()) {
      group->isAggressiveGroup = false;
    }
    for (Unit* n : state->unitsInfo().myResourceDepots()) {
      if (utils::distance(u, n) <= kAggressiveGroupDistance) {
        group->isAggressiveGroup = false;
      }
    }
  }

  // The group's score is how attractive it is to our units as a target
  // (Lower is more attractive)

  // Prefer to attack units that are far away from their start location.
  if (state->areaInfo().foundEnemyStartLocation()) {
    float d = utils::distance(state->areaInfo().enemyStartLocation(), u);
    group->score -= d * d;
  }
  // And near our workers.
  float nearestWorkerDistance = kfInfty;
  for (Unit* worker : state->unitsInfo().myWorkers()) {
    float d = utils::distance(u, worker);
    if (d < nearestWorkerDistance) {
      nearestWorkerDistance = d;
    }
  }
  if (nearestWorkerDistance != kfInfty) {
    group->score += nearestWorkerDistance * nearestWorkerDistance;
  }

  Vec2 sumPos;
  int sumN = 0;
  for (Unit* u : group->enemyUnits) {
    ++sumN;
    sumPos += Vec2(u->pos());
  }

  group->averagePos = Position(sumPos / sumN);
}

void TacticsState::createTacticsGroups(
    State* state,
    std::vector<uint8_t>& inBaseArea) {
  // Divide enemy units into clusters based on proximity
  const auto& enemyUnits = state->unitsInfo().enemyUnits();
  std::vector<Unit*> enemyUnitsToAdd;
  enemyUnitsToAdd.reserve(enemyUnits.size());

  for (Unit* u : enemyUnits) {
    if (u->gone || (u->detected() && u->invincible())) {
      continue;
    }
    if (u->type == buildtypes::Zerg_Larva || u->type == buildtypes::Zerg_Egg) {
      continue;
    }
    enemyUnitsToAdd.push_back(u);
  }
  while (!enemyUnitsToAdd.empty()) {
    Unit* u = enemyUnitsToAdd.back();
    enemyUnitsToAdd.pop_back();

    groups_.emplace_back();
    TacticsGroup* thisGroup = &(groups_).back();
    addEnemyUnitToGroup(state, u, thisGroup, inBaseArea);

    // Cluster nearby enemy units starting with u as the seed
    // n.b. enemyUnits expands as more units are added to the cluster
    const int clusteringMargin = 16;
    for (size_t i2 = 0; i2 != thisGroup->enemyUnits.size(); ++i2) {
      Unit* unitA = thisGroup->enemyUnits[i2];
      int unitARange = std::max(unitA->unit.airRange, unitA->unit.groundRange);
      for (size_t i3 = 0; i3 != enemyUnitsToAdd.size(); ++i3) {
        Unit* unitB = enemyUnitsToAdd[i3];
        int unitBRange =
            std::max(unitB->unit.airRange, unitB->unit.groundRange);
        int unitBRadius = std::max(unitARange, unitBRange) + clusteringMargin;
        if (utils::distance(unitA, unitB) <= unitBRadius) {
          addEnemyUnitToGroup(state, unitB, thisGroup, inBaseArea);
          if (i3 != enemyUnitsToAdd.size() - 1) {
            std::swap(enemyUnitsToAdd[i3], enemyUnitsToAdd.back());
          }
          enemyUnitsToAdd.pop_back();
          --i3;
        }
      }
    }
  }

  for (auto& g : groups_) {
    if (!g.enemyUnits.empty()) {
      g.score /= g.enemyUnits.size();
    }
  }

  bool anyGroupsWithBuildings = std::any_of(
      groups_.begin(), groups_.end(), [](const TacticsGroup& group) {
        return group.hasEnemyBuildings;
      });

  // What if we don't know where the enemy base is?
  if (groups_.empty() || !anyGroupsWithBuildings) {
    bool found = false;
    for (auto tilePos : state->map()->StartingLocations()) {
      Position pos(
          tilePos.x * tc::BW::XYWalktilesPerBuildtile,
          tilePos.y * tc::BW::XYWalktilesPerBuildtile);
      auto& tile = state->tilesInfo().getTile(pos.x, pos.y);
      if (tile.building && tile.building->isEnemy) {
        groups_.emplace_back();
        groups_.back().targetPos = pos;
        groups_.back().hasEnemyGroundUnits = true;
        groups_.back().hasEnemyBuildings = true;
        found = true;
        break;
      }
    }
    if (!found) {
      for (auto tilePos : state->map()->StartingLocations()) {
        Position pos(
            tilePos.x * tc::BW::XYWalktilesPerBuildtile,
            tilePos.y * tc::BW::XYWalktilesPerBuildtile);
        auto& tile = state->tilesInfo().getTile(pos.x, pos.y);
        if (tile.lastSeen == 0) {
          groups_.emplace_back();
          groups_.back().targetPos = pos;
          groups_.back().hasEnemyGroundUnits = true;
          groups_.back().hasEnemyBuildings = true;
        }
      }
    }
  }

  // If we can't see any enemy units, make a group to go find them
  if (groups_.empty()) {
    groups_.emplace_back();
    groups_.back().searchAndDestroy = true;
    groups_.back().targetPos = Position(0, 1);
  }

  // Mark groups as aggresive or otherwise
  if (state->currentFrame() < 15 * 60 * 16) {
    for (TacticsGroup& group : groups_) {
      if (!group.hasEnemyTanks && !group.hasEnemyReavers &&
          !group.hasEnemyAirUnits && !group.enemiesInOurBase &&
          !group.enemyUnits.empty()) {
        group.isAggressiveGroup = true;
      }
    }
  }

  // Prioritize defending
  for (TacticsGroup& g : groups_) {
    if (!g.isAggressiveGroup) {
      g.score -= 100000.0;
    }
  }

  groups_.sort(
      [](TacticsGroup& a, TacticsGroup& b) { return a.score < b.score; });

  // Make a scouting group if we have many workers
  if (state->unitsInfo().myWorkers().size() >= 30 ||
      state->unitsInfo().myUnitsOfType(buildtypes::Zerg_Zergling).size() >=
          18) {
    groups_.emplace_back();
    groups_.back().isScoutGroup = true;
    groups_.back().targetPos = Position(1, 0);
  }

  if (!state->board()->get<bool>("TacticsAttack", true)) {
    groups_.emplace_back();
    groups_.back().isIdleGroup = true;
    groups_.back().isAggressiveGroup = false;
    groups_.back().targetPos = Position(1, 1);
  }
}

void TacticsState::collectMapNodesCoveredByGroups(State* state) {
  const int mapWidth = state->mapWidth();
  const int mapHeight = state->mapHeight();

  // Flood-fill the map with the tiles nearest to each cluster
  std::deque<TacticsMapNode> open;
  for (TacticsGroup& g : groups_) {
    for (Unit* e : g.enemyUnits) {
      if (e->type != buildtypes::Zerg_Overlord) {
        open.push_back({state->tilesInfo().tryGetTile(e->x, e->y), &g, e});
      }
    }
  }
  auto* tilesData = state->tilesInfo().tiles.data();
  while (!open.empty()) {
    TacticsMapNode curNode = open.front();
    open.pop_front();
    Tile* tile = curNode.tile;
    if (!tile) {
      continue;
    }
    size_t index = tile - tilesData;
    TacticsMapNode& n = nodeInsideGroupTracker_[index];
    if (n.group) {
      continue;
    }
    if (utils::distance(
            tile->x,
            tile->y,
            curNode.nearestEnemy->x,
            curNode.nearestEnemy->y) > insideGroupDistance_) {
      continue;
    }
    n = curNode;

    if (tile->x > 0) {
      open.push_back({tile - 1, curNode.group, curNode.nearestEnemy});
    }
    if (tile->y > 0) {
      open.push_back(
          {tile - TilesInfo::tilesWidth, curNode.group, curNode.nearestEnemy});
    }
    if (tile->x < mapWidth - tc::BW::XYWalktilesPerBuildtile) {
      open.push_back({tile + 1, curNode.group, curNode.nearestEnemy});
    }
    if (tile->y < mapHeight - tc::BW::XYWalktilesPerBuildtile) {
      open.push_back(
          {tile + TilesInfo::tilesWidth, curNode.group, curNode.nearestEnemy});
    }
  }

  uint8_t visitedValue = ++visitNumber_;

  for (TacticsGroup& g : groups_) {
    for (Unit* e : g.enemyUnits) {
      if (!e->type->isWorker && e->type != buildtypes::Zerg_Overlord) {
        open.push_back({state->tilesInfo().tryGetTile(e->x, e->y), &g, e});
      }
    }
  }
  while (!open.empty()) {
    TacticsMapNode curNode = open.front();
    open.pop_front();
    Tile* tile = curNode.tile;
    if (!tile) {
      continue;
    }
    size_t index = tile - tilesData;
    if (tileVisitTracker_[index] == visitedValue) {
      continue;
    }
    tileVisitTracker_[index] = visitedValue;
    if (!nodeInsideGroupTracker_[index].group) {
      nodeGroupEdgeTracker_[index] = curNode.group;
      continue;
    }

    if (tile->x > 0) {
      open.push_back({tile - 1, curNode.group, curNode.nearestEnemy});
    }
    if (tile->y > 0) {
      open.push_back(
          {tile - TilesInfo::tilesWidth, curNode.group, curNode.nearestEnemy});
    }
    if (tile->x < mapWidth - tc::BW::XYWalktilesPerBuildtile) {
      open.push_back({tile + 1, curNode.group, curNode.nearestEnemy});
    }
    if (tile->y < mapHeight - tc::BW::XYWalktilesPerBuildtile) {
      open.push_back(
          {tile + TilesInfo::tilesWidth, curNode.group, curNode.nearestEnemy});
    }
  }
}

void TacticsState::assignUnitsBasedOnPreviousAssignments(
    State* state,
    std::unordered_set<Unit*>& wasInAGroup,
    std::vector<std::shared_ptr<Task>>& tasks) {
  auto* tilesData = state->tilesInfo().tiles.data();

  // Begin assigning our units to Groups
  // Start by assigning units to Groups based on prior assignments

  for (auto& t : tasks) {
    auto task = std::static_pointer_cast<TacticsTask>(t);
    for (Unit* u : task->myUnits) {
      wasInAGroup.insert(u);
    }
  }

  std::unordered_set<TacticsGroup*> groupTaken;
  std::unordered_set<TacticsTask*> taskTaken;

  for (int i = 0; i != 2; ++i) {
    while (true) {
      float bestDistance = kfInfty;
      TacticsGroup* bestGroup = nullptr;
      std::shared_ptr<TacticsTask> bestTask = nullptr;

      for (auto& t : tasks) {
        auto task = std::static_pointer_cast<TacticsTask>(t);
        if (taskTaken.find(&*task) != taskTaken.end()) {
          continue;
        }
        for (auto& g : groups_) {
          if (i == 0 && (g.isIdleGroup || g.enemyUnits.empty() ||
                         task->averagePos == Position())) {
            continue;
          }
          if (groupTaken.find(&g) != groupTaken.end()) {
            continue;
          }
          float d = utils::distance(
              task->averagePos == Position() ? g.targetPos : g.averagePos,
              task->averagePos == Position() ? task->targetPos
                                             : task->averagePos);
          if (d < 4 * 16 && d < bestDistance) {
            bestDistance = d;
            bestGroup = &g;
            bestTask = task;
          }
        }
      }

      if (!bestGroup) {
        break;
      }

      groupTaken.insert(bestGroup);
      taskTaken.insert(&*bestTask);

      bestGroup->task = bestTask;

      if (!bestGroup->isIdleGroup) {
        for (Unit* u : bestTask->myUnits) {
          if (u->dead || !u->isMine || !u->active()) {
            continue;
          }
          if (!softAssignedUnits_.emplace(u, bestGroup).second) {
            continue;
          }
          if (bestGroup->isScoutGroup && !u->burrowed()) {
            size_t index = &state->tilesInfo().getTile(u->x, u->y) - tilesData;
            TacticsMapNode& n = nodeInsideGroupTracker_[index];
            if (!n.group) {
              bestGroup->myUnits.push_back(u);
              hardAssignedUnits_[u] = n.group;
            }
          }
        }
      }
    }
  }

  for (auto& t : tasks) {
    auto task = std::static_pointer_cast<TacticsTask>(t);
    if (taskTaken.find(&*task) == taskTaken.end()) {
      task->cancel(state);
    }
  }
}

void TacticsState::collectAvailableUnits(
    State* state,
    std::vector<Unit*>& availableUnits) {
  auto* tilesData = state->tilesInfo().tiles.data();
  auto& myUnits = state->unitsInfo().myUnits();
  for (Unit* u : myUnits) {
    if (!u->active() || u->type->isBuilding) {
      continue;
    }
    if (u->type->isNonUsable) {
      continue;
    }
    if (hardAssignedUnits_.find(u) != hardAssignedUnits_.end()) {
      continue;
    }

    TaskData d = state->board()->taskDataWithUnit(u);
    // Don't take units from Builder
    if (d.owner && d.owner->name().find("Builder") != std::string::npos) {
      continue;
    }
    // Don't take units from Scouting
    if (d.owner && d.owner->name().find("Scouting") != std::string::npos) {
      continue;
    }

    size_t index = &state->tilesInfo().getTile(u->x, u->y) - tilesData;
    TacticsMapNode& n = nodeInsideGroupTracker_.at(index);
    // Accept units who can help fight these enemies
    // TODO: Should include all unit types that don't shoot up.
    // TODO: Need to do the same for air units which can't shoot down.
    // See the code about 50 lines below this for a good way to do this
    // though we may also want to include units with no weapons (eg. casters)
    bool canAttackMe = n.group &&
        (u->flying() ? n.group->hasEnemyAntiAir : n.group->hasEnemyAntiGround);
    if (n.group && canAttackMe && !u->type->isWorker &&
        (u->type != buildtypes::Zerg_Zergling ||
         n.group->hasEnemyGroundUnits) &&
        !n.group->enemyIsOnlyWorkers && !n.group->hasEnoughUnits) {
      n.group->myUnits.push_back(u);
      n.group->hasEnoughUnits =
          winRatio(n.group->myUnits, n.group->enemyUnits) >= 4.0;
    } else {
      if (u->type->isWorker) {
        continue;
      }

      availableUnits.push_back(u);
    }
  }
}

void TacticsState::assignScoutingUnits(
    State* state,
    std::vector<Unit*>& availableUnits) {
  auto* tilesData = state->tilesInfo().tiles.data();
  size_t nScouts = 1;
  if (state->unitsInfo().myWorkers().size() >= 60) {
    nScouts = 3;
  } else if (state->unitsInfo().myWorkers().size() >= 45) {
    nScouts = 2;
  }
  double armySupply = 0.0;
  for (Unit* u : state->unitsInfo().myUnits()) {
    armySupply += u->type->supplyRequired;
  }
  if (armySupply >= 20.0f) {
    nScouts *= 2;
  }
  for (TacticsGroup& g : groups_) {
    if (g.isScoutGroup && g.myUnits.size() < nScouts) {
      for (auto i = availableUnits.begin(); i != availableUnits.end();) {
        Unit* u = *i;
        // Find acceptable scouts
        // TODO: This should consider scout units for other races
        if (u->type == buildtypes::Zerg_Zergling && !u->burrowed() &&
            !nodeInsideGroupTracker_[&state->tilesInfo().getTile(u->x, u->y) -
                                     tilesData]
                 .group) {
          i = availableUnits.erase(i);
          g.myUnits.push_back(u);
          if (g.myUnits.size() >= nScouts) {
            break;
          }
        } else {
          ++i;
        }
      }
    }
  }
}

bool TacticsState::aggressiveUnit(State* state, Unit* unit) {
  return state->board()->get<bool>("TacticsAttack", true);
}

// Heuristic of how helpful this unit is to the group fight
double TacticsState::scoreUnitForGroup(State* state, Unit* u, TacticsGroup& g) {
  if ((!g.hasEnemyAirUnits || !u->type->hasAirWeapon) &&
      (!g.hasEnemyGroundUnits || !u->type->hasGroundWeapon)) {
    return kdInfty;
  }
  if (g.isAggressiveGroup && !aggressiveUnit(state, u)) {
    return kdInfty;
  }
  // If the unit can't reach the group, then it shouldn't be assigned
  if (!u->flying() &&
      state->areaInfo().getArea(u->pos()).groupId !=
          state->areaInfo().getArea(g.targetPos).groupId) {
    return kdInfty;
  }
  double d = g.targetPos.distanceTo(u);
  // Burrowed units are not very useful.
  if (u->burrowed() && u->type != buildtypes::Zerg_Lurker) {
    d += 4 * 256;
  }
  // Dissuade workers from getting pulled into fights
  if (u->type->isWorker) {
    if (!g.enemyIsOnlyWorkers || !g.enemiesInOurBase) {
      return kdInfty;
    }
    d += 4 * 256;
  }
  // Avoid having units waffle between groups.
  // Make units stickier to their last assignment.
  if (softAssignedUnits_[u] == &g) {
    d -= 4 * 128;
  }
  return d;
}

void TacticsState::assignNecessaryUnits(
    State* state,
    std::vector<Unit*>& availableUnits) {
  // For each group, recruit the next-best unit
  // until we have enough to win the fight.
  // First iteration: only defend, spread units across the groups to ensure
  // we defend everywhere.
  for (int i = 0; i != 2; ++i) {
    bool assignedAnything;
    do {
      assignedAnything = false;
      for (TacticsGroup& g : groups_) {
        if (g.hasEnoughUnits || (i == 0 && g.isAggressiveGroup)) {
          continue;
        }

        // Remove workers; use only for worker-on-worker defense
        auto workersIt =
            std::remove_if(g.myUnits.begin(), g.myUnits.end(), [&](Unit* u) {
              return u->type->isWorker;
            });
        std::vector<Unit*> workers(workersIt, g.myUnits.end());
        g.myUnits.erase(workersIt, g.myUnits.end());
        if (!g.enemyIsOnlyWorkers) {
          workers.clear();
        }

        // Recruit units for the fight until we're satisfied (or out of units).
        while (!g.hasEnoughUnits) {
          auto i = utils::getBestScore(
              availableUnits,
              [&](Unit* u) { return scoreUnitForGroup(state, u, g); },
              kdInfty);
          if (i == availableUnits.end()) {
            break;
          }
          Unit* u = *i;
          if (i != std::prev(availableUnits.end())) {
            std::swap(*i, availableUnits.back());
          }

          availableUnits.pop_back();
          g.myUnits.push_back(u);
          assignedAnything = true;
          // Recruit only the units we need to win
          double desiredWinRatio = g.isAggressiveGroup ? 4.0 : 2.0;
          if (g.enemyIsOnlyWorkers) {
            desiredWinRatio = 1.0;
          }
          double ratio = winRatio(g.myUnits, g.enemyUnits);
          g.hasEnoughUnits = ratio >= desiredWinRatio;
          if (g.enemyIsOnlyWorkers && g.enemyUnits.size() == 1) {
            g.hasEnoughUnits = true;
            break;
          }
          if (!g.isAggressiveGroup) {
            if (ratio >= 0.5) {
              break;
            }
          }
        }

        if (!g.hasEnoughUnits) {
          g.myUnits.insert(g.myUnits.end(), workers.begin(), workers.end());
        }
      }
    } while (assignedAnything);
  }
}

void TacticsState::assignDetectors(std::vector<Unit*>& availableUnits) {
  std::deque<TacticsGroup*> remainingGroups;
  for (auto& g : groups_) {
    remainingGroups.push_back(&g);
  }
  std::sort(
      remainingGroups.begin(),
      remainingGroups.end(),
      [](TacticsGroup* a, TacticsGroup* b) {
        if (a->hasEnemyCloakedUnits != b->hasEnemyCloakedUnits) {
          return a->hasEnemyCloakedUnits;
        }
        return a->myUnits.size() > b->myUnits.size();
      });
  while (!remainingGroups.empty()) {
    TacticsGroup* g = remainingGroups.front();
    remainingGroups.pop_front();
    if (!g->hasEnemyCloakedUnits) {
      continue;
    }
    bool hasDetector = false;
    for (Unit* u : g->myUnits) {
      if (u->type->isDetector) {
        hasDetector = true;
        break;
      }
    }
    if (!hasDetector) {
      auto i = utils::getBestScore(
          availableUnits,
          [&](Unit* u) {
            if (!u->type->isDetector) {
              return kdInfty;
            }
            return g->targetPos.distanceTo(u);
          },
          kdInfty);
      if (i == availableUnits.end()) {
        break;
      }
      Unit* u = *i;
      if (i != std::prev(availableUnits.end())) {
        std::swap(*i, availableUnits.back());
      }
      availableUnits.pop_back();
      g->myUnits.push_back(u);
    }
  }
}

void TacticsState::assignLeftovers(
    State* state,
    std::vector<Unit*>& availableUnits,
    std::vector<Unit*>& leftoverWorkers) {
  if (groups_.empty()) {
    return;
  }

  int assignNOverlords = 0;
  if (state->unitsInfo().myWorkers().size() >= 45) {
    assignNOverlords = 2;
    for (auto& g : groups_) {
      if (g.isAggressiveGroup) {
        for (Unit* u : g.myUnits) {
          if (u->type == buildtypes::Zerg_Overlord) {
            --assignNOverlords;
          }
        }
      }
    }
  }

  TacticsGroup* airGroup = &groups_.front();
  TacticsGroup* groundGroup = &groups_.front();
  for (auto& g : groups_) {
    if (g.hasEnemyAirUnits) {
      airGroup = &g;
      break;
    }
  }
  for (auto& g : groups_) {
    if (g.hasEnemyGroundUnits) {
      groundGroup = &g;
      break;
    }
  }
  TacticsGroup* defAirGroup = &groups_.back();
  TacticsGroup* defGroundGroup = &groups_.back();
  for (auto& g : groups_) {
    if (!g.isAggressiveGroup && g.hasEnemyAirUnits) {
      defAirGroup = &g;
      break;
    }
  }
  for (auto& g : groups_) {
    if (!g.isAggressiveGroup && g.hasEnemyGroundUnits) {
      defGroundGroup = &g;
      break;
    }
  }
  while (!availableUnits.empty()) {
    Unit* u = availableUnits.back();
    TacticsGroup* g = aggressiveUnit(state, u)
        ? (u->type->hasAirWeapon ? airGroup : groundGroup)
        : (u->type->hasAirWeapon ? defAirGroup : defGroundGroup);
    if (u->type->isWorker) {
      leftoverWorkers.push_back(availableUnits.back());
    } else if (
        u->type != buildtypes::Zerg_Overlord ||
        (assignNOverlords && assignNOverlords--)) {
      g->myUnits.push_back(u);
    }
    availableUnits.pop_back();
  }
}

void TacticsState::assignUnits(
    State* state,
    std::unordered_set<Unit*>& wasInAGroup,
    std::vector<Unit*> leftoverWorkers,
    std::vector<std::shared_ptr<Task>>& tasks) {
  std::vector<Unit*> availableUnits;
  availableUnits.reserve(state->unitsInfo().myUnits().size());

  assignUnitsBasedOnPreviousAssignments(state, wasInAGroup, tasks);
  collectAvailableUnits(state, availableUnits);
  assignScoutingUnits(state, availableUnits);
  assignNecessaryUnits(state, availableUnits);
  assignDetectors(availableUnits);
  assignLeftovers(state, availableUnits, leftoverWorkers);

  if (VLOG_IS_ON(2)) {
    VLOG(2) << groups_.size() << "groups";
    for (TacticsGroup& g : groups_) {
      VLOG(2) << "group at " << g.targetPos.x << " " << g.targetPos.y << ": "
              << g.myUnits.size() << " allies, " << g.enemyUnits.size()
              << " enemies"
              << " aggressive " << g.isAggressiveGroup;
      for (Unit* u : g.myUnits) {
        VLOG(2) << "  " << utils::unitString(u);
      }
      for (Unit* u : g.enemyUnits) {
        VLOG(2) << "  " << utils::unitString(u);
      }
    }
  }
}

bool TacticsState::isAllyInRangeOfEnemy(TacticsGroup& g) {
  for (Unit* u : g.myUnits) {
    for (Unit* e : g.enemyUnits) {
      if (e->topSpeed >= u->topSpeed && u->inRangeOf(e, 4)) {
        return true;
      }
    }
  }
  return false;
}

void TacticsState::prepareCombatSimulationData(
    State* state,
    TacticsGroup& g,
    std::vector<Unit*>& nearbyAllies,
    std::unordered_set<Unit*>& nearbyEnemies,
    std::unordered_map<Unit*, int>& nmyInStaticDefenseRange,
    std::unordered_map<Unit*, int>& nmyAlmostInStaticDefenseRange) {
  for (Unit* u : g.myUnits) {
    // Are there any enemies near this unit?
    // If so, consider this unit (and the nearby enemies)
    // in combat simulation
    Unit* enemy = utils::getBestScoreCopy(
        g.enemyUnits,
        [&](Unit* e) {
          double d = utils::distance(u, e);
          if (d >= kNearbyUnitDistance) {
            return kdInfty;
          }
          nearbyEnemies.insert(e);
          return d;
        },
        kdInfty);
    if (enemy) {
      nearbyAllies.push_back(u);
    }
  }
  // Also consider the support of our static defenses, but only if
  // the enemy units are in range.
  // This may be how we want to handle Lurkers/Siege Tanks as well.
  for (auto u : state->unitsInfo().myBuildings()) {
    if (u->type->hasGroundWeapon || u->type->hasAirWeapon) {
      Unit* enemy = utils::getBestScoreCopy(
          g.enemyUnits,
          [&](Unit* e) {
            double d = utils::distance(u, e);
            auto range = e->flying() ? u->unit.airRange : u->unit.groundRange;
            if (d >= range) {
              if (d <= range + 12) {
                ++nmyAlmostInStaticDefenseRange[e];
              }
              return kdInfty;
            }
            nearbyEnemies.insert(e);
            ++nmyInStaticDefenseRange[e];
            return d;
          },
          kdInfty);
      if (enemy) {
        nearbyAllies.push_back(u);
      }
    }
  }
}

std::pair<double, double> TacticsState::combatSimCalculateFightScoreMod(
    State* state,
    TacticsGroup& g,
    std::vector<Unit*>& nearbyAllies,
    std::unordered_set<Unit*>& nearbyEnemies,
    std::unordered_map<Unit*, int>& nmyInStaticDefenseRange) {
  double score = 0;
  double mod = 0;

  // Account for the uselessness of Zerglings against Vultures.
  //
  // The Zergling/Vulture dilemma presently accounted for here
  // exists in other forms as well:
  // * Zealot vs. Vulture
  // * Slow Zealot vs. Dragoon
  // * Stimless/range-less Marine vs. ranged Dragoon
  // * Slow/range-less Hydralisk vs. ranged Dragoon
  // etc.
  size_t myLings = 0;
  size_t myTotal = 0;
  size_t enemyVultures = 0;
  for (Unit* u : nearbyAllies) {
    if (u->type == buildtypes::Zerg_Zergling) {
      ++myLings;
    }
    if (u->type != buildtypes::Zerg_Overlord) {
      ++myTotal;
    }
  }
  for (Unit* u : nearbyEnemies) {
    if (!u->type->isWorker && u->type != buildtypes::Zerg_Overlord) {
      if (u->type == buildtypes::Terran_Vulture) {
        ++enemyVultures;
      }
    }
  }

  auto num_to_avg_over = 2;
  for (int i = 0; i < num_to_avg_over; ++i) {
    std::vector<Unit*> allyTeam;
    std::vector<Unit*> enemyTeam;
    CombatSim sim;
    if (i == 1) {
      sim.speedMult = 0.5;
    }
    for (Unit* u : nearbyAllies) {
      auto used = sim.addUnit(u);
      if (used) {
        allyTeam.emplace_back(u);
      }
    }
    for (Unit* u : nearbyEnemies) {
      if (!u->type->isWorker && u->type != buildtypes::Zerg_Overlord) {
        auto used = sim.addUnit(u);
        if (used) {
          enemyTeam.emplace_back(u);
        }
      }
    }
    sim.run(10 * 24);

    score += convertSimToScore(sim, allyTeam, enemyTeam);
  }

  score /= num_to_avg_over;

  // Don't fight Vultures with just Zerglings unless they're already in our
  // base
  // (Since they can kite the Zerglings indefinitely in open space)
  if (myLings == myTotal && enemyVultures &&
      enemyVultures >=
          (nearbyEnemies.size() - 1) / 2 + nearbyEnemies.size() / 6 &&
      state->currentFrame() < 24 * 60 * 9 && !g.enemiesAreAttacking &&
      !g.enemiesInOurBase) {
    if (nmyInStaticDefenseRange.empty()) {
      bool inMain = false;
      auto& mainArea =
          state->areaInfo().getArea(state->areaInfo().myStartLocation());
      for (Unit* u : nearbyEnemies) {
        if (&state->areaInfo().getArea(Position(u)) == &mainArea) {
          inMain = true;
        }
      }
      if (!inMain) {
        mod += 100.0;
      }
    }
  } else {
    if (isAllyInRangeOfEnemy(g)) {
      mod -= 0.3;
    } else {
      if (!g.isAggressiveGroup) {
        mod += 0.2;
      }
    }
    if (!g.isAggressiveGroup) {
      mod -= 0.3;
    }
    if (g.enemiesInOurBase) {
      mod -= 0.3;
    }
  }
  if (g.hasEnemyTanks) {
    mod += 0.3;
  }
  if (state->currentFrame() < 24 * 60 * 15 && g.hasEnemyStaticDefence) {
    mod += 0.3;
  }
  // Bunkers are scary when repaired
  if (g.hasEnemyBunkers && g.hasEnemyWorkers &&
      state->currentFrame() < 24 * 60 * 15) {
    mod += 0.3;
  }
  return std::make_pair(score, mod);
}

TacticsFightScores TacticsState::combatSimFightPrediction(
    State* state,
    TacticsGroup& g,
    std::unordered_map<Unit*, int>& nmyInStaticDefenceRange,
    std::unordered_map<Unit*, int>& nmyAlmostInStaticDefenceRange) {
  TacticsFightScores tfs;

  std::vector<Unit*> nearbyAllies;
  std::unordered_set<Unit*> nearbyEnemies;
  prepareCombatSimulationData(
      state,
      g,
      nearbyAllies,
      nearbyEnemies,
      nmyInStaticDefenceRange,
      nmyAlmostInStaticDefenceRange);

  // Decide whether (and how eagerly) we want to fight.
  // This is powered largely by a combat simulation,
  // but is also influenced by
  // * Contextual considerations (where we're fighting)
  // * Considerations not captured by simulation (kiting)
  if (!nearbyAllies.empty() && !nearbyEnemies.empty()) {
    double mod;
    std::tie(tfs.score, mod) = combatSimCalculateFightScoreMod(
        state, g, nearbyAllies, nearbyEnemies, nmyInStaticDefenceRange);

    // Apply the contextual considerations.
    // Also apply hysteresis based on the overall combat decision;
    // persist in fighting when we're already fighting.
    if (g.task->isFighting) {
      tfs.airFight = tfs.score >= (0.0 + mod);
      tfs.groundFight = tfs.score >= (0.0 + mod);
    } else {
      tfs.airFight = tfs.score >= (0.4 + mod);
      tfs.groundFight = tfs.score >= (0.4 + mod);
    }
  }
  return tfs;
}

Unit* TacticsState::getBestEnemyTarget(
    State* state,
    TacticsGroup& g,
    Unit* u,
    std::unordered_map<Unit*, int>& meleeTargetCount,
    std::unordered_map<Unit*, int>& lastTargetInRange,
    bool& anySpiderMinesNearby) {
  Unit* target = nullptr;
  target = utils::getBestScoreCopy(
      g.enemyUnits,
      [&](Unit* e) {
        if (e->flying() ? !u->type->hasAirWeapon : !u->type->hasGroundWeapon) {
          return kdInfty;
        }
        double d =
            (double)utils::pxDistanceBB(u, e) * tc::BW::XYPixelsPerWalktile;
        if (e->type == buildtypes::Terran_Vulture_Spider_Mine && d <= 4 * 4) {
          anySpiderMinesNearby = true;
        }
        double r = d;
        if (e->type->isWorker) {
          r -= 4 * 2;
        }
        if (e->type == buildtypes::Terran_Siege_Tank_Siege_Mode) {
          r -= 4 * 10;
        }
        if (u->type == buildtypes::Zerg_Zergling ||
            u->type == buildtypes::Zerg_Scourge) {
          int maxN = 2 + e->type->size;
          if (meleeTargetCount[e] >= maxN) {
            r += 4 * 6;
          }
          if (e->type == buildtypes::Terran_Missile_Turret) {
            r -= 4 * 10;
          }
          if (e->type == buildtypes::Terran_Vulture && d > 32) {
            r += 4 * 6;
          }
        }
        if (d > 4 * 2 && r < 4 * 2) {
          r = 4 * 2;
        }
        return r;
      },
      kdInfty);
  if (target) {
    if (u->type == buildtypes::Zerg_Zergling ||
        u->type == buildtypes::Zerg_Scourge) {
      ++meleeTargetCount[target];
    }
    if (target->inRangeOf(u)) {
      lastTargetInRange[u] = state->currentFrame();
    }
  }
  return target;
}

bool TacticsState::shouldRunFromHiddenTarget(
    TacticsGroup& g,
    Unit* u,
    Unit* target) {
  if (target && (target->cloaked() || target->burrowed()) &&
      !target->detected() && !u->type->isDetector && u->inRangeOf(target, 16)) {
    Unit* detector = utils::getBestScoreCopy(
        g.myUnits,
        [&](Unit* n) {
          if (!n->type->isDetector) {
            return kfInfty;
          }
          return utils::distance(u, n);
        },
        kfInfty);
    if (!detector || utils::distance(u, detector) > 4 * 8) {
      return true;
    }
  }
  return false;
}

int TacticsState::getRandomCoord(int range, std::ranlux24& rngEngine) {
  double n = std::normal_distribution<>(range / 2, range / 2)(rngEngine);
  bool neg = n < 0.0;
  n = std::fmod(std::abs(n), range);
  return neg ? range - (int)n : (int)n;
}

Position TacticsState::idleGroupTargetPos(
    State* state,
    Unit* u,
    std::vector<uint8_t>& inBaseArea) {
  auto& areaInfo = state->areaInfo();
  Position moveTo = Position(u);
  if (areaInfo.numMyBases() <= 2) {
    auto chokes =
        areaInfo.getArea(areaInfo.myStartLocation()).area->ChokePoints();
    for (auto choke : chokes) {
      if (!choke->Blocked()) {
        moveTo = Position(choke->Pos(BWEM::ChokePoint::node::middle));
        if (VLOG_IS_ON(2)) {
          utils::drawLine(
              state,
              Position(choke->Pos(BWEM::ChokePoint::node::end1)),
              Position(choke->Pos(BWEM::ChokePoint::node::middle)),
              tc::BW::Color::Blue);
          utils::drawLine(
              state,
              Position(choke->Pos(BWEM::ChokePoint::node::end2)),
              Position(choke->Pos(BWEM::ChokePoint::node::middle)),
              tc::BW::Color::Red);
        }
        break;
      }
    }
  } else {
    Unit* hatch = utils::getBestScoreCopy(
        state->unitsInfo().myResourceDepots(),
        [&](Unit* n) { return utils::distance(u, n); });
    if (hatch) {
      moveTo = Position(hatch);
    }
  }
  Tile* tile = state->tilesInfo().tryGetTile(u->x, u->y);
  if (tile && inBaseArea[tile - state->tilesInfo().tiles.data()] != 0) {
    Unit* drone = utils::getBestScoreCopy(
        state->unitsInfo().myWorkers(),
        [&](Unit* n) { return utils::distance(u, n); });
    if (drone && utils::distance(u, drone) <= 4 * 4) {
      Position pos = findMoveAwayPos(state, u, Position(drone), 4 * 6);
      if (pos != Position()) {
        moveTo = pos;
      }
    }
  }
  return moveTo;
}

Position TacticsState::scoutGroupTargetPos(
    State* state,
    TacticsGroup& g,
    Unit* u,
    std::unordered_map<Unit*, std::pair<int, Position>>& scoutTarget,
    std::ranlux24& rngEngine) {
  auto& target = scoutTarget[u];
  if (state->currentFrame() - target.first >= 15 * 2) {
    // This helps blow up mines from expansions when workers are trying
    // to expand
    const BWEM::Area* sourceArea =
        state->map()->GetNearestArea(BWAPI::WalkPosition(u->x, u->y));
    for (auto& area : state->map()->Areas()) {
      if (area.AccessibleFrom(sourceArea)) {
        for (auto& base : area.Bases()) {
          BWAPI::WalkPosition walkpos(base.Center());
          Position pos(walkpos.x, walkpos.y);
          const Tile& tile = state->tilesInfo().getTile(pos.x, pos.y);
          if (!tile.building) {
            Unit* worker = utils::getBestScoreCopy(
                state->unitsInfo().myWorkers(),
                [&](Unit* u) {
                  auto taskData = state->board()->taskDataWithUnit(u);
                  if (!taskData.task || !taskData.owner ||
                      taskData.owner->name().find("Builder") ==
                          std::string::npos) {
                    return kfInfty;
                  }
                  float d = utils::distance(Position(u), pos);
                  if (d > 4 * 8) {
                    return kfInfty;
                  }
                  return d;
                },
                kfInfty);
            if (worker) {
              target.first = state->currentFrame();
              target.second = pos -
                  Position(std::uniform_int_distribution<>(-4, 4)(rngEngine),
                           std::uniform_int_distribution<>(-6, 6)(rngEngine));
            }
          }
        }
      }
    }
  }

  Position moveTo = target.second;

  auto shouldBurrow = [&]() {
    if (state->hasResearched(buildtypes::Burrowing)) {
      if (state->areaInfo().foundEnemyStartLocation()) {
        std::vector<std::pair<float, Position>> sortedExpansions;
        for (auto& area : state->map()->Areas()) {
          for (auto& base : area.Bases()) {
            BWAPI::WalkPosition walkpos(base.Center());
            Position pos(walkpos.x, walkpos.y);
            const Tile& tile = state->tilesInfo().getTile(pos.x, pos.y);
            bool okay = !tile.building;
            if (okay && utils::distance(u, pos) > 4 * 8) {
              for (Unit* u : state->unitsInfo().enemyUnits()) {
                if (!u->gone &&
                    (u->type->isBuilding || u->type->hasGroundWeapon)) {
                  if (utils::distance(u, pos) <= 4 * 16) {
                    okay = false;
                  }
                }
              }
            }
            if (okay) {
              for (Unit* u : state->unitsInfo().myUnits()) {
                if (u->burrowed() && utils::distance(u, pos) <= 8) {
                  okay = false;
                }
              }
            }
            if (okay) {
              float d = state->areaInfo().walkPathLength(
                  state->areaInfo().enemyStartLocation(), pos);
              sortedExpansions.emplace_back(d, pos);
            }
          }
        }
        std::sort(sortedExpansions.begin(), sortedExpansions.end());
        for (size_t i = 0; i != 2 && i != sortedExpansions.size(); ++i) {
          Position pos = sortedExpansions[i].second;
          if (utils::distance(u, pos) <= 6) {
            return true;
          }
        }
      }
    }
    return false;
  };

  if (shouldBurrow()) {
    state->board()->postCommand(
        tc::Client::Command(
            tc::BW::Command::CommandUnit,
            u->id,
            tc::BW::UnitCommandType::Burrow),
        g.task->upcId());
  }

  if (state->currentFrame() - target.first >= 15 * 30 ||
      utils::distance(u->x, u->y, moveTo.x, moveTo.y) <= 4) {
    const BWEM::Area* sourceArea =
        state->map()->GetNearestArea(BWAPI::WalkPosition(u->x, u->y));
    std::vector<std::pair<double, Position>> destinations;
    if (destinations.empty()) {
      if (std::uniform_int_distribution<>(0, 255)(rngEngine) <= 10) {
        moveTo.x = getRandomCoord(state->mapWidth(), rngEngine);
        moveTo.y = getRandomCoord(state->mapHeight(), rngEngine);
      } else if (std::uniform_int_distribution<>(0, 255)(rngEngine) <= 240) {
        for (auto& area : state->map()->Areas()) {
          if (area.AccessibleFrom(sourceArea)) {
            for (auto& base : area.Bases()) {
              BWAPI::WalkPosition walkpos(base.Center());
              Position pos(walkpos.x, walkpos.y);
              const Tile& tile = state->tilesInfo().getTile(pos.x, pos.y);
              double age =
                  std::min(state->currentFrame() - tile.lastSeen, 1003);
              if (state->areaInfo().foundEnemyStartLocation()) {
                bool okay = true;
                for (Unit* u : state->unitsInfo().enemyUnits()) {
                  if (!u->gone &&
                      (u->type->isBuilding || u->type->hasGroundWeapon)) {
                    if (utils::distance(u, pos) <= 4 * 16) {
                      okay = false;
                    }
                  }
                }
                if (okay) {
                  float d = state->areaInfo().walkPathLength(
                      state->areaInfo().enemyStartLocation(), pos);
                  age /= d;
                } else {
                  age /= 1024;
                }
              }
              destinations.emplace_back(age, pos);
            }
          }
        }
      } else {
        for (auto& area : state->map()->Areas()) {
          if (area.AccessibleFrom(sourceArea)) {
            BWAPI::WalkPosition pos = area.Top();
            destinations.emplace_back(1.0, Position(pos.x, pos.y));
          }
        }
      }
    }

    if (!destinations.empty()) {
      double sum = 0.0;
      for (auto& v : destinations) {
        sum += v.first;
      }
      double v = std::uniform_real_distribution<>(0, sum)(rngEngine);
      moveTo = destinations[0].second;
      for (size_t i = 1; i != destinations.size(); ++i) {
        if (v < destinations[i].first) {
          moveTo = destinations[i].second;
          break;
        } else {
          v -= destinations[i].first;
        }
      }
    }

    target.first = state->currentFrame();
    target.second = moveTo;
  }
  return moveTo;
}

Position TacticsState::searchAndDestroyGroupTargetPos(
    State* state,
    Unit* u,
    std::unordered_map<Unit*, std::pair<int, Position>>& searchAndDestroyTarget,
    std::ranlux24& rngEngine) {
  auto& sndTarget = searchAndDestroyTarget[u];
  Position moveTo = sndTarget.second;
  if (state->currentFrame() - sndTarget.first >= 15 * 30 ||
      utils::distance(u->x, u->y, moveTo.x, moveTo.y) <= 4) {
    moveTo.x = getRandomCoord(state->mapWidth(), rngEngine);
    moveTo.y = getRandomCoord(state->mapHeight(), rngEngine);
    sndTarget.first = state->currentFrame();
    sndTarget.second = moveTo;
  }
  return moveTo;
}

Position TacticsState::findRunPos(
    State* state,
    Unit* u,
    std::vector<uint16_t>& fleeScore) {
  auto mapWidth = state->mapWidth();
  auto mapHeight = state->mapHeight();
  bool flying = u->flying();

  uint8_t visitedValue = ++visitNumber_;

  auto* tilesData = state->tilesInfo().tiles.data();

  Position startPos(u->x, u->y);

  int nFound = 0;
  int bestScore = std::numeric_limits<int>::max();
  Position bestPos;

  std::deque<const Tile*> open;
  auto* startTile = &state->tilesInfo().getTile(u->x, u->y);
  open.push_back(startTile);
  while (!open.empty()) {
    const Tile* tile = open.front();
    open.pop_front();

    if (nodeGroupEdgeTracker_[tile - tilesData]) {
      int score = fleeScore[tile - tilesData] +
          tileSpotTakenTracker_[tile - tilesData] * 16;
      if (score < bestScore) {
        bestScore = score;
        bestPos = Position(tile->x + 2, tile->y + 2);
      }
      ++nFound;
      if (nFound >= 16) {
        break;
      }
      continue;
    }

    auto add = [&](const Tile* ntile) {
      if (!flying && !tile->entirelyWalkable && tile != startTile) {
        return;
      }
      auto& v = tileVisitTracker_[ntile - tilesData];
      if (v == visitedValue) {
        return;
      }
      v = visitedValue;
      if (startPos.distanceTo(ntile) <= kMaxFleeSearchDistance) {
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

  return bestPos;
}

Position TacticsState::findMoveAwayPos(
    State* state,
    Unit* u,
    Position source,
    float distance) {
  auto mapWidth = state->mapWidth();
  auto mapHeight = state->mapHeight();
  bool flying = u->flying();

  uint8_t visitedValue = ++visitNumber_;

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
      if (!flying && !tile->entirelyWalkable) {
        return;
      }
      auto& v = tileVisitTracker_[ntile - tilesData];
      if (v == visitedValue) {
        return;
      }
      v = visitedValue;
      if (utils::distance(ntile->x, ntile->y, startPos.x, startPos.y) <=
          kMaxFleeSearchDistance) {
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
}

void TacticsModule::onGameEnd(State* state) {}

void TacticsModule::formGroups(
    State* state,
    TacticsState& tstate,
    std::vector<Unit*>& leftoverWorkers,
    std::unordered_set<Unit*>& wasInAGroup) {
  // Create the different combat groups based on enemy
  // unit positions and specific ally roles like scouting
  tstate.createTacticsGroups(state, inBaseArea_);

  // Fill tstate.nodeInsideGroupTracker_ with nodes covered by groups
  tstate.collectMapNodesCoveredByGroups(state);

  // Then, assign ally units to the optimal group
  auto tasks = state->board()->tasksOfModule(this);
  tstate.assignUnits(state, wasInAGroup, leftoverWorkers, tasks);
}

std::pair<double, double> TacticsModule::distributeFightFlee(
    State* state,
    TacticsState& tstate,
    TacticsGroup& g,
    std::vector<Unit*>& fightUnits,
    std::vector<Unit*>& fleeUnits) {
  // Use Combat Simulation to predict the outcome of a fight
  std::unordered_map<Unit*, int> enemiesInRangeOfOurStaticDefence;
  std::unordered_map<Unit*, int> enemiesAlmostInRangeOfOurStaticDefence;
  auto fightScores = tstate.combatSimFightPrediction(
      state,
      g,
      enemiesInRangeOfOurStaticDefence,
      enemiesAlmostInRangeOfOurStaticDefence);
  g.task->isFighting = fightScores.groundFight;

  bool anyAntiAir = false;
  for (Unit* u : g.enemyUnits) {
    if (u->type == buildtypes::Terran_Bunker || u->type->hasAirWeapon) {
      anyAntiAir = true;
      break;
    }
  }

  // Create a list of my units in this group sorted by the distance to the
  // nearest enemy unit that they can attack.
  std::vector<std::pair<float, Unit*>> mySortedUnits;
  for (Unit* u : g.myUnits) {
    float nearestDistance = kfInfty;
    for (Unit* e : g.enemyUnits) {
      if (e->flying() ? u->type->hasAirWeapon : u->type->hasGroundWeapon) {
        float d = utils::distance(u->x, u->y, e->x, e->y);
        if (d < nearestDistance) {
          nearestDistance = d;
        }
      }
    }
    mySortedUnits.emplace_back(nearestDistance, u);
  }
  std::sort(mySortedUnits.begin(), mySortedUnits.end());

  // Iterate through them, assigning to fight or flee groups and assigning
  // target positions.
  std::unordered_map<Unit*, int> meleeTargetCount;
  for (auto& v : mySortedUnits) {
    Unit* u = v.second;

    bool anySpiderMinesNearby = false;
    Unit* target = tstate.getBestEnemyTarget(
        state,
        g,
        u,
        meleeTargetCount,
        lastTargetInRange_,
        anySpiderMinesNearby);

    Position moveTo = g.targetPos;
    bool runAway = tstate.shouldRunFromHiddenTarget(g, u, target);

    if (g.isIdleGroup) {
      moveTo = tstate.idleGroupTargetPos(state, u, inBaseArea_);
    }

    bool fight = u->flying() ? fightScores.airFight : fightScores.groundFight;
    auto* tilesData = state->tilesInfo().tiles.data();

    // Determine whether should run based on other considerations

    // Avoid fighting outside our static defense
    //
    // If we don't want to fight, and there's anything that could shoot us
    if (!fight && (!u->flying() || anyAntiAir)) {
      size_t index = &state->tilesInfo().getTile(u->x, u->y) - tilesData;
      if (tstate.nodeInsideGroupTracker_.at(index).group == &g ||
          tstate.nodeGroupEdgeTracker_.at(index) == &g) {
        runAway = true;
      }
    }

    // Drag Spider Mines into the enemy?
    if (anySpiderMinesNearby && !u->flying()) {
      runAway = false;
    }

    // If run away is set, remove target and mark units to flee
    if (runAway) {
      // First some rules for defending against melee units on one base
      Position runPos = kInvalidPosition;
      auto enemyIsMeleeOnly = [&]() {
        for (Unit* e : g.enemyUnits) {
          if (e->type != buildtypes::Zerg_Zergling &&
              e->type != buildtypes::Protoss_Zealot) {
            return false;
          }
        }
        return true;
      };
      if (state->areaInfo().numMyBases() == 1 &&
          state->areaInfo().myBase(0)->resourceDepot && enemyIsMeleeOnly()) {
        Position basePos = state->areaInfo().myBase(0)->resourceDepot->pos();
        Vec2 sumPos;
        int n = 0;
        for (Unit* u : state->unitsInfo().myWorkers()) {
          if (utils::distance(u, basePos) <= 4.0f * 10) {
            sumPos += u->posf();
            ++n;
          }
        }
        if (n) {
          runPos = Position(sumPos / n);
        }
        auto enemiesInRange = [&]() {
          for (Unit* e : g.enemyUnits) {
            for (Unit* u : state->unitsInfo().myWorkers()) {
              if (u->inRangeOf(e, 9)) {
                return true;
              }
            }
          }
          return false;
        };
        auto IAmInRange = [&]() {
          for (Unit* e : g.enemyUnits) {
            if (u->inRangeOf(e, 9)) {
              return true;
            }
          }
          return false;
        };
        if (utils::distance(u, runPos) <= 4.0f * 12 && enemiesInRange()) {
          runAway = false;
        } else if (IAmInRange()) {
          runPos = kInvalidPosition;
        }
      }
      if (runAway) {
        if (runPos == kInvalidPosition) {
          runPos = tstate.findRunPos(state, u, fleeScore_);
        }
        if (runPos != Position()) {
          ++tstate.tileSpotTakenTracker_[&state->tilesInfo().getTile(
                                             runPos.x, runPos.y) -
                                         tilesData];
          target = nullptr;
          moveTo = runPos;
        }
      }
    }

    // Distribute units to fight or flee groups
    if (moveTo == g.targetPos && !g.enemyUnits.empty()) {
      fightUnits.push_back(u);
      continue;
    }
    if (target) {
      fightUnits.push_back(u);
    } else {
      fleeUnits.push_back(u);
      moveUnit(state, tstate.srcUpcId_, u, moveTo);
    }
  }

  // Process units according to their assignments chosen above
  //
  // TODO: Constify this magic 0.11;
  // it's specifically chosen to exceed the 0.10 threshold in SquadCombat
  auto deleteScore = utils::clamp(fightScores.score / 2 + 1, 0.11, 0.99);
  return std::make_pair(deleteScore, 1 - deleteScore);
}

void TacticsModule::distributeLeftoverWorkers(
    std::unordered_set<Unit*>& unitSet,
    std::vector<Unit*>& leftoverWorkers,
    std::unordered_set<Unit*>& wasInAGroup) {
  // In order to get workers to go back to mining, we need to grab the unit
  // so that SquadCombat releases it. We will release it next update.
  // It doesn't matter which task we assign it to, so we assign all workers
  // that were previously in a group to the first task we process.
  if (!leftoverWorkers.empty()) {
    for (auto& u : leftoverWorkers) {
      if (wasInAGroup.find(u) != wasInAGroup.end()) {
        unitSet.insert(u);
      }
    }
    leftoverWorkers.clear();
  }
}

void TacticsModule::processNonFightFleeGroup(
    State* state,
    TacticsState& tstate,
    TacticsGroup& g,
    std::vector<Unit*>& leftoverWorkers,
    std::unordered_set<Unit*>& wasInAGroup) {
  std::unordered_set<Unit*> otherUnits;
  if (g.isScoutGroup) {
    for (auto& u : g.myUnits) {
      auto moveTo =
          tstate.scoutGroupTargetPos(state, g, u, scoutTarget_, rngEngine_);
      otherUnits.insert(u);
      moveUnit(state, tstate.srcUpcId_, u, moveTo);
    }
  }

  if (g.searchAndDestroy) {
    for (auto& u : g.myUnits) {
      auto moveTo = tstate.searchAndDestroyGroupTargetPos(
          state, u, searchAndDestroyTarget_, rngEngine_);
      otherUnits.insert(u);
      moveUnit(state, tstate.srcUpcId_, u, moveTo);
    }
  }

  // Processing leftover workers in case this is the only group we
  // have, highly unlikely but doesn't hurt
  distributeLeftoverWorkers(otherUnits, leftoverWorkers, wasInAGroup);
  g.task->setUnits(state, otherUnits);
}

void TacticsModule::processOrders(
    State* state,
    TacticsGroup& g,
    int srcUpcId,
    double deleteScore,
    double moveScore,
    std::vector<Unit*>& fightUnits,
    std::vector<Unit*>& fleeUnits,
    std::vector<Unit*>& leftoverWorkers,
    std::unordered_set<Unit*>& wasInAGroup) {
  // We always directly control fleeting units, so assign them to the task.
  // Attacking units will be assigned to some micro module
  std::unordered_set<Unit*> unitSet;
  for (Unit* u : fleeUnits) {
    unitSet.insert(u);
  }
  distributeLeftoverWorkers(unitSet, leftoverWorkers, wasInAGroup);
  g.task->setUnits(state, std::move(unitSet));

  if (!fightUnits.empty()) {
    auto upc = std::make_shared<UPCTuple>();
    for (Unit* u : fightUnits) {
      upc->unit[u] = 1.0f;
    }
    upc->scale = 1;

    UPCTuple::UnitMap map;
    for (Unit* e : g.enemyUnits) {
      map[e] = 1;
    }
    upc->position = std::move(map);
    VLOG(3) << "SCORE FROM TACTICS: " << deleteScore;
    VLOG(3) << "My units " << utils::unitsString(fightUnits);
    upc->command[Command::Delete] = deleteScore;
    upc->command[Command::Flee] = moveScore;
    state->board()->postUPC(std::move(upc), srcUpcId, this);
  }
}

void TacticsModule::process(State* state, int srcUpcId) {
  if (state->board()->hasKey("TacticsDisabled")) {
    for (auto& t : state->board()->tasksOfModule(this)) {
      auto task = std::static_pointer_cast<TacticsTask>(t);
      task->cancel(state);
    }
    return;
  }

  TacticsState tstate;
  tstate.srcUpcId_ = srcUpcId;
  std::vector<Unit*> leftoverWorkers;
  std::unordered_set<Unit*> wasInAGroup;

  formGroups(state, tstate, leftoverWorkers, wasInAGroup);

  for (auto& g : tstate.groups_) {
    if (!g.task) {
      auto upc = std::make_shared<UPCTuple>();
      upc->command[Command::Delete] = 0.5;
      upc->command[Command::Flee] = 0.5;
      auto upcId = state->board()->postUPC(std::move(upc), srcUpcId, this);
      state->board()->consumeUPC(upcId, this);
      auto task = std::make_shared<TacticsTask>(upcId);
      task->targetPos = g.targetPos;
      task->averagePos = g.averagePos;
      state->board()->postTask(task, this, true);
      g.task = task;
    }

    g.task->myUnits = g.myUnits;
    g.task->targetPos = g.targetPos;

    if (g.searchAndDestroy || g.isScoutGroup) {
      processNonFightFleeGroup(state, tstate, g, leftoverWorkers, wasInAGroup);
      continue;
    }

    std::vector<Unit*> fightUnits;
    std::vector<Unit*> fleeUnits;
    double fightScore, moveScore;
    std::tie(fightScore, moveScore) =
        distributeFightFlee(state, tstate, g, fightUnits, fleeUnits);
    processOrders(
        state,
        g,
        srcUpcId,
        fightScore,
        moveScore,
        fightUnits,
        fleeUnits,
        leftoverWorkers,
        wasInAGroup);
  }
}

void TacticsModule::moveUnit(
    State* state,
    UpcId srcUpcId,
    Unit* u,
    Position target) {
  if (VLOG_IS_ON(2)) {
    utils::drawLine(state, Position(u), target, tc::BW::Color::Green);
  }
  state->board()->postUPC(
      utils::makeSharpUPC(u, target, Command::Flee), srcUpcId, this);
  lastMove_[u] = state->currentFrame();
}

UpcId TacticsModule::findSourceUpc(State* state) {
  // Find 'Delete' UPC with unspecified (empty) units
  for (auto& upcs : state->board()->upcsWithCommand(Command::Delete, 0.5)) {
    if (upcs.second->unit.empty()) {
      return upcs.first;
    }
  }
  return -1;
}

void TacticsModule::step(State* state) {
  auto board = state->board();

  auto srcUpcId = findSourceUpc(state);
  if (srcUpcId < 0) {
    VLOG(4) << "No suitable source UPC";
    return;
  }

  board->consumeUPC(srcUpcId, this);

  if (lastUpdateInBaseArea_ == 0 ||
      state->currentFrame() - lastUpdateInBaseArea_ >= 60) {
    lastUpdateInBaseArea_ = state->currentFrame();
    utils::updateInBaseArea(state, inBaseArea_);
  }

  if (lastUpdateFleeScore_ == 0 ||
      state->currentFrame() - lastUpdateFleeScore_ >= 122) {
    lastUpdateFleeScore_ = state->currentFrame();
    updateFleeScore(state, fleeScore_);
  }

  if (lastProcess_ == 0 ||
      (uint64_t)(state->currentFrame() - lastProcess_) >=
          FLAGS_tactics_fight_or_flee_interval) {
    lastProcess_ = state->currentFrame();
    process(state, srcUpcId);
  }
}

} // namespace cherrypi
