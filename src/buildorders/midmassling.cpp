/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "base.h"

#include <bwem/map.h>

namespace cherrypi {

class ABBOmidmassling : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  bool buildExtractor = false;
  bool hasBuiltExtractor = false;
  bool attacking = false;
  int buildArmyUntil = 0;
  int attackUntil = 0;
  double buildArmySupply = 0.0;

  Position nextSporePos;

  bool waitForPool = false;
  bool waitDoNothing = false;

  bool goHydras = false;

  double highestArmySupply = 0.0;

  Position buildNydusPosition;

  Position findNydusPosition(Position farAwayFrom = kInvalidPosition) {
    using namespace buildtypes;

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

    std::vector<Position> basePositions;

    for (int i = 0; i != state_->areaInfo().numMyBases(); ++i) {
      Unit* depot = state_->areaInfo().myBase(i)->resourceDepot;
      if (!depot) {
        continue;
      }

      bool alreadyHasNydus = false;
      for (Unit* u : state_->unitsInfo().myUnitsOfType(Zerg_Nydus_Canal)) {
        if (utils::distance(depot, u) <= 4 * 18) {
          alreadyHasNydus = true;
        }
      }

      if (alreadyHasNydus) {
        continue;
      }

      basePositions.push_back(Position(depot));
    }

    if (basePositions.empty()) {
      state_->tilesInfo().tiles = copy;
      return kInvalidPosition;
    }

    Position r = builderhelpers::findBuildLocation(
        state_,
        basePositions,
        Zerg_Nydus_Canal,
        {},
        [&](State* state, const BuildType* type, const Tile* tile) {
          Position pos = Position(tile) + Position(4, 4);
          float r = 0.0f;
          for (Unit* u : state->unitsInfo().myWorkers()) {
            if (utils::distance(pos, u) < 4 * 18) {
              r -= 1.0f;
            }
          }
          if (farAwayFrom != kInvalidPosition) {
            r -= utils::distance(pos, farAwayFrom);
          }
          return r;
        });

    state_->tilesInfo().tiles = copy;

    return r;
  }

  //  bool buildNydusExit(Position pos) {
  //    for (Unit* u : state_->unitsInfo().myCompletedUnitsOfType(
  //             buildtypes::Zerg_Nydus_Canal)) {
  //      if (!u->associatedUnit) {
  //        state->board()->postCommand(
  //            tc::Client::Command(
  //                tc::BW::Command::CommandUnit,
  //                u->id,
  //                tc::BW::UnitCommandType::Build,
  //                -1,
  //                pos.x,
  //                pos.y,
  //                u->type->unit),
  //            this->upcId());
  //        return true;
  //      }
  //    }
  //    return false;
  //  }

  virtual void preBuild2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    if (st.usedSupply[tc::BW::Race::Zerg] >= 190.0 ||
        st.frame >= 24 * 60 * 40) {
      attacking = true;
    }
    if (st.usedSupply[tc::BW::Race::Zerg] < 160.0) {
      attacking = false;
    }
    if (st.frame < attackUntil) {
      attacking = true;
    }
    postBlackboardKey("TacticsAttack", attacking);
    postBlackboardKey(Blackboard::kMinScoutFrameKey, 24 * 60);

    if (armySupply < enemyAttackingGroundArmySupply * 1.25 -
                (bases == 2 && countPlusProduction(st, Zerg_Drone) < 22
                     ? countPlusProduction(st, Zerg_Sunken_Colony) * 4
                     : 0) ||
        armySupply < std::min(enemyArmySupplyInOurBase, 2.0)) {
      buildArmyUntil = st.frame + 24 * 20;
      buildArmySupply = std::max(
          enemyAttackingArmySupply + 6.0 -
              countPlusProduction(st, Zerg_Sunken_Colony) * 6,
          enemyAttackingArmySupply * 0.66);
    }

    if (armySupply > enemyGroundArmySupply) {
      attackUntil = st.frame + 24 * 30;
    }
    if (armySupply > enemyAttackingGroundArmySupply * 2 &&
        armySupply >= enemyArmySupply * 0.4) {
      attackUntil = st.frame + 24 * 20;
    }

    if (enemyRace == +tc::BW::Race::Terran &&
        armySupply >= enemyAttackingArmySupply) {
      attackUntil = st.frame + 24 * 20;
    }

    if (st.frame < 24 * 60 * 6 &&
        countPlusProduction(st, Zerg_Sunken_Colony) >= enemyArmySupply / 5.0) {
      attackUntil = st.frame + 24 * 5;
    }

    if (st.frame < buildArmyUntil) {
      if (armySupply > enemyGroundArmySupply * 1.15) {
        attackUntil = st.frame + 24 * 30;
      }
    } else {
      buildArmySupply = 0.0;
    }

    nextSporePos = findSunkenPos(Zerg_Spore_Colony);

    waitForPool = false;
    if (!enemyHasExpanded && enemyForgeCount + enemyStaticDefenceCount == 0 &&
        st.frame < 24 * 60 * 3 + 24 * 30) {
      if (!state_->unitsInfo().myUnitsOfType(Zerg_Spawning_Pool).empty() &&
          state_->unitsInfo()
              .myCompletedUnitsOfType(Zerg_Spawning_Pool)
              .empty()) {
        waitForPool = true;
      }
    }
    waitDoNothing = false;
    if (st.frame < 24 * 60 * 3 + 24 * 45 &&
        !state_->unitsInfo()
             .myCompletedUnitsOfType(Zerg_Spawning_Pool)
             .empty()) {
      if (!enemyHasExpanded && enemyForgeCount + enemyStaticDefenceCount == 0 &&
          enemyArmySupply == 0.0 && countPlusProduction(st, Zerg_Drone) >= 11 &&
          st.minerals < 300) {
        waitDoNothing = true;
      }
    }

    preferSafeExpansions = bases >= 3;

    highestArmySupply = std::max(highestArmySupply, armySupply);

    buildNydusPosition = kInvalidPosition;
    //    if (countPlusProduction(st, Zerg_Nydus_Canal) < bases) {
    //      Position farAwayFrom = kInvalidPosition;
    //      for (Unit* u : state_->unitsInfo().myCompletedUnitsOfType(
    //               buildtypes::Zerg_Nydus_Canal)) {
    //        if (!u->associatedUnit) {
    //          farAwayFrom = u;
    //          break;
    //        }
    //      }
    //      buildNydusPosition = findNydusPosition(farAwayFrom);
    //      if (buildNydusPosition != kInvalidPosition &&
    //          buildNydusExit(buildNydusPosition)) {
    //        buildNydusPosition = kInvalidPosition;
    //      }
    //    }
  }

  virtual void buildStep2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    int droneCount = countPlusProduction(st, Zerg_Drone);

    st.autoBuildRefineries =
        (countPlusProduction(st, Zerg_Extractor) == 0 && droneCount >= 13) ||
        st.frame >= 15 * 60 * 9;

    int hatcheries = countPlusProduction(st, Zerg_Hatchery);

    if (st.frame < 24 * 60 * 5) {
      bool beingRushed = enemyProxyGatewayCount + enemyProxyBarracksCount +
              enemyProxyForgeCount + enemyProxyCannonCount ||
          enemyAttackingArmySupply >= 4.0;
      if (waitForPool) {
        if (hatcheries < 3) {
          if (bases == 1 && !beingRushed) {
            build(Zerg_Hatchery, nextBase);
          } else {
            build(Zerg_Hatchery);
          }
        }
        build(Zerg_Zergling);
        buildN(Zerg_Drone, 10);
        if (beingRushed) {
          if (bases >= 2) {
            buildSunkens(st, 2);
          } else {
            // buildN(Zerg_Sunken_Colony, 1);
          }
        }
        return;
      }
      if (waitDoNothing) {
        return;
      }
    }

    int zerglingCount = countPlusProduction(st, Zerg_Zergling);
    int hydraliskCount = countPlusProduction(st, Zerg_Hydralisk);

    build(Zerg_Zergling);
    if (goHydras &&
        (zerglingCount >= hydraliskCount ||
         enemyAirArmySupply / enemyArmySupply > hydraliskCount / armySupply)) {
      build(Zerg_Hydralisk);
    }

    bool wantsSpores = false;

    bool enemyIsOneBase =
        !enemyHasExpanded && enemyForgeCount + enemyStaticDefenceCount == 0;
    if (st.frame >= 24 * 60 * 5 + (enemyGroundArmySupply * 24 * 3)) {
      enemyIsOneBase = false;
    }

    if (currentFrame >= 24 * 60 * 3) {
      if (armySupply >= droneCount * 0.66 &&
          countProduction(st, Zerg_Drone) <
              (groundArmySupply > enemyGroundArmySupply ? 2 : 1) *
                  (bases >= 4 && armySupply >= 34.0 ? 2 : 1)) {
        buildN(Zerg_Drone, 70);
      }
      if (armySupply >= 28.0) {
        upgrade(Plague);
        buildN(Zerg_Defiler, 3);
        upgrade(Consume);

        upgrade(Zerg_Carapace_1) && upgrade(Zerg_Carapace_2) &&
            upgrade(Zerg_Carapace_3);
        upgrade(Zerg_Melee_Attacks_1) && upgrade(Zerg_Melee_Attacks_2) &&
            upgrade(Zerg_Melee_Attacks_3);
        upgrade(Adrenal_Glands);

        upgrade(Pneumatized_Carapace);
      }

      if (highestArmySupply >= 14.0 &&
          armySupply >= std::min(enemyArmySupply - 4, 20.0)) {
        //        if (has(st, Zerg_Hive) && buildNydusPosition !=
        //        kInvalidPosition &&
        //            countProduction(st, Zerg_Nydus_Canal) == 0) {
        //          build(Zerg_Nydus_Canal, buildNydusPosition);
        //        }
        buildN(Zerg_Drone, 36);
      }

      if (highestArmySupply >= 6.0 && attacking) {
        int n = 4;
        if (droneCount >= (int)(mineralFields * 1.5)) {
          n = bases + 1;
        }
        if (bases < n && canExpand && !st.isExpanding) {
          build(Zerg_Hatchery, nextBase);
        }
      }

      if (enemyAirArmySupply || enemyCloakedUnitCount) {
        upgrade(Pneumatized_Carapace);
      }

      buildN(Zerg_Drone, 30);

      if (goHydras) {
        upgrade(Muscular_Augments) && upgrade(Grooved_Spines);
      }

      //      if (enemyStargateCount + enemyCorsairCount + enemyWraithCount) {
      //        int n = std::max(bases, (int)(enemyAirArmySupply / 4.0));
      //        if (countPlusProduction(st, Zerg_Spore_Colony) < n) {
      //          wantsSpores = true;
      //        }
      //        if (countPlusProduction(st, Zerg_Creep_Colony) +
      //        countPlusProduction(st, Zerg_Spore_Colony) < n) {
      //          wantsSpores = true;
      //          build(Zerg_Creep_Colony, nextSporePos);
      //        }
      //      }

      if (droneCount >= 22 && !enemyIsOneBase &&
          (enemyStargateCount + enemyAirArmySupply == 0.0 ||
           droneCount >= 30)) {
        if (droneCount >= 32 &&
            armySupply >= std::min(enemyArmySupply * 0.75, 20.0)) {
          if (droneCount >= 42) {
            upgrade(Adrenal_Glands);
          } else {
            buildN(Zerg_Lair, 1);
          }
        }
        if (droneCount >= 28 && !goHydras) {
          if (enemyRace == +tc::BW::Race::Protoss) {
            if (enemyZealotCount >= enemyDragoonCount) {
              upgrade(Zerg_Carapace_1);
            } else {
              upgrade(Zerg_Melee_Attacks_1);
            }
          } else {
            upgrade(Zerg_Carapace_1);
          }
        }
      }
      if (hatcheries >= 3 &&
          (droneCount >= 20 || enemyArmySupply >= 6.0 || armySupply >= 6.0)) {
        if ((enemyZealotCount >= 6 || enemyForgeIsSpinning) &&
            (enemyStargateCount + enemyAirArmySupply == 0.0 ||
             droneCount >= 30)) {
          upgrade(Metabolic_Boost);
          if (droneCount >= 13 && !goHydras) {
            if (has(st, Zerg_Extractor)) {
              upgrade(Zerg_Carapace_1);
            } else {
              buildN(Zerg_Extractor, 1);
            }
          }
        } else {
          upgrade(Metabolic_Boost);
        }
      }
    }

    if (enemyStargateCount + enemyAirArmySupply) {
      if (countPlusProduction(st, Zerg_Hydralisk) < 4) {
        build(Zerg_Hydralisk);
      } else {
        double AASupply = enemyAirArmySupply - enemyScienceVesselCount * 1.5;
        if (countPlusProduction(st, Zerg_Hydralisk) +
                countPlusProduction(st, Zerg_Scourge) +
                countPlusProduction(st, Zerg_Mutalisk) <
            std::max(AASupply, 4 + AASupply / 2.0)) {
          if (enemyScienceVesselCount * 2 != enemyAirArmySupply) {
            build(Zerg_Hydralisk);
            upgrade(Muscular_Augments) && upgrade(Grooved_Spines);
          }
          if (droneCount >= 29 ||
              (armySupply > enemyArmySupply && droneCount >= 20)) {
            buildN(Zerg_Spire, 1);
          }
        }
      }
    }

    if (enemyIsOneBase &&
        enemyGroundArmySupply <
            armySupply + countPlusProduction(st, Zerg_Sunken_Colony) * 6) {
      buildN(Zerg_Hydralisk_Den, 1);
    }

    if (enemyRace == +tc::BW::Race::Terran &&
        (enemyIsOneBase || enemyAttackingArmySupply > armySupply) &&
        !attacking && droneCount < 28) {
      build(Zerg_Zergling);
    }

    buildN(Zerg_Drone, 16);

    if (countPlusProduction(st, Zerg_Sunken_Colony) >= 2) {
      buildN(Zerg_Hatchery, 3);
    }

    bool wantsSunkens = false;
    if (bases >= 2 && enemyRace == +tc::BW::Race::Protoss &&
        (enemyIsOneBase ||
         (droneCount >= 32 && enemyZealotCount >= 6 &&
          enemyArmySupply > armySupply))) {
      int n = 1;
      if (enemyArmySupply > armySupply + 4.0) {
        ++n;
      }
      if (droneCount >= 25 && enemyIsOneBase) {
        n += 2;
      } else if (droneCount >= 30 && enemyZealotCount >= 12) {
        n += 2;
      }
      if (countPlusProduction(st, Zerg_Sunken_Colony) < n) {
        wantsSunkens = true;
      }
      if (countPlusProduction(st, Zerg_Creep_Colony) +
              countPlusProduction(st, Zerg_Sunken_Colony) <
          n) {
        buildSunkens(st, n);
      }
    }

    if (!enemyHasExpanded && enemyForgeCount + enemyStaticDefenceCount == 0 &&
        st.frame < 24 * 60 * 6) {
      if (bases == 2 && st.frame >= 24 * 60 * 3 &&
          enemyRace == +tc::BW::Race::Protoss) {
        int n = 1 + enemyGroundArmySupply / 5.0;
        if (enemyGasUnits == 0) {
          n = std::max(n, 2);
        } else if (
            droneCount >= 20 &&
            enemyCyberneticsCoreCount + enemyTemplarArchivesCount +
                enemyCloakedUnitCount &&
            armySupply <= 4.0) {
          n = std::max(n, 4);
        }
        buildSunkens(st, n);
        wantsSunkens = true;
      } else {
        buildN(
            Zerg_Zergling, 6 + std::min((int)(enemyGroundArmySupply / 2), 4));
      }
    }

    if (armySupply < buildArmySupply && armySupply < 18.0) {
      build(Zerg_Zergling);
      if (goHydras && hasOrInProduction(st, Muscular_Augments) &&
          zerglingCount >= hydraliskCount) {
        build(Zerg_Hydralisk);
      }
    }

    if (enemyHasExpanded || enemyForgeCount + enemyStaticDefenceCount) {
      if (hatcheries < droneCount / 5 && bases < 4 && canExpand &&
          !st.isExpanding && (attacking || enemyArmySupply < 8.0)) {
        build(Zerg_Hatchery, nextBase);
      }
    } else {
      if (hatcheries == 1) {
        build(Zerg_Hatchery, nextBase);
      }
    }

    if (st.frame < 24 * 60 * 5) {
      if (enemyProxyGatewayCount + enemyProxyBarracksCount +
              enemyProxyForgeCount + enemyProxyCannonCount ||
          (st.frame < 24 * 60 * 3 + 24 * 30 &&
           enemyAttackingArmySupply >= 4.0)) {
        buildN(Zerg_Sunken_Colony, 1);
        buildN(Zerg_Zergling, 6);
      } else {
        if (hatcheries >= 3 && enemyAttackingArmySupply < 4.0) {
          if (hatcheries < 4) {
            if (enemyIsOneBase) {
              buildN(Zerg_Hydralisk_Den, 1);
              buildN(Zerg_Drone, 15);
              buildN(Zerg_Extractor, 1);
              buildN(Zerg_Drone, 14);
              buildN(Zerg_Hatchery, 3);
            } else {
              build(Zerg_Hatchery, nextBase);
            }
          }
          buildN(Zerg_Drone, 14);
          if (enemyBuildingCount < 3 && (enemyIsOneBase || enemyIsRushing)) {
            if (enemyArmySupply < 4) {
              buildN(Zerg_Zergling, 4);
            } else {
              buildN(
                  Zerg_Zergling,
                  std::min(
                      9 - enemyZealotCount +
                          enemyZealotCount * enemyZealotCount,
                      14));
            }
          }
          if (enemyArmySupply >= 4.0 &&
              countPlusProduction(st, Zerg_Sunken_Colony) < 2) {
            buildN(
                Zerg_Zergling,
                enemyGasUnits == 0 && droneCount >= 12 &&
                        (enemyGatewayCount + enemyBarracksCount >= 2 ||
                         enemyArmySupply >= 6.0)
                    ? 8
                    : 4);
          }
        }
      }
    }

    if (hatcheries >= 2 && !enemyHasExpanded &&
        enemyForgeCount + enemyStaticDefenceCount == 0) {
      buildN(Zerg_Spawning_Pool, 1);
    }

    if (hatcheries < 2) {
      buildN(Zerg_Drone, 12);
    }

    buildN(Zerg_Drone, 9);

    if (countPlusProduction(st, Zerg_Creep_Colony)) {
      if (wantsSpores) {
        build(Zerg_Spore_Colony);
      }
      if (wantsSunkens || st.frame < 24 * 60 * 5) {
        build(Zerg_Sunken_Colony);
      }
    }
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOmidmassling, UpcId, State*, Module*);
} // namespace cherrypi
