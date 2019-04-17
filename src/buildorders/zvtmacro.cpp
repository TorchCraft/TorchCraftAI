/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "base.h"

namespace cherrypi {

class ABBOzvtMacro : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  bool hasSunken = false;
  bool waitForPool = false;
  bool enemyHasMadeVultures = false;
  bool poolDone = false;
  bool enemyIsMech = false;
  bool enemyOpenedBio = false;
  double enemyMineralsLost = 0.0;

  virtual void preBuild2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    hasSunken = !state_->unitsInfo().myUnitsOfType(Zerg_Sunken_Colony).empty();

    bool attack = true;
    if (st.frame < 24 * 60 * 9 && enemyRace != +tc::BW::Race::Protoss &&
        (enemyHasMadeVultures ||
         (bases < 4 && enemyArmySupply >= std::max(8.0, armySupply))) &&
        !weArePlanningExpansion) {
      attack = false;
    }
    postBlackboardKey("TacticsAttack", attack);
    poolDone =
        !state_->unitsInfo().myCompletedUnitsOfType(Zerg_Spawning_Pool).empty();
    if (!enemyHasMadeVultures) {
      enemyHasMadeVultures = enemyVultureCount != 0;
    }

    enemyMineralsLost = 0;
    for (Unit* u : state_->unitsInfo().allUnitsEver()) {
      if (u->isEnemy && u->dead) {
        enemyMineralsLost += u->type->mineralCost;
      }
    }

    if (st.frame < 24 * 60 * 9) {
      if (enemyBiologicalArmySupply >= 8.0) {
        enemyOpenedBio = true;
      }
    }
    enemyIsMech = (!enemyOpenedBio || st.frame >= 24 * 60 * 9 ||
                   enemyArmySupply - enemyBiologicalArmySupply >= 12.0) &&
        enemyVultureCount + enemyGoliathCount + enemyTankCount >
            (enemyBiologicalArmySupply / 2 - 4);
  }

  virtual void buildStep2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    int droneCount = countPlusProduction(st, Zerg_Drone);
    int zerglingCount = countPlusProduction(st, Zerg_Zergling);
    int hydraliskCount = countPlusProduction(st, Zerg_Hydralisk);
    int mutaliskCount = countPlusProduction(st, Zerg_Mutalisk);
    int scourgeCount = countPlusProduction(st, Zerg_Scourge);

    auto sunkens = [&]() {
      if (hasSunken &&
          armySupply + countPlusProduction(st, Zerg_Sunken_Colony) * 3 >=
              std::max(enemyArmySupply, 8.0) &&
          st.frame < 24 * 60 * 4 + 24 * 30) {
        return;
      }
      if (hasSunken && enemyArmySupply == 0.0 && enemyBarracksCount == 1) {
        return;
      }
      if (enemyRace == +tc::BW::Race::Protoss &&
          enemyForgeCount + enemyStaticDefenceCount) {
        return;
      }
      int n = 0;
      if ((droneCount < 40 || armySupply < 30.0) && !enemyHasExpanded &&
          droneCount >= 12) {
        if (enemyVultureCount) {
          if (!has(st, Zerg_Hydralisk_Den)) {
            buildN(Zerg_Zergling, 6);
          }
          n = 1;
        }
        if (st.frame >= 24 * 60 * 4 + 24 * 30 &&
            enemyBiologicalArmySupply >= 4.0) {
          n = 3;
        }
        if (st.frame >= 24 * 60 * 5 + 24 * 30 &&
            enemyBiologicalArmySupply >= 8.0) {
          n = 4;
        }
      }
      if (enemyRace == +tc::BW::Race::Terran && !enemyHasExpanded &&
          enemyFactoryCount == 0 && st.frame >= 24 * 60 * 4 &&
          (enemyOpenedBio || enemyBarracksCount >= 2 || enemySupplyInOurBase)) {
        if (enemyMineralsLost < 200 && st.frame >= 24 * 60 * 6 &&
            enemyAttackingArmySupply) {
          n = droneCount >= 30 ? 6 : 5;
        } else if (st.frame >= 24 * 60 * 5) {
          n = droneCount >= 26 ? 4 : 3;
        } else {
          n = 3;
        }
      }
      if (enemyTankCount + enemyVultureCount && n > 1) {
        n = 1;
      }
      n = std::min(n, 1 + std::max((droneCount - 12) / 2, 0));
      buildSunkens(st, n);
    };

    if (currentFrame < 24 * 60 * 5) {
      bool poolFirst = false;
      if (enemyRace == +tc::BW::Race::Terran) {
        poolFirst = !enemyHasExpanded;
      } else {
        poolFirst =
            enemyBuildingCount == 0 || enemyGatewayCount || enemyArmySupply;
      }

      if (poolFirst) {
        if (poolDone) {
          if (hasSunken || currentFrame >= 24 * 60 * 3 + 24 * 30) {
            buildN(Zerg_Zergling, 12);
            buildN(Zerg_Drone, 24);
            if (enemyBiologicalArmySupply == enemyArmySupply) {
              buildN(
                  Zerg_Zergling,
                  std::max(6, 3 + (int)enemyBiologicalArmySupply));
            } else {
              if (has(st, Zerg_Hydralisk_Den)) {
                buildN(Zerg_Zergling, 4);
                buildN(Zerg_Hydralisk, 2);
              } else {
                buildN(Zerg_Hydralisk, 6);
              }
            }
            buildN(Zerg_Drone, 20);
            if (enemyRace == +tc::BW::Race::Terran) {
              buildN(Zerg_Hydralisk_Den, 1) && upgrade(Metabolic_Boost);
            } else {
              upgrade(Metabolic_Boost);
            }
            buildN(Zerg_Drone, 16);
            buildN(Zerg_Zergling, 2);
            buildN(Zerg_Extractor, 1);
            buildN(Zerg_Drone, 14);
          } else {
            buildN(Zerg_Drone, 12);
          }
          if (enemyForgeCount + enemyStaticDefenceCount == 0) {
            buildSunkens(st, 1);
          }
          if (armySupply < enemyArmySupplyInOurBase) {
            build(Zerg_Zergling);
            if (enemyVultureCount && has(st, Zerg_Hydralisk_Den)) {
              build(Zerg_Hydralisk);
            }
          }
        }
        if (countPlusProduction(st, Zerg_Hatchery) == 2) {
          build(Zerg_Hatchery);
          buildN(Zerg_Drone, 12);
        }
        if ((enemyGatewayCount >= 2 || enemyZealotCount) &&
            enemyForgeCount + enemyStaticDefenceCount == 0 &&
            !enemyHasExpanded) {
          buildN(Zerg_Zergling, 12);
        }
        buildN(Zerg_Spawning_Pool, 1);
      } else {
        build(Zerg_Drone);
        buildN(Zerg_Hydralisk_Den, 1);
        upgrade(Metabolic_Boost);
        buildN(Zerg_Extractor, 1);
        buildN(Zerg_Drone, 20);

        buildN(Zerg_Zergling, 2);
        buildN(Zerg_Spawning_Pool, 1);
        buildN(Zerg_Hatchery, 4);
        buildN(Zerg_Drone, 16);
        if (countPlusProduction(st, Zerg_Hatchery) == 2) {
          build(Zerg_Hatchery, nextBase);
          buildN(Zerg_Drone, 14);
        }
      }
      if (countPlusProduction(st, Zerg_Hatchery) == 1) {
        build(Zerg_Hatchery, nextBase);
        buildN(Zerg_Drone, 12);
      }

      if (myCompletedHatchCount >= 2) {
        sunkens();
      }

      if (countPlusProduction(st, Zerg_Hatchery) >= 3 && st.workers >= 14 &&
          st.workers < 18 && !enemyHasExpanded) {
        if (enemyForgeCount + enemyStaticDefenceCount == 0) {
          buildSunkens(st, 1);
        }
      }
      return;
    }

    //    if (waitForPool) {
    //      if (countPlusProduction(st, Zerg_Hatchery) >= 3 && st.workers >= 14
    //      && st.workers < 18 && !enemyHasExpanded) {
    //        buildSunkens(st, 1);
    //      }
    //      return;
    //    }

    //    if (enemyIsRushing && armySupply < enemyAttackingArmySupply + 1.0 &&
    //        (enemyAttackingArmySupply ||
    //         (!enemyHasExpanded && enemyStaticDefenceCount == 0))) {
    //      if (myCompletedHatchCount >= 2 && nextStaticDefencePos !=
    //      Position()) {
    //        if (!hasSunken) {
    //          buildSunkens(st, 2);
    //        }
    //      }
    //      // this flag helps prioritize the hatchery
    //      auto buildingDefenses = false;
    //      if (countPlusProduction(st, Zerg_Zergling) <
    //          std::max(enemyArmySupply * 3, 8.)) {
    //        build(Zerg_Zergling);
    //        buildingDefenses = true;
    //      }
    //      if (countPlusProduction(st, Zerg_Hatchery) == 1) {
    //        build(Zerg_Hatchery, nextBase);
    //      }
    //      if (buildingDefenses) {
    //        return;
    //      }
    //    }

    if (bases < 6 && armySupply > enemyAttackingArmySupply &&
        st.minerals < 600 && !st.isExpanding) {
      if (countPlusProduction(st, Zerg_Hatchery) < 12) {
        build(Zerg_Hatchery, nextBase);
      }
    }

    st.autoBuildRefineries = st.workers >= 34 ||
        countUnits(st, Zerg_Extractor) >= 2 ||
        countPlusProduction(st, Zerg_Hydralisk) >= 6;
    build(Zerg_Zergling);

    if (countProduction(st, Zerg_Drone) <
        (armySupply > enemyArmySupply ? 3 : 1)) {
      buildN(Zerg_Drone, 70);
    }

    // buildN(Zerg_Scourge, (int)enemyAirArmySupply - hydraliskCount -
    // mutaliskCount - enemyWraithCount - enemyScienceVesselCount * 2);

    if (st.frame < 24 * 60 * 9 ||
        enemyAttackingArmySupply >= armySupply * 0.5) {
      buildN(Zerg_Zergling, 60 - hydraliskCount * 2 - mutaliskCount * 2);
      buildN(Zerg_Mutalisk, zerglingCount / 4);
      if (enemySmallArmySupply < enemyArmySupply * 0.33) {
        buildN(Zerg_Hydralisk, 12);
        if (hydraliskCount >= 4) {
          upgrade(Grooved_Spines) && upgrade(Muscular_Augments);
        }
      }
    }
    if (enemyGoliathCount + enemyVultureCount >= 4) {
      if (hydraliskCount < 20 || hydraliskCount < zerglingCount / 2) {
        buildN(Zerg_Hydralisk, enemyGoliathCount + enemyVultureCount);
        upgrade(Grooved_Spines) && upgrade(Muscular_Augments);
      }
    }
    if (enemyAntiAirArmySupply <= enemyArmySupply * 0.5) {
      buildN(Zerg_Mutalisk, 9);
    }
    if (droneCount >= 48) {
      if (mutaliskCount < (enemyArmySupply - enemyAntiAirArmySupply) / 2) {
        if (mutaliskCount < armySupply / 4) {
          build(Zerg_Mutalisk);
        }
      }
      //      upgrade(Zerg_Missile_Attacks_1) &&
      //          upgrade(Zerg_Missile_Attacks_2) &&
      //          upgrade(Zerg_Missile_Attacks_3);
      //      buildN(Zerg_Evolution_Chamber, 3);
      //      upgrade(Zerg_Flyer_Carapace_1) &&
      //          upgrade(Zerg_Flyer_Attacks_1) &&
      //          upgrade(Zerg_Flyer_Carapace_2) &&
      //          upgrade(Zerg_Flyer_Attacks_2) &&
      //          upgrade(Zerg_Flyer_Carapace_3) &&
      //          upgrade(Zerg_Flyer_Attacks_3);
    }
    if (armySupply >= std::max(enemyArmySupply, 60.0) ||
        (st.workers >= 70 && armySupply >= 50.0)) {
      if (countPlusProduction(st, Zerg_Ultralisk) * 4 <
          (enemyGroundArmySupply - enemyTankCount * 2) +
              enemyBiologicalArmySupply - enemyScienceVesselCount * 3) {
        build(Zerg_Ultralisk);
        upgrade(Chitinous_Plating) && upgrade(Anabolic_Synthesis);
      }
    }
    if (st.workers >= 66) {
      buildN(Zerg_Evolution_Chamber, 3);
    }
    buildN(Zerg_Drone, enemyRace == +tc::BW::Race::Terran ? 42 : 40);
    bool useLurkers = enemyBiologicalArmySupply >= enemyArmySupply * 0.4;
    useLurkers = false;
    if (st.workers >= 34 &&
        (armySupply < enemyArmySupply || countProduction(st, Zerg_Drone)) &&
        countPlusProduction(st, Zerg_Ultralisk) == 0) {
      if (useLurkers) {
        if (enemyBiologicalArmySupply >= enemyArmySupply * 0.66) {
          build(Zerg_Lurker);
        } else {
          buildN(Zerg_Lurker, (int)(enemyBiologicalArmySupply / 4));
        }
      }
    }
    if ((st.frame < 24 * 60 * 9 || enemyAttackingArmySupply > armySupply) &&
        (armySupply < enemyArmySupply + 4.0 || mutaliskCount < 5)) {
      if (st.workers >= 30) {
        buildSunkens(st, 1);
      }
      if (st.workers >= 29) {
        upgrade(Pneumatized_Carapace);
        if (useLurkers) {
          upgrade(Lurker_Aspect);
        }
        buildN(Zerg_Spire, 1);
        buildN(Zerg_Lair, 1);
        if (hydraliskCount > enemyVultureCount * 2) {
          buildN(Zerg_Zergling, 8);
        } else {
          buildN(Zerg_Hydralisk, 4);
        }
      }
      if (enemyAttackingArmySupply >= 6.0 &&
          (enemyVultureCount || droneCount >= 27)) {
        buildN(Zerg_Zergling, 14 - mutaliskCount * 2);
        if (useLurkers) {
          buildN(Zerg_Lurker, std::max(enemyVultureCount, 6));
        } else {
          buildN(Zerg_Mutalisk, std::max(enemyVultureCount, 6));
        }
      }
    }

    if (countUnits(st, Zerg_Drone) > 30 && countUnits(st, Zerg_Zergling) > 16) {
      upgrade(Zerg_Carapace_1) && upgrade(Zerg_Melee_Attacks_1) &&
          upgrade(Zerg_Carapace_2) && upgrade(Zerg_Melee_Attacks_2) &&
          upgrade(Zerg_Carapace_3) && upgrade(Zerg_Melee_Attacks_3);
    }

    // if (countPlusProduction(st, Zerg_Hatchery) == 3 && !st.isExpanding) {
    if (armySupply >= enemyArmySupply - 2.0 && bases < 4 && canExpand &&
        !st.isExpanding) {
      build(Zerg_Hatchery, nextBase);
    }

    buildN(Zerg_Spire, 1);

    if (enemyIsMech && !hasUpgrade(st, Muscular_Augments)) {
      upgrade(Muscular_Augments) && upgrade(Grooved_Spines);
    } else {
      upgrade(Zerg_Melee_Attacks_1) && upgrade(Zerg_Carapace_1);
      if (st.workers >= 55) {
        upgrade(Zerg_Carapace_3);
        upgrade(Zerg_Melee_Attacks_3);
        upgrade(Zerg_Carapace_2);
        upgrade(Zerg_Melee_Attacks_2);
      }
    }
    buildN(Zerg_Lair, 1);
    buildN(Zerg_Drone, 26);

    if ((st.workers < 40 || armySupply < 20.0) &&
        armySupply < enemyAttackingArmySupply &&
        (st.workers >= 28 ||
         enemyArmySupply >
             armySupply + countPlusProduction(st, Zerg_Sunken_Colony) * 3)) {
      if (enemyArmySupply - enemyVultureCount >= enemyArmySupply - 4.0) {
        build(Zerg_Zergling);
        if (enemyVultureCount >= 2) {
          if (has(st, Zerg_Spire) &&
              enemyAntiAirArmySupply <= enemyArmySupply * 0.5) {
            buildN(Zerg_Mutalisk, std::max(enemyVultureCount, 6));
          } else {
            buildN(Zerg_Hydralisk, enemyVultureCount * 3);
          }
        } else if (enemyShuttleCount + enemyReaverCount) {
          if (has(st, Zerg_Spire)) {
            buildN(Zerg_Mutalisk, (enemyShuttleCount + enemyReaverCount) * 4);
          } else if (
              hasOrInProduction(st, Muscular_Augments) || zerglingCount >= 18 ||
              droneCount >= 20) {
            buildN(Zerg_Hydralisk, (enemyShuttleCount + enemyReaverCount) * 4);
            upgrade(Grooved_Spines) && upgrade(Muscular_Augments);
          }
        }
      }
    }
    if (enemyVultureCount + enemyAirArmySupply) {
      if (!has(st, Zerg_Spire)) {
        buildN(
            Zerg_Hydralisk,
            enemyVultureCount + 2 + (int)enemyAirArmySupply - enemyWraithCount);
      } else {
        if (hydraliskCount + scourgeCount < enemyAirArmySupply -
                enemyWraithCount - enemyScienceVesselCount * 2 -
                enemyArbiterCount * 2) {
          build(Zerg_Hydralisk);
          if (hydraliskCount > mutaliskCount * 2 && mutaliskCount < 4) {
            build(Zerg_Mutalisk);
          }
        } else {
          buildN(Zerg_Mutalisk, std::min(enemyVultureCount * 2, 6));
        }
      }
    }
    if (enemyGroundArmySupply > enemyAirArmySupply) {
      buildN(Zerg_Zergling, hydraliskCount + mutaliskCount);
    }
    if (enemyAttackingArmySupply && enemyHasMadeVultures && enemyIsMech &&
        st.workers >= 20 && armySupply > enemyArmySupplyInOurBase) {
      buildN(Zerg_Spire, 1);
    }

    if (enemyZealotCount && droneCount >= 25) {
      upgrade(Zerg_Melee_Attacks_1) && upgrade(Zerg_Carapace_1);
    }
    if (enemyRace == +tc::BW::Race::Protoss &&
        countPlusProduction(st, Zerg_Sunken_Colony) >= 2 && droneCount >= 18 &&
        !enemyHasExpanded && enemyForgeCount + enemyStaticDefenceCount == 0 &&
        st.frame < 24 * 60 * 12) {
      buildN(Zerg_Drone, 27);
      buildSunkens(st, 5);
      buildN(Zerg_Drone, 18);
    }

    if (st.workers >= 52 || has(st, Zerg_Hive)) {
      upgrade(Adrenal_Glands);
    }
    if (droneCount >= 46) {
      buildN(Zerg_Hive, 1);
    }
    buildN(Zerg_Hydralisk_Den, 1);
    buildN(Zerg_Extractor, 1);
    buildN(Zerg_Drone, 20);

    if (droneCount >= 20 || st.gas >= 100) {
      upgrade(Metabolic_Boost);
    }

    if (st.frame < 24 * 60 * 9) {
      sunkens();
    }
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOzvtMacro, UpcId, State*, Module*);
} // namespace cherrypi
