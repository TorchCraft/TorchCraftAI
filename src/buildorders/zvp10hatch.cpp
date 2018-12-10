/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "base.h"

namespace cherrypi {

class ABBOzvp10hatch : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  bool buildExtractor = false;
  bool hasBuiltExtractor = false;
  int hurtSunkens = 0;
  bool hasSunken = false;
  bool wasAllinRushed = false;

  virtual void preBuild2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    postBlackboardKey(Blackboard::kMinScoutFrameKey, 0);

    bool attack = false;
    if (st.frame >= 15 * 60 * 14) {
      attack = armySupply > enemyArmySupply + 8.0 - enemyAttackingArmySupply;
      if (bases > 3 && armySupply < enemyArmySupply + 16.0) {
        attack = false;
      }
      if (st.frame >= 15 * 60 * 30) {
        attack = true;
      }
    }
    if (st.frame < 24 * 60 * 8 && enemyArmySupply < 12.0 && !wasAllinRushed) {
      attack = true;
    }
    if ((wasAllinRushed && armySupply > enemyArmySupply) ||
        weArePlanningExpansion) {
      attack = true;
    }
    if (enemyStaticDefenceCount || enemyHasExpanded ||
        state_->unitsInfo().myUnitsOfType(Zerg_Hydralisk).empty()) {
      if (enemyStaticDefenceCount >= 8) {
        attack = st.frame >= 15 * 60 * 22;
      }
      if (enemyArmySupply < 8.0 && enemyStaticDefenceCount < 4) {
        attack = true;
      }
    }
    postBlackboardKey("TacticsAttack", attack);

    if (!hasBuiltExtractor && countPlusProduction(st, Zerg_Drone) == 9 &&
        countPlusProduction(st, Zerg_Overlord) == 1) {
      buildExtractor = true;
      hasBuiltExtractor = cancelGas();
    } else {
      buildExtractor = false;
    }

    if (!wasAllinRushed && state_->unitsInfo().myWorkers().size() < 22) {
      double totalEnemyArmySupply = 0.0;
      for (Unit* u : state_->unitsInfo().allUnitsEver()) {
        if (u->isEnemy) {
          totalEnemyArmySupply += u->type->supplyRequired;
        }
      }
      if (totalEnemyArmySupply >= 16.0) {
        wasAllinRushed = true;
      }
    }

    hurtSunkens = 0;
    for (Unit* u :
         state_->unitsInfo().myCompletedUnitsOfType(Zerg_Sunken_Colony)) {
      if (u->unit.health < u->type->maxHp / 2) {
        ++hurtSunkens;
      }
    }

    if (!hasSunken) {
      hasSunken =
          !state_->unitsInfo().myUnitsOfType(Zerg_Sunken_Colony).empty();
    }
  }

  virtual void buildStep2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    st.autoBuildRefineries = countPlusProduction(st, Zerg_Extractor) == 0 ||
        st.frame >= 15 * 60 * 15;

    if (st.frame < 15 * 60 * 4 + 15 * 50) {
      if (myCompletedHatchCount >= 2 && nextStaticDefencePos != Position()) {
        if (!hasSunken) {
          buildSunkens(st, 2);
          return;
        }
      }
    }

    if (st.usedSupply[tc::BW::Race::Zerg] < 185.0 ||
        countPlusProduction(st, Zerg_Mutalisk) >= 20) {
      build(Zerg_Zergling);
      int hydraliskCount = countPlusProduction(st, Zerg_Hydralisk);
      int zerglingCount = countPlusProduction(st, Zerg_Zergling);
      if (zerglingCount >=
          std::min(hydraliskCount * 2, enemyDragoonCount * 3)) {
        build(Zerg_Hydralisk);
      }
      if (groundArmySupply >= 25.0 && st.workers >= 44) {
        buildN(Zerg_Mutalisk, 6);
      }
      if (has(st, Zerg_Spire)) {
        if (enemyReaverCount) {
          if (countPlusProduction(st, Zerg_Mutalisk) < enemyArmySupply -
                  enemyAntiAirArmySupply * 1.5 + enemyReaverCount) {
            build(Zerg_Mutalisk);
          }
        }
        if (countPlusProduction(st, Zerg_Scourge) <
            std::min(enemyAirArmySupply, 4.0)) {
          build(Zerg_Scourge);
        }
      }
    } else {
      build(Zerg_Mutalisk);
    }

    if (countPlusProduction(st, Zerg_Hydralisk) >= 40.0 &&
        (armySupply > enemyArmySupply || armySupply >= 80.0)) {
      buildN(Zerg_Mutalisk, 6);
      buildN(Zerg_Scourge, std::min((int)enemyAirArmySupply, 10));
    }

    if (countPlusProduction(st, Zerg_Zergling) >= 10) {
      upgrade(Metabolic_Boost);
    }

    if (armySupply > enemyArmySupply) {
      if (countProduction(st, Zerg_Drone) == 0) {
        buildN(Zerg_Drone, 66);
      }
      if (armySupply > enemyArmySupply + enemyAttackingArmySupply &&
          countProduction(st, Zerg_Drone) < 3) {
        buildN(Zerg_Drone, 45);
      }
    }

    if (st.workers >= 40) {
      upgrade(Pneumatized_Carapace);
    }

    if (st.workers >= 30 &&
        ((armySupply > enemyArmySupply && !wasAllinRushed) ||
         st.workers >= 42)) {
      buildN(Zerg_Lair, 1) && buildN(Zerg_Spire, 1);
    }

    if (armySupply > enemyArmySupply + 8.0 || armySupply >= 20.0) {
      if (st.workers >= 40) {
        upgrade(Zerg_Carapace_1) && upgrade(Zerg_Carapace_2) &&
            upgrade(Zerg_Carapace_3);
      }
      upgrade(Zerg_Missile_Attacks_1) && upgrade(Zerg_Missile_Attacks_2) &&
          upgrade(Zerg_Missile_Attacks_3);
    }

    if (bases < (armySupply >= 20.0 && armySupply > enemyArmySupply + 8.0
                     ? 4
                     : 3) &&
        !st.isExpanding && canExpand &&
        armySupply >= std::min(enemyArmySupply, 12.0)) {
      build(Zerg_Hatchery, nextBase);
    }
    if (armySupply > enemyArmySupply) {
      buildN(Zerg_Drone, 24 + std::max(enemyStaticDefenceCount - 3, 0) * 4);
    } else {
      buildN(Zerg_Drone, 24 + enemyStaticDefenceCount * 4);
    }

    upgrade(Muscular_Augments) && upgrade(Grooved_Spines);
    if (enemyStaticDefenceCount == 0 && !enemyHasExpanded) {
      if (!hasUpgrade(st, Grooved_Spines) ||
          !hasUpgrade(st, Muscular_Augments)) {
        buildN(Zerg_Hydralisk, 6);
        buildN(Zerg_Zergling, countPlusProduction(st, Zerg_Hydralisk) * 2);
      } else {
        buildN(Zerg_Hydralisk, 9);
      }
      if (st.frame < 24 * 60 * 12) {
        buildN(Zerg_Drone, 27);
        if (bases == 2 && enemyFactoryCount == 0) {
          buildSunkens(st, 5);
        }
        buildN(Zerg_Drone, 20);
        buildSunkens(st, 3);
      }
    }
    if (st.workers >= 24) {
      if (enemyDragoonCount) {
        upgrade(Grooved_Spines) && upgrade(Muscular_Augments);
      }
      if (enemyCloakedUnitCount) {
        upgrade(Pneumatized_Carapace);
      }
    }
    if (st.frame < 24 * 60 * 7) {
      buildN(Zerg_Hydralisk, 2);
      if (enemyRace == +tc::BW::Race::Terran) {
        if ((enemyStaticDefenceCount || enemyHasExpanded) &&
            armySupply > enemyArmySupply) {
          buildN(Zerg_Drone, 32);
        }
      }
      buildN(Zerg_Drone, 18 + enemyStaticDefenceCount * 2);

      buildN(Zerg_Hydralisk_Den, 1);
      buildN(Zerg_Drone, 16);

      if (enemyStaticDefenceCount || enemyHasExpanded) {
        if (armySupply > enemyArmySupply) {
          buildN(Zerg_Drone, 20);
        }
        if (countPlusProduction(st, Zerg_Hatchery) < 3) {
          build(Zerg_Hatchery, nextBase);
        }
      } else {
        buildN(Zerg_Hatchery, 3);
      }
      buildN(Zerg_Drone, 14);
      if (st.frame < 15 * 60 * 11) {
        if (enemyZealotCount / 2.0 - armySupply / 2.0 >
            countPlusProduction(st, Zerg_Sunken_Colony) - 1) {
          buildSunkens(st, 4);
        }
        if (enemyArmySupplyInOurBase > armySupply) {
          buildN(Zerg_Zergling, 16);
        }
      }
      if (countPlusProduction(st, Zerg_Hydralisk) == 0) {
        buildN(Zerg_Zergling, 4);
      }
      buildSunkens(st, (enemyZealotCount ? 2 : 1) + hurtSunkens);
      buildN(Zerg_Overlord, 2);
      buildN(Zerg_Spawning_Pool, 1);
      if (countPlusProduction(st, Zerg_Hatchery) == 1) {
        build(Zerg_Hatchery, nextBase);
        if (!hasBuiltExtractor && buildExtractor) {
          buildN(Zerg_Extractor, 1);
        }
        buildN(Zerg_Drone, 9);
      }
    }
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOzvp10hatch, UpcId, State*, Module*);
}
