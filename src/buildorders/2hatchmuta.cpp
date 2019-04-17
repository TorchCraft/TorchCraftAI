/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "base.h"

namespace cherrypi {

class ABBO2HatchMuta : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  bool saveLarva = false;
  Position nextSunkenPos;

  virtual void preBuild2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    bool attack = false;
    if (st.frame < 24 * 60 * 9) {
      if (armySupply > enemyAttackingArmySupply) {
        attack = true;
      }
    } else {
      if (enemyAttackingArmySupply >= enemyArmySupply * 0.5) {
        attack = true;
      }
      if (armySupply >= 40.0) {
        attack = true;
      }
    }
    if (st.frame < 24 * 60 * 9 && enemyVultureCount &&
        countUnits(st, Zerg_Mutalisk) == 0) {
      attack = false;
    }
    if (weArePlanningExpansion) {
      attack = true;
    }
    postBlackboardKey("TacticsAttack", attack);

    saveLarva = false;
    if (state_->unitsInfo().myCompletedUnitsOfType(Zerg_Spire).empty()) {
      for (Unit* u : state_->unitsInfo().myUnitsOfType(Zerg_Spire)) {
        if (u->remainingBuildTrainTime < 900) {
          saveLarva = true;
        }
      }
    }

    nextSunkenPos = findSunkenPos(Zerg_Sunken_Colony, true, true);
  }

  virtual void buildStep2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    if (st.frame < 24 * 60 * 8) {
      st.autoBuildRefineries = false;
      autoExpand = false;
    } else {
      autoExpand = true;
    }

    if (hasOrInProduction(st, Zerg_Creep_Colony)) {
      build(Zerg_Sunken_Colony);
      return;
    }

    if (saveLarva) {
      buildN(Zerg_Hatchery, 3);
      buildN(Zerg_Overlord, 5);
      build(Zerg_Mutalisk);
      return;
    }

    if (st.frame < 24 * 60 * 8) {
      build(Zerg_Zergling);
      if (enemyArmySupply + enemyVultureCount < 12.0) {
        buildN(Zerg_Drone, 26);
      } else {
        buildN(Zerg_Drone, 19);
      }
      build(Zerg_Mutalisk);
    } else {
      if (armySupply < enemyArmySupply * 0.75) {
        build(Zerg_Zergling);
      } else if (st.minerals >= 500) {
        buildN(Zerg_Drone, 50);
      }
      build(Zerg_Mutalisk);

      if (armySupply > enemyArmySupply * 0.66) {
        if (countProduction(st, Zerg_Drone) <
            (armySupply > enemyArmySupply ? 2 : 1)) {
          buildN(Zerg_Drone, 66);
        }
      }
      if (countPlusProduction(st, Zerg_Mutalisk) >= 6 || armySupply >= 20.0) {
        if (armySupply > enemyArmySupply) {
          buildN(Zerg_Drone, 32);
        }
      }

      if (armySupply >= 16.0 && armySupply >= enemyAttackingArmySupply + 8.0 &&
          st.workers >= 24) {
        if (bases < 4 && canExpand && !st.isExpanding) {
          build(Zerg_Hatchery, nextBase);
        }
      }
    }
    if (countPlusProduction(st, Zerg_Drone) >= 24 &&
        countPlusProduction(st, Zerg_Mutalisk) >= 6) {
      if (enemyAirArmySupply || enemyCloakedUnitCount) {
        upgrade(Pneumatized_Carapace);
      }
    }
    if (hasOrInProduction(st, Zerg_Spire)) {
      buildN(Zerg_Extractor, 2);
      upgrade(Zerg_Melee_Attacks_1);
    }
    if (!has(st, Zerg_Spire)) {
      buildN(Zerg_Spire, 1);
      buildN(Zerg_Drone, 20);
      upgrade(Metabolic_Boost);
      buildN(Zerg_Lair, 1);
      buildN(Zerg_Drone, 12);
      buildN(Zerg_Zergling, 2);
    }

    int sunkens = 0;
    if (!enemyHasExpanded && countPlusProduction(st, Zerg_Drone) >= 18) {
      buildN(Zerg_Zergling, 6);
      sunkens = 2;
    }
    if (enemyVultureCount >= 2) {
      ++sunkens;
    }
    buildSunkens(st, sunkens);

    if (enemyArmySupplyInOurBase) {
      buildN(Zerg_Zergling, 4);
    }

    if (myCompletedHatchCount >= 2 &&
        (st.workers >= 12 || enemyArmySupplyInOurBase) &&
        !has(st, Zerg_Spire)) {
      buildSunkens(st, 1);
    }
    buildN(Zerg_Extractor, 1);
    buildN(Zerg_Spawning_Pool, 1);
    if (countPlusProduction(st, Zerg_Hatchery) == 1) {
      build(Zerg_Hatchery, nextBase);
      buildN(Zerg_Drone, 12);
    }
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBO2HatchMuta, UpcId, State*, Module*);
} // namespace cherrypi
