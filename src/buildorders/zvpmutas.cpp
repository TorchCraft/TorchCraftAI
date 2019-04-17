/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "base.h"

namespace cherrypi {

class ABBOzvpmutas : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  bool buildExtractor = false;
  bool hasBuiltExtractor = false;
  int hurtSunkens = 0;
  bool hasSunken = false;

  virtual void preBuild2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    bool attack = true;
    postBlackboardKey("TacticsAttack", attack);

    if (!hasBuiltExtractor && countPlusProduction(st, Zerg_Drone) == 9 &&
        countPlusProduction(st, Zerg_Overlord) == 1) {
      buildExtractor = true;
      hasBuiltExtractor = cancelGas();
    } else {
      buildExtractor = false;
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
        st.frame >= 15 * 60 * 11;

    auto buildSunkens = [&](int n) {
      if (hasOrInProduction(st, Zerg_Creep_Colony)) {
        build(Zerg_Sunken_Colony);
      } else {
        if (myCompletedHatchCount >= 2 && nextStaticDefencePos != Position()) {
          if (countPlusProduction(st, Zerg_Sunken_Colony) < n &&
              !isInProduction(st, Zerg_Creep_Colony)) {
            build(Zerg_Creep_Colony, nextStaticDefencePos);
          }
        }
      }
    };

    if (hasOrInProduction(st, Zerg_Creep_Colony)) {
      build(Zerg_Sunken_Colony);
      return;
    }

    if (st.frame < 15 * 60 * 4 + 15 * 50) {
      if (myCompletedHatchCount >= 2 && nextStaticDefencePos != Position()) {
        if (!hasSunken) {
          buildSunkens(2);
          return;
        }
      }
    }

    build(Zerg_Zergling);
    build(Zerg_Mutalisk);

    if (enemyAntiAirArmySupply >= enemyArmySupply * 0.33) {
      if (armySupply >= 20.0) {
        upgrade(Grooved_Spines) && upgrade(Muscular_Augments);
      }
      if (has(st, Grooved_Spines)) {
        buildN(
            Zerg_Hydralisk,
            (int)(std::min(enemyLargeArmySupply, enemyAntiAirArmySupply) / 2.0));
      }
    }

    if (st.workers >= 40) {
      upgrade(Pneumatized_Carapace);
    }

    if (armySupply >= 20.0) {
      if (countProduction(st, Zerg_Drone) <
          (st.workers < 36 && armySupply > enemyArmySupply ? 2 : 1)) {
        build(Zerg_Drone);
      }
    }

    upgrade(Metabolic_Boost);

    if (countPlusProduction(st, Zerg_Mutalisk) >= 6) {
      if (bases < 3 && !st.isExpanding && canExpand &&
          armySupply >= std::min(enemyArmySupply, 12.0)) {
        build(Zerg_Hatchery, nextBase);
      }
    }

    if (armySupply > enemyArmySupply / 2 + enemyAttackingArmySupply * 2) {
      buildN(Zerg_Drone, 50);
      if (bases < 3 && !st.isExpanding && canExpand &&
          armySupply >= std::min(enemyArmySupply, 12.0)) {
        build(Zerg_Hatchery, nextBase);
      }
      buildN(Zerg_Drone, 28);
    }
    buildN(Zerg_Hatchery, 3);

    buildN(Zerg_Spire, 1);
    buildN(Zerg_Drone, 18);

    buildN(Zerg_Lair, 1);

    if (!hasOrInProduction(st, Zerg_Spire)) {
      if (armySupply > enemyArmySupplyInOurBase) {
        if (enemyArmySupply >= 8.0) {
          buildSunkens(4);
        }
        if (enemyArmySupply >= 12.0) {
          buildSunkens(5);
        }
      } else if (armySupply < 8.0) {
        build(Zerg_Zergling);
      }
    }

    buildN(Zerg_Drone, 14);
    if (st.workers < 14) {
      buildN(Zerg_Zergling, 2);
    }
    buildSunkens(
        (enemyZealotCount || enemyAttackingArmySupply ? 2 : 1) + hurtSunkens);
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
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOzvpmutas, UpcId, State*, Module*);
} // namespace cherrypi
