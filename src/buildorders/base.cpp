/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "base.h"
#include "cherrypi.h"
#include "utils.h"

#include "modules/tactics.h"

#include <bwem/map.h>
#include <fmt/format.h>

namespace cherrypi {

namespace buildorders {
std::shared_ptr<AutoBuildTask>
createTask(UpcId srcUpcId, std::string name, State* state, Module* module) {
  auto* record =
      SubclassRegistry<ABBOBase, UpcId, State*, Module*>::record(name);
  if (record != nullptr) {
    return record->ctor(srcUpcId, state, module);
  }
  record =
      SubclassRegistry<ABBOBase, UpcId, State*, Module*>::record("ABBO" + name);
  if (record != nullptr) {
    return record->ctor(srcUpcId, state, module);
  }

  LOG(DFATAL) << "No such build order: " << name;
  return nullptr;
}
} // namespace buildorders

using namespace autobuild;
using namespace buildtypes;

void ABBOBase::findNaturalDefencePos(State* state) {
  if (lastFindNaturalDefencePosEnemyPos == enemyBasePos ||
      naturalPos == Position()) {
    return;
  }
  lastFindNaturalDefencePosEnemyPos = enemyBasePos;

  auto posTo = [&](Position dst) {
    auto path = state->map()->GetPath(
        BWAPI::Position(BWAPI::WalkPosition(naturalPos.x, naturalPos.y)),
        BWAPI::Position(BWAPI::WalkPosition(dst.x, dst.y)));

    if (path.size() <= 1) {
      // If we can't find a path to the destination, use a straight-line
      // approximation instead.
      auto diff = dst - naturalPos;
      auto ratio = 30.0 / std::max(1.0, diff.length());
      auto output = naturalPos + Position(diff.x * ratio, diff.y * ratio);
      VLOG(1) << "Failed to find path from natural at " << naturalPos << " to "
              << dst << ". Path size " << path.size() << ". Going with "
              << output;
      return output;
    } else {
      VLOG(1) << "Found path from natural at " << naturalPos << " to " << dst;
      auto pos = BWAPI::WalkPosition(path[1]->Center());
      return Position(pos.x, pos.y);
    }
  };
  // In practice, we never actually seem to find a path to homePosition.
  naturalDefencePos = posTo(enemyBasePos);
  mainNaturalChoke = posTo(homePosition);
}

Position ABBOBase::getStaticDefencePos(State* state, const BuildType* type) {
  // ugly hack. temporarily unset all reserved tiles
  //
  // DG: Could we instead add an argument to findBuildLocation which ignores
  // reserved tiles?
  auto& tilesInfo = state->tilesInfo();
  auto copy = tilesInfo.tiles;
  size_t stride = TilesInfo::tilesWidth - tilesInfo.mapTileWidth();
  Tile* ptr = tilesInfo.tiles.data();
  for (unsigned tileY = 0; tileY != tilesInfo.mapTileHeight();
       ++tileY, ptr += stride) {
    for (unsigned tileX = 0; tileX != tilesInfo.mapTileWidth();
         ++tileX, ++ptr) {
      ptr->reservedAsUnbuildable = false;
    }
  }
  constexpr int maxAllowableDistance = 4 * 7;
  VLOG(3) << "Looking for static defense position near " << naturalPos;
  Position r = builderhelpers::findBuildLocation(
      state,
      {naturalPos},
      type,
      {},
      [&](State* state, const BuildType* type, const Tile* tile) {
        if (utils::distance(tile->x, tile->y, naturalPos.x, naturalPos.y) >
            maxAllowableDistance) {
          return kfInfty;
        }
        const float a = utils::distance(
            tile->x, tile->y, naturalDefencePos.x, naturalDefencePos.y);
        const float b = utils::distance(
            tile->x, tile->y, mainNaturalChoke.x, mainNaturalChoke.y);
        float r = a * a + b * b;
        if (type->requiresCreep &&
            !builderhelpers::checkCreepAt(state, type, Position(tile))) {
          r += 64 * 64;
        }
        return r;
      });
  state->tilesInfo().tiles = copy;
  const auto distance = utils::distance(r.x, r.y, naturalPos.x, naturalPos.y);
  if (distance > maxAllowableDistance) {
    VLOG(3) << distance << " is too far: " << r;
    return Position();
  }
  VLOG(3) << distance << " is close enough: " << r;
  return r;
}

void ABBOBase::buildZergStaticDefense(
    BuildState& st,
    int numberDesired,
    const BuildType* morphedType,
    Position position,
    bool morphFirst) {
  auto morphedTotal = countPlusProduction(st, morphedType);
  auto creepsTotal = countPlusProduction(st, Zerg_Creep_Colony);
  auto creepsDone = countUnits(st, Zerg_Creep_Colony);

  auto morphsNeededForDesiredNumberOfSunkens =
      std::min(creepsDone, numberDesired - morphedTotal);
  auto creepsNeededForDesiredNumberOfSunkens =
      numberDesired - morphedTotal - creepsTotal;

  bool specificPosition = position != Position();
  position = specificPosition ? position : nextStaticDefencePos;

  auto doMorph = [&]() {
    if (morphsNeededForDesiredNumberOfSunkens > 0) {
      build(morphedType);
    }
  };
  auto addCreep = [&]() {
    if (creepsNeededForDesiredNumberOfSunkens > 0 &&
        (specificPosition || countPlusProduction(st, Zerg_Hatchery) >= 2) &&
        position != Position()) {
      build(Zerg_Creep_Colony, position);
    }
  };

  if (morphFirst) {
    addCreep();
    doMorph();
  } else {
    doMorph();
    addCreep();
  }
}

void ABBOBase::buildSunkens(
    autobuild::BuildState& st,
    int n,
    Position position,
    bool morphFirst) {
  buildZergStaticDefense(st, n, Zerg_Sunken_Colony, position, morphFirst);
}

void ABBOBase::buildSpores(
    autobuild::BuildState& st,
    int n,
    Position position,
    bool morphFirst) {
  buildZergStaticDefense(st, n, Zerg_Spore_Colony, position, morphFirst);
}

void ABBOBase::morphSunkens(autobuild::BuildState& st, int n) {
  if (has(st, Zerg_Creep_Colony) &&
      countPlusProduction(st, Zerg_Sunken_Colony) < n) {
    build(Zerg_Sunken_Colony);
  }
}

void ABBOBase::morphSpores(autobuild::BuildState& st, int n) {
  if (has(st, Zerg_Creep_Colony) &&
      countPlusProduction(st, Zerg_Spore_Colony) < n) {
    build(Zerg_Spore_Colony);
  }
}

void ABBOBase::expand(BuildState& st) {
  takeNBases(st, bases + 1);
}

void ABBOBase::takeNBases(BuildState& st, int basesDesired) {
  if (canExpand && bases < basesDesired && !st.isExpanding) {
    build(Zerg_Hatchery, nextBase);
  }
}

void ABBOBase::calculateArmySupply(const BuildState& st) {
  armySupply = 0.0;
  airArmySupply = 0.0;
  groundArmySupply = 0.0;
  for (auto& v : st.units) {
    const BuildType* t = v.first;
    if (!t->isWorker) {
      armySupply += t->supplyRequired * v.second.size();
      if (t->isFlyer) {
        airArmySupply += t->supplyRequired * v.second.size();
      } else {
        groundArmySupply += t->supplyRequired * v.second.size();
      }
    }
  }
  for (auto& v : st.production) {
    const BuildType* t = v.second;
    if (!t->isWorker) {
      armySupply += t->supplyRequired;
      if (t->isFlyer) {
        airArmySupply += t->supplyRequired;
      } else {
        groundArmySupply += t->supplyRequired;
      }
    }
  }
}

Position ABBOBase::findHatcheryPosNear(Position seedPos) {
  std::unordered_map<Unit*, int> coverage;

  // ugly hack. temporarily unset all reserved tiles
  auto& tilesInfo = state_->tilesInfo();
  auto copy = tilesInfo.tiles;
  size_t stride = TilesInfo::tilesWidth - tilesInfo.mapTileWidth();
  Tile* ptr = tilesInfo.tiles.data();
  for (unsigned tileY = 0; tileY != tilesInfo.mapTileHeight();
       ++tileY, ptr += stride) {
    for (unsigned tileX = 0; tileX != tilesInfo.mapTileWidth();
         ++tileX, ++ptr) {
      ptr->reservedAsUnbuildable = false;
    }
  }

  std::vector<Position> seedPositions;

  seedPositions.push_back(seedPos);

  Position r = builderhelpers::findBuildLocation(
      state_,
      seedPositions,
      Zerg_Hatchery,
      {},
      [&](State* state, const BuildType* type, const Tile* tile) {
        return 0.0f;
      });

  state_->tilesInfo().tiles = copy;

  return r;
}

Position ABBOBase::findSunkenPosNear(
    const BuildType* type,
    Position seedPos,
    bool coverMineralsOnly) {
  std::unordered_map<Unit*, int> coverage;

  // ugly hack. temporarily unset all reserved tiles
  auto& tilesInfo = state_->tilesInfo();
  auto copy = tilesInfo.tiles;
  size_t stride = TilesInfo::tilesWidth - tilesInfo.mapTileWidth();
  Tile* ptr = tilesInfo.tiles.data();
  for (unsigned tileY = 0; tileY != tilesInfo.mapTileHeight();
       ++tileY, ptr += stride) {
    for (unsigned tileX = 0; tileX != tilesInfo.mapTileWidth();
         ++tileX, ++ptr) {
      ptr->reservedAsUnbuildable = false;
    }
  }

  float coverageRange = 4 * 5.5;

  std::vector<Position> basePositions;

  std::vector<Unit*> existingStaticDefence;
  for (Unit* u : state_->unitsInfo().myUnitsOfType(type)) {
    existingStaticDefence.push_back(u);
  }
  for (Unit* u : state_->unitsInfo().myUnitsOfType(Zerg_Creep_Colony)) {
    existingStaticDefence.push_back(u);
  }

  if (!coverMineralsOnly) {
    for (Unit* building : state_->unitsInfo().myBuildings()) {
      coverage[building];
      for (Unit* u : existingStaticDefence) {
        if (utils::distance(u, building) <= coverageRange) {
          ++coverage[building];
        }
      }
    }
  }

  basePositions.push_back(seedPos);

  Position r = builderhelpers::findBuildLocation(
      state_,
      basePositions,
      Zerg_Creep_Colony,
      {},
      [&](State* state, const BuildType* type, const Tile* tile) {
        Position pos = Position(tile) + Position(4, 4);
        float r = 0.0f;
        for (auto& v : coverage) {
          if (utils::distance(pos, Position(v.first)) <= coverageRange) {
            r -= 1.25f - (v.second ? v.second : -12.0f);
          }
        }
        for (Unit* u : existingStaticDefence) {
          if (utils::distance(pos, Position(u)) < 12) {
            r += 24.0f;
          }
        }
        return r;
      });

  state_->tilesInfo().tiles = copy;

  return r;
}

Position ABBOBase::findSunkenPos(
    const BuildType* type,
    bool mainBaseOnly,
    bool coverMineralsOnly) {
  std::unordered_map<Unit*, int> coverage;

  // ugly hack. temporarily unset all reserved tiles
  auto& tilesInfo = state_->tilesInfo();
  auto copy = tilesInfo.tiles;
  size_t stride = TilesInfo::tilesWidth - tilesInfo.mapTileWidth();
  Tile* ptr = tilesInfo.tiles.data();
  for (unsigned tileY = 0; tileY != tilesInfo.mapTileHeight();
       ++tileY, ptr += stride) {
    for (unsigned tileX = 0; tileX != tilesInfo.mapTileWidth();
         ++tileX, ++ptr) {
      ptr->reservedAsUnbuildable = false;
    }
  }

  float coverageRange = 4 * 5.5;

  std::vector<Position> basePositions;

  std::vector<Unit*> existingStaticDefence;
  for (Unit* u : state_->unitsInfo().myUnitsOfType(type)) {
    existingStaticDefence.push_back(u);
  }
  for (Unit* u : state_->unitsInfo().myUnitsOfType(Zerg_Creep_Colony)) {
    existingStaticDefence.push_back(u);
  }

  if (!coverMineralsOnly) {
    for (Unit* building : state_->unitsInfo().myBuildings()) {
      coverage[building];
      for (Unit* u : existingStaticDefence) {
        if (utils::distance(u, building) <= coverageRange) {
          ++coverage[building];
        }
      }
    }
  }

  for (int i = 0; i != state_->areaInfo().numMyBases(); ++i) {
    auto resources = state_->areaInfo().myBaseResources(i);

    Unit* depot = state_->areaInfo().myBase(i)->resourceDepot;
    if (!depot) {
      continue;
    }

    for (Unit* r : resources) {
      coverage[r];
      for (Unit* u : existingStaticDefence) {
        if (utils::distance(u, r) <= coverageRange) {
          ++coverage[r];
        }
      }
    }

    basePositions.push_back(Position(depot));

    if (mainBaseOnly) {
      break;
    }
  }

  Position r = builderhelpers::findBuildLocation(
      state_,
      basePositions,
      Zerg_Creep_Colony,
      {},
      [&](State* state, const BuildType* type, const Tile* tile) {
        Position pos = Position(tile) + Position(4, 4);
        float r = 0.0f;
        for (auto& v : coverage) {
          if (utils::distance(pos, Position(v.first)) <= coverageRange) {
            r -= 1.25f - (v.second ? v.second : -12.0f);
          }
        }
        for (Unit* u : existingStaticDefence) {
          if (utils::distance(pos, Position(u)) < 12) {
            r += 24.0f;
          }
        }
        return r;
      });

  state_->tilesInfo().tiles = copy;

  return r;
}

void ABBOBase::draw(State* state) {
  AutoBuildTask::draw(state);

  if (VLOG_IS_ON(2)) {
    utils::drawBox(
        state,
        enemyBasePos,
        enemyBasePos + Position(16, 12),
        tc::BW::Color::Red);
    utils::drawBox(
        state,
        naturalPos + Position(1, 1),
        naturalPos + Position(15, 11),
        tc::BW::Color::Green);
    utils::drawBox(
        state, nextBase, nextBase + Position(16, 12), tc::BW::Color::Teal);
    utils::drawCircle(state, homePosition, 8, tc::BW::Color::White);
    utils::drawCircle(state, mainNaturalChoke, 8, tc::BW::Color::Grey);
    utils::drawCircle(state, naturalDefencePos, 8, tc::BW::Color::Black);
    utils::drawBox(
        state,
        nextStaticDefencePos,
        nextStaticDefencePos + Position(8, 8),
        tc::BW::Color::Yellow);
  }
}
void ABBOBase::preBuild(autobuild::BuildState& st) {
  calculateArmySupply(st);
  currentFrame = state_->currentFrame();

  if (!state_->unitsInfo().myResourceDepots().empty()) {
    homePosition.x = state_->unitsInfo().myResourceDepots().front()->x;
    homePosition.y = state_->unitsInfo().myResourceDepots().front()->y;
  } else if (!state_->unitsInfo().myBuildings().empty()) {
    homePosition.x = state_->unitsInfo().myBuildings().front()->x;
    homePosition.y = state_->unitsInfo().myBuildings().front()->y;
  } else if (!state_->unitsInfo().myUnits().empty()) {
    homePosition.x = state_->unitsInfo().myUnits().front()->x;
    homePosition.y = state_->unitsInfo().myUnits().front()->y;
  }

  // Count our bases and mineral patches
  mineralFields = 0;
  // TODO: Need to reset geysers = 0 but maybe code DEPENDS on this being wrong
  // now
  bases = 0;
  for (auto& area : state_->map()->Areas()) {
    for (auto& base : area.Bases()) {
      if (!base.BlockingMinerals().empty()) {
        continue;
      }
      Position pos(
          base.Location().x * tc::BW::XYWalktilesPerBuildtile,
          base.Location().y * tc::BW::XYWalktilesPerBuildtile);
      const Tile& tile = state_->tilesInfo().getTile(pos.x, pos.y);
      Unit* building = tile.building;
      if (building) {
        if (building->isMine) {
          ++bases;
          mineralFields += base.Minerals().size();
          geysers += base.Geysers().size();
        }
      }
    }
  }

  // Choose next expansion base
  std::vector<std::tuple<Position, double>> allBases;
  for (auto& area : state_->areaInfo().areas()) {
    for (auto& centerPos : area.baseLocations) {
      // Base locations are center-of-building -- move to top left instead
      Position pos = centerPos - Position(8, 6);
      if (!builderhelpers::canBuildAt(state_, Zerg_Hatchery, pos, true)) {
        continue;
      }

      int distanceHome = 0;
      int distanceEnemy = 0;
      int distanceMiddle = 0;

      // Avoid building far from home
      distanceHome = state_->areaInfo().walkPathLength(pos, homePosition);

      // Avoid building near the enemy
      if (enemyBasePos != Position() && bases >= 2) {
        distanceEnemy = state_->areaInfo().walkPathLength(pos, enemyBasePos);
      }

      // Avoid building towards the middle of the map
      distanceHome = distanceHome > 0 ? distanceHome : kdInfty;
      distanceEnemy = distanceEnemy > 0 ? distanceEnemy : 0;
      distanceMiddle = utils::distance(
          pos, Position(state_->mapWidth() / 2, state_->mapHeight() / 2));

      // Against opponents playing less mobile races, further prefer bases far
      // from them
      auto raceMobility = [](tc::BW::Race race) {
        if (race == +tc::BW::Race::Terran) {
          return 0;
        }
        if (race == +tc::BW::Race::Protoss) {
          return 1;
        }
        if (race == +tc::BW::Race::Unknown) {
          return 2;
        }
        return 3;
      };
      auto raceEnemy = state_->raceFromClient(state_->firstOpponent());
      int mobilityUs = raceMobility(state_->myRace());
      int mobilityEnemy = raceMobility(raceEnemy);
      if (preferSafeExpansions) {
        distanceEnemy *= 1.0 + std::max(0, mobilityUs - mobilityEnemy);
      }

      auto isValidResource = [&pos](Unit* unit) {
        return utils::distance(unit, pos) < 48 && unit->unit.resources > 300;
      };
      int baseMinerals = std::count_if(
          area.minerals.begin(), area.minerals.end(), isValidResource);
      int baseGeysers = std::count_if(
          area.geysers.begin(), area.geysers.end(), isValidResource);
      baseMinerals = std::min(8, baseMinerals);
      baseGeysers = std::min(1, baseGeysers);
      double distanceScore = distanceHome - distanceMiddle - distanceEnemy;
      double multiplierGas = geysers < 2 ? 1000 : 100;
      double score =
          distanceScore - 16 * baseMinerals - multiplierGas * baseGeysers;
      allBases.emplace_back(pos, score);
      VLOG(3) << fmt::format(
          "{}: dH {} dE {} dM {} mG {} bM {} bG {} -> s {}",
          utils::positionString(pos),
          distanceHome,
          distanceEnemy,
          distanceMiddle,
          multiplierGas,
          baseMinerals,
          baseGeysers,
          score);
    }
  }

  auto* bestBase = utils::getBestScorePointer(
      allBases, [&](auto& v) { return std::get<1>(v); });
  if (bestBase) {
    canExpand = true;
    nextBase = std::get<0>(*bestBase);
  } else {
    canExpand = false;
    nextBase = Position();
  }

  nextStaticDefencePos = getStaticDefencePos(state_, Zerg_Creep_Colony);
  VLOG(2) << "Assigned static defense position: " << nextStaticDefencePos;

  if (!hasFoundEnemyBase) {
    for (auto tilePos : state_->map()->StartingLocations()) {
      Position pos(
          tilePos.x * tc::BW::XYWalktilesPerBuildtile,
          tilePos.y * tc::BW::XYWalktilesPerBuildtile);
      auto& tile = state_->tilesInfo().getTile(pos.x, pos.y);
      if (tile.building && tile.building->isEnemy) {
        enemyBasePos = pos;
        hasFoundEnemyBase = true;
        break;
      } else if (tile.lastSeen == 0) {
        enemyBasePos = pos;
      }
    }
    if (!hasFoundEnemyBase) {
      for (int i = 0; i != 3; ++i) {
        bool found = false;
        for (Unit* u : state_->unitsInfo().enemyUnits()) {
          if (i == 0 ? u->type->isBuilding
                     : i == 1
                      ? (u->type->hasGroundWeapon || u->type->hasAirWeapon) &&
                          !u->type->isWorker
                      : true) {
            Position nearestPos;
            float nearestDistance = kfInfty;
            for (auto tilePos : state_->map()->StartingLocations()) {
              Position pos(
                  tilePos.x * tc::BW::XYWalktilesPerBuildtile,
                  tilePos.y * tc::BW::XYWalktilesPerBuildtile);
              auto& tile = state_->tilesInfo().getTile(pos.x, pos.y);
              if (!tile.building) {
                float d = utils::distance(u->x, u->y, pos.x, pos.y);
                if (d < nearestDistance && d < 4 * 30) {
                  nearestDistance = d;
                  nearestPos = pos;
                }
              }
            }
            if (nearestPos != Position()) {
              enemyBasePos = nearestPos;
              found = true;
              break;
            }
          }
        }
        if (found) {
          break;
        }
      }
    }
  }

  if (naturalPos == Position() && state_->areaInfo().numMyBases() > 1) {
    Unit* depot = state_->areaInfo().myBase(1)->resourceDepot;
    if (depot) {
      naturalPos = depot->pos();
    }
  }

  if (naturalPos == Position() && nextBase != Position()) {
    naturalPos = nextBase;
    findNaturalDefencePos(state_);
  }

  if (state_->currentFrame() - lastUpdateInBaseArea >= 90) {
    lastUpdateInBaseArea = state_->currentFrame();
    utils::updateInBaseArea(state_, inBaseArea);
  }

  shouldExpand = canExpand &&
      bases <
          std::max(
              ((int)state_->unitsInfo().myResourceDepots().size() + 1) / 2 + 1,
              2);
  forceExpand = canExpand &&
      state_->unitsInfo().myWorkers().size() >= mineralFields * 1.8;
  if (forceExpand) {
    shouldExpand = true;
  }

  auto& tilesInfo = state_->tilesInfo();
  auto* tilesData = tilesInfo.tiles.data();

  enemyWorkerCount = 0;
  enemyGasCount = 0;
  enemyZealotCount = 0;
  enemyDragoonCount = 0;
  enemyDarkTemplarCount = 0;
  enemyHighTemplarCount = 0;
  enemyArchonCount = 0;
  enemyReaverCount = 0;
  enemyVultureCount = 0;
  enemyGoliathCount = 0;
  enemyTankCount = 0;
  enemyMissileTurretCount = 0;
  enemyCorsairCount = 0;
  enemyScoutCount = 0;
  enemyObserverCount = 0;
  enemyStargateCount = 0;
  enemyWraithCount = 0;
  enemyBattlecruiserCount = 0;
  enemyValkyrieCount = 0;
  enemyStaticDefenceCount = 0;
  enemyBarracksCount = 0;
  enemyRefineryCount = 0;
  enemyAcademyCount = 0;
  enemyZerglingCount = 0;
  enemyHydraliskCount = 0;
  enemyMutaliskCount = 0;
  enemyScourgeCount = 0;
  enemySunkenCount = 0;
  enemySporeCount = 0;
  enemyMarineCount = 0;
  enemyMedicCount = 0;
  enemyFirebatCount = 0;
  enemyFactoryCount = 0;
  enemyLairCount = 0;
  enemySpireCount = 0;
  enemyCloakedUnitCount = 0;
  enemyBuildingCount = 0;
  enemyGatewayCount = 0;
  enemyCyberneticsCoreCount = 0;
  enemyStargateCount = 0;
  enemyScienceVesselCount = 0;
  enemyArbiterCount = 0;
  enemyForgeCount = 0;
  enemyShuttleCount = 0;
  enemyResourceDepots = 0;
  enemyGasUnits = 0;
  enemyTemplarArchivesCount = 0;

  enemySupplyInOurBase = 0.0;
  enemyArmySupplyInOurBase = 0.0;
  enemyArmySupply = 0.0;
  enemyGroundArmySupply = 0.0;
  enemyAirArmySupply = 0.0;
  enemyAntiAirArmySupply = 0.0;
  enemyAttackingArmySupply = 0.0;
  enemyAttackingGroundArmySupply = 0.0;
  enemyAttackingAirArmySupply = 0.0;
  enemyAttackingWorkerCount = 0;
  enemyLargeArmySupply = 0.0;
  enemySmallArmySupply = 0.0;
  enemyBiologicalArmySupply = 0.0;

  enemyProxyBuildingCount = 0;
  enemyProxyGatewayCount = 0;
  enemyProxyBarracksCount = 0;
  enemyProxyForgeCount = 0;
  enemyProxyCannonCount = 0;

  enemyForgeIsSpinning = false;

  // We should just make this a map.
  auto countUnit = [&](Unit* u) {
    if (u->type == Terran_SCV || u->type == Protoss_Probe ||
        u->type == Zerg_Drone) {
      ++enemyWorkerCount;
    } else if (
        u->type == Terran_Refinery || u->type == Protoss_Assimilator ||
        u->type == Zerg_Extractor) {
      ++enemyGasCount;
    } else if (u->type == Protoss_Zealot) {
      ++enemyZealotCount;
    } else if (u->type == Protoss_Dragoon) {
      ++enemyDragoonCount;
    } else if (u->type == Protoss_Dark_Templar) {
      ++enemyDarkTemplarCount;
    } else if (u->type == Protoss_High_Templar) {
      ++enemyHighTemplarCount;
    } else if (u->type == Protoss_Archon) {
      ++enemyArchonCount;
    } else if (u->type == Protoss_Reaver) {
      ++enemyReaverCount;
    } else if (u->type == Terran_Vulture) {
      ++enemyVultureCount;
    } else if (u->type == Terran_Goliath) {
      ++enemyGoliathCount;
    } else if (
        u->type == Terran_Siege_Tank_Tank_Mode ||
        u->type == Terran_Siege_Tank_Siege_Mode) {
      ++enemyTankCount;
    } else if (u->type == Terran_Missile_Turret) {
      ++enemyMissileTurretCount;
    } else if (u->type == Protoss_Corsair) {
      ++enemyCorsairCount;
    } else if (u->type == Protoss_Scout) {
      ++enemyScoutCount;
    } else if (u->type == Protoss_Observer) {
      ++enemyObserverCount;
    } else if (u->type == Protoss_Stargate) {
      ++enemyStargateCount;
    } else if (u->type == Terran_Wraith) {
      ++enemyWraithCount;
    } else if (u->type == Terran_Valkyrie) {
      ++enemyValkyrieCount;
    } else if (u->type == Terran_Battlecruiser) {
      ++enemyBattlecruiserCount;
    } else if (u->type == Terran_Barracks) {
      ++enemyBarracksCount;
    } else if (u->type == Terran_Refinery) {
      ++enemyRefineryCount;
    } else if (u->type == Terran_Academy) {
      ++enemyAcademyCount;
    } else if (u->type == Protoss_Gateway) {
      ++enemyGatewayCount;
    } else if (u->type == Protoss_Cybernetics_Core) {
      ++enemyCyberneticsCoreCount;
    } else if (u->type == Protoss_Forge) {
      ++enemyForgeCount;
      if (u->upgrading()) {
        enemyForgeIsSpinning = true;
      }
    } else if (u->type == Zerg_Zergling) {
      ++enemyZerglingCount;
    } else if (u->type == Zerg_Hydralisk) {
      ++enemyHydraliskCount;
    } else if (u->type == Zerg_Mutalisk) {
      ++enemyMutaliskCount;
    } else if (u->type == Zerg_Scourge) {
      ++enemyScourgeCount;
    } else if (u->type == Zerg_Sunken_Colony) {
      ++enemySunkenCount;
    } else if (u->type == Zerg_Spore_Colony) {
      ++enemySporeCount;
    } else if (u->type == Terran_Marine) {
      ++enemyMarineCount;
    } else if (u->type == Terran_Medic) {
      ++enemyMedicCount;
    } else if (u->type == Terran_Firebat) {
      ++enemyFirebatCount;
    } else if (u->type == Terran_Factory) {
      ++enemyFactoryCount;
    } else if (u->type == Zerg_Lair) {
      ++enemyLairCount;
    } else if (u->type == Zerg_Spire) {
      ++enemySpireCount;
    } else if (u->type == Terran_Science_Vessel) {
      ++enemyScienceVesselCount;
    } else if (u->type == Protoss_Arbiter) {
      ++enemyArbiterCount;
    } else if (u->type == Protoss_Shuttle) {
      ++enemyShuttleCount;
    } else if (u->type == Protoss_Templar_Archives) {
      ++enemyTemplarArchivesCount;
    }
    if (u->type->isBuilding) {
      ++enemyBuildingCount;
    }
    if (u->cloaked() || u->burrowed()) {
      ++enemyCloakedUnitCount;
    }
    if (u->type->isBuilding &&
        (u->type == Terran_Bunker || u->type->hasGroundWeapon ||
         u->type->hasAirWeapon)) {
      ++enemyStaticDefenceCount;
    }
    if (u->type->isResourceDepot) {
      ++enemyResourceDepots;
      if (!enemyHasExpanded && u->id >= 0) {
        if (utils::distance(u->x, u->y, enemyBasePos.x, enemyBasePos.y) > 48) {
          enemyHasExpanded = true;
        }
      }
    }
    if (u->type->gasCost || u->type->isRefinery) {
      ++enemyGasUnits;
    }
    if (!u->type->isWorker) {
      enemyArmySupply += u->type->supplyRequired;
      if (u->flying()) {
        enemyAirArmySupply += u->type->supplyRequired;
      } else {
        enemyGroundArmySupply += u->type->supplyRequired;
      }
      if (u->type->hasAirWeapon || u->type == Protoss_Carrier) {
        enemyAntiAirArmySupply += u->type->supplyRequired;
      }
      float nearestEnemyBaseDistance = kfInfty;
      for (Position pos : state_->areaInfo().candidateEnemyStartLocations()) {
        nearestEnemyBaseDistance = std::min(
            nearestEnemyBaseDistance,
            state_->areaInfo().walkPathLength(u->pos(), pos));
      }
      float myBaseDistance =
          state_->areaInfo().walkPathLength(u->pos(), homePosition);
      if (myBaseDistance < nearestEnemyBaseDistance * 1.25) {
        enemyAttackingArmySupply += u->type->supplyRequired;
        if (u->flying()) {
          enemyAttackingAirArmySupply += u->type->supplyRequired;
        } else {
          enemyAttackingGroundArmySupply += u->type->supplyRequired;
        }
        if (u->type->isBuilding) {
          ++enemyProxyBuildingCount;
        }
        if (u->type == Protoss_Gateway) {
          ++enemyProxyGatewayCount;
        } else if (u->type == Terran_Barracks) {
          ++enemyProxyBarracksCount;
        } else if (u->type == Protoss_Forge) {
          ++enemyProxyForgeCount;
        } else if (u->type == Protoss_Photon_Cannon) {
          ++enemyProxyCannonCount;
        }
      }
      if (u->type->size == 1) {
        enemySmallArmySupply += u->type->supplyRequired;
      }
      if (u->type->size == 3) {
        enemyLargeArmySupply += u->type->supplyRequired;
      }
      if (u->type->isBiological) {
        enemyBiologicalArmySupply += u->type->supplyRequired;
      }
    } else {
      if (utils::distance(u->x, u->y, homePosition.x, homePosition.y) <
          utils::distance(u->x, u->y, enemyBasePos.x, enemyBasePos.y) * 1.25) {
        enemyAttackingWorkerCount += u->type->supplyRequired;
      }
    }
    const Tile* tile = tilesInfo.tryGetTile(u->x, u->y);
    if (tile) {
      size_t index = tile - tilesData;
      if (inBaseArea[index]) {
        enemySupplyInOurBase += u->type->supplyRequired;
        if (!u->type->isWorker) {
          enemyArmySupplyInOurBase += u->type->supplyRequired;
        }
      }
    }
  };

  std::for_each(
      state_->unitsInfo().enemyUnits().begin(),
      state_->unitsInfo().enemyUnits().end(),
      countUnit);

  if (enemyFactoryCount == 0) {
    if (enemyVultureCount + enemyGoliathCount + enemyTankCount) {
      enemyFactoryCount = 1;
    }
  }
  if (!enemyHasExpanded && enemyResourceDepots >= 2) {
    enemyHasExpanded = true;
  }

  if (state_->currentFrame() < 3 * 60 * 24 && enemyBarracksCount >= 2) {
    enemyIsRushing = true;
  }
  if (state_->currentFrame() < 4 * 60 * 24 && enemyArmySupply > 4) {
    enemyIsRushing = true;
  }
  if (state_->currentFrame() > 6 * 60 * 24) {
    enemyIsRushing = false;
  }

  // We should probably just make a map out of this.
  myLarvaCount = state_->unitsInfo().myUnitsOfType(Zerg_Larva).size();
  mySunkenCount = state_->unitsInfo().myUnitsOfType(Zerg_Sunken_Colony).size();
  mySporeCount = state_->unitsInfo().myUnitsOfType(Zerg_Spore_Colony).size();
  myDroneCount = state_->unitsInfo().myUnitsOfType(Zerg_Drone).size();
  myZerglingCount = state_->unitsInfo().myUnitsOfType(Zerg_Zergling).size();
  myHydraliskCount = state_->unitsInfo().myUnitsOfType(Zerg_Hydralisk).size();
  myMutaliskCount = state_->unitsInfo().myUnitsOfType(Zerg_Mutalisk).size();
  myScourgeCount = state_->unitsInfo().myUnitsOfType(Zerg_Scourge).size();
  myLurkerCount = state_->unitsInfo().myUnitsOfType(Zerg_Lurker).size();
  myUltraliskCount = state_->unitsInfo().myUnitsOfType(Zerg_Ultralisk).size();
  myGuardianCount = state_->unitsInfo().myUnitsOfType(Zerg_Guardian).size();
  myDefilerCount = state_->unitsInfo().myUnitsOfType(Zerg_Defiler).size();
  myCompletedHatchCount =
      state_->unitsInfo().myCompletedUnitsOfType(Zerg_Hatchery).size() +
      state_->unitsInfo().myUnitsOfType(Zerg_Lair).size() +
      state_->unitsInfo().myUnitsOfType(Zerg_Hive).size();

  isLosingAnOverlord = false;
  for (Unit* u : state_->unitsInfo().myCompletedUnitsOfType(Zerg_Overlord)) {
    if (u->unit.health <= u->type->maxHp / 2) {
      isLosingAnOverlord = true;
    }
  }

  auto enemyRaceBB = tc::BW::Race::_from_integral_nothrow(
      state_->board()->get<int>(Blackboard::kEnemyRaceKey));
  if (enemyRaceBB) {
    enemyRace = *enemyRaceBB;
  }

  if (currentFrame < 15 * 60 * 5) {
    if (enemyAttackingWorkerCount >= 3) {
      postBlackboardKey(Blackboard::kMinScoutFrameKey, 0);
    }
  }
  enemyProximity = enemyAttackingArmySupply / std::max(1.0, enemyArmySupply);

  preBuild2(st);

  weArePlanningExpansion = false;
}

void ABBOBase::postBuild(autobuild::BuildState& st) {
  postBuild2(st);
}

void ABBOBase::buildStep(BuildState& st) {
  calculateArmySupply(st);

  if (st.frame - currentFrame <= 15 * 30 && st.isExpanding) {
    weArePlanningExpansion = true;
  }

  buildStep2(st);

  if (st.frame < 15 * 60 * 5) {
    if (enemyAttackingWorkerCount >= 3) {
      if (!hasOrInProduction(st, Zerg_Spawning_Pool)) {
        buildN(Zerg_Spawning_Pool, 1);
        buildN(Zerg_Drone, 8);
      } else {
        buildN(Zerg_Zergling, std::max(enemyAttackingWorkerCount, 4));
      }
    }
  }

  if (st.frame < 15 * 60 * 4 && enemyAttackingWorkerCount >= 2 &&
      st.workers < 13) {
    buildN(Zerg_Zergling, enemyAttackingWorkerCount);
  }

  if (autoUpgrade) {
    if (st.workers >= 50 &&
        ((armySupply > enemyArmySupply && armySupply >= 40.0) ||
         armySupply >= 70.0)) {
      if (countPlusProduction(st, Zerg_Mutalisk) >= 10) {
        upgrade(Zerg_Flyer_Attacks_3);
        upgrade(Zerg_Flyer_Carapace_3);
      }
      if (countPlusProduction(st, Zerg_Hydralisk) +
              countPlusProduction(st, Zerg_Lurker) * 1 >=
          15) {
        upgrade(Zerg_Missile_Attacks_3);
      }
      if (countPlusProduction(st, Zerg_Hydralisk) >= 8) {
        upgrade(Grooved_Spines) && upgrade(Muscular_Augments);
      }
      if (countPlusProduction(st, Zerg_Zergling) >= 20) {
        upgrade(Zerg_Melee_Attacks_3);
      }
      if (has(st, Zerg_Hive) || countPlusProduction(st, Zerg_Zergling) >= 40) {
        upgrade(Zerg_Carapace_3);
        upgrade(Adrenal_Glands);
      }
      upgrade(Pneumatized_Carapace) && has(st, Zerg_Hive) && upgrade(Antennae);
    }
    if (st.workers >= 30) {
      if (armySupply > enemyArmySupply || armySupply >= 14) {
        upgrade(Burrowing);
      }
      upgrade(Metabolic_Boost);
    }
  }

  if (buildExtraOverlordsIfLosingThem && isLosingAnOverlord) {
    int n = enemyCorsairCount + enemyWraithCount ? 2 : 1;
    if (countProduction(st, Zerg_Overlord) < n &&
        st.usedSupply[tc::BW::Race::Zerg] >=
            st.maxSupply[tc::BW::Race::Zerg] - 8 * n) {
      build(Zerg_Overlord);
    }
  }

  if (autoExpand) {
    if (forceExpand && !st.isExpanding) {
      build(Zerg_Hatchery, nextBase);
      st.autoBuildRefineries = false;
    }
  }
}
} // namespace cherrypi
