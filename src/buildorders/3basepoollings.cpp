/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "base.h"

namespace cherrypi {

class ABBO3BasePoolLings : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  bool hasSunken = false;
  Position baseSunkenPos;

  virtual void preBuild2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    hasSunken = !state_->unitsInfo().myUnitsOfType(Zerg_Sunken_Colony).empty();

    bool attack = true;
    if (st.frame < 24 * 60 * 16 && armySupply < enemyArmySupply + 8.0) {
      attack = false;
    }
    postBlackboardKey("TacticsAttack", attack);

    baseSunkenPos = kInvalidPosition;
    if (state_->areaInfo().numMyBases() >= 3) {
      for (int i = state_->areaInfo().numMyBases(); i != 0;) {
        --i;
        Unit* depot = state_->areaInfo().myBase(i)->resourceDepot;
        if (depot) {
          baseSunkenPos = findSunkenPosNear(Zerg_Sunken_Colony, depot->pos());
          if (baseSunkenPos != kInvalidPosition) {
            Unit* sunken = utils::getBestScoreCopy(
                state_->unitsInfo().myBuildings(),
                [&](Unit* u) {
                  if (u->type != Zerg_Sunken_Colony &&
                      u->type != Zerg_Creep_Colony) {
                    return kfInfty;
                  }
                  float d = utils::distance(u, baseSunkenPos);
                  if (d > 4 * 12) {
                    return kfInfty;
                  }
                  return d;
                },
                kfInfty);
            if (sunken) {
              baseSunkenPos = kInvalidPosition;
            } else {
              break;
            }
          }
        }
      }
    }
  }

  virtual void buildStep2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    if (enemyIsRushing && armySupply < enemyAttackingArmySupply + 1.0 &&
        (enemyAttackingArmySupply ||
         (!enemyHasExpanded && enemyStaticDefenceCount == 0))) {
      if (myCompletedHatchCount >= 2 && nextStaticDefencePos != Position()) {
        if (!hasSunken) {
          buildSunkens(st, 2);
        }
      }
      // this flag helps prioritize the hatchery
      auto buildingDefenses = false;
      if (countPlusProduction(st, Zerg_Zergling) <
          std::max(enemyArmySupply * 3, 8.)) {
        build(Zerg_Zergling);
        buildingDefenses = true;
      }
      if (countPlusProduction(st, Zerg_Hatchery) == 1) {
        build(Zerg_Hatchery, nextBase);
      }
      if (buildingDefenses) {
        return;
      }
    }

    if (bases < 6 && armySupply > enemyAttackingArmySupply * 2 &&
        st.minerals < 500 && !st.isExpanding) {
      if (countPlusProduction(st, Zerg_Hatchery) < 8) {
        build(Zerg_Hatchery, nextBase);
      }
    }

    st.autoBuildRefineries =
        st.workers >= 50 || countUnits(st, Zerg_Extractor) >= 2;
    build(Zerg_Zergling);

    buildN(Zerg_Drone, 70);

    buildN(Zerg_Scourge, (int)enemyAirArmySupply);
    buildN(Zerg_Spire, 1);

    if (baseSunkenPos != kInvalidPosition &&
        countPlusProduction(st, Zerg_Creep_Colony) == 0) {
      build(Zerg_Creep_Colony, baseSunkenPos);
    }

    // buildN(Zerg_Zergling, 20);
    buildN(Zerg_Drone, 40);

    if (countUnits(st, Zerg_Drone) > 30 && countUnits(st, Zerg_Zergling) > 16) {
      upgrade(Zerg_Carapace_2) && upgrade(Zerg_Melee_Attacks_2) &&
          upgrade(Zerg_Carapace_3) && upgrade(Zerg_Melee_Attacks_3);
    }

    if (countPlusProduction(st, Zerg_Hatchery) == 3 && !st.isExpanding) {
      build(Zerg_Hatchery, nextBase);
    }

    upgrade(Zerg_Carapace_1) && upgrade(Zerg_Melee_Attacks_1);
    buildN(Zerg_Drone, 26);

    if (st.workers < 40 && armySupply < enemyAttackingGroundArmySupply) {
      build(Zerg_Zergling);
    }

    if (st.workers >= 42) {
      upgrade(Adrenal_Glands);
    }
    upgrade(Metabolic_Boost);
    buildN(Zerg_Extractor, 1);
    buildN(Zerg_Drone, 20);

    buildN(Zerg_Spawning_Pool, 1);
    if (countPlusProduction(st, Zerg_Hatchery) == 2) {
      build(Zerg_Hatchery, nextBase);
      buildN(Zerg_Drone, 14);
    }
    if (countPlusProduction(st, Zerg_Hatchery) == 1) {
      build(Zerg_Hatchery, nextBase);
      buildN(Zerg_Drone, 12);
    }

    if (countPlusProduction(st, Zerg_Creep_Colony)) {
      build(Zerg_Sunken_Colony);
    }
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBO3BasePoolLings, UpcId, State*, Module*);
} // namespace cherrypi
