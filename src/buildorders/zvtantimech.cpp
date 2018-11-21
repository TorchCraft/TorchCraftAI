/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "base.h"

namespace cherrypi {

class ABBOzvtantimech : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  Position baseSunkenPos;

  virtual void preBuild2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    preferSafeExpansions = false;

    baseSunkenPos = kInvalidPosition;
    if (state_->areaInfo().numMyBases() >= 3) {
      for (int i = state_->areaInfo().numMyBases(); i > 1;) {
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

    st.autoBuildRefineries = st.frame >= 24 * 60 * 6;

    int droneCount = countPlusProduction(st, Zerg_Drone);
    int mutaliskCount = countPlusProduction(st, Zerg_Mutalisk);
    int hydraliskCount = countPlusProduction(st, Zerg_Hydralisk);
    int zerglingCount = countPlusProduction(st, Zerg_Zergling);

    if (st.frame < 24 * 60 * 4) {
      buildN(Zerg_Drone, 18);
      buildN(Zerg_Hydralisk, 2);
      buildN(Zerg_Drone, 16);
      buildN(Zerg_Extractor, 1);
      buildN(Zerg_Hydralisk_Den, 1);

      buildN(Zerg_Spawning_Pool, 1);
      if (countPlusProduction(st, Zerg_Hatchery) == 2) {
        build(Zerg_Hatchery, nextBase);
        buildN(Zerg_Drone, 13);
      }
      if (countPlusProduction(st, Zerg_Hatchery) == 1) {
        build(Zerg_Hatchery, nextBase);
        buildN(Zerg_Drone, 12);
      }
      return;
    }

    build(Zerg_Zergling);
    if (hydraliskCount < 20 || zerglingCount >= hydraliskCount) {
      build(Zerg_Hydralisk);
    }

    if (droneCount >= 45 && armySupply >= 90.0 &&
        armySupply > enemyAttackingArmySupply) {
      buildN(Zerg_Drone, 74);
      if (canExpand && !st.isExpanding) {
        build(Zerg_Hatchery, nextBase);
      }
    } else {
      if (droneCount < 40 || countProduction(st, Zerg_Drone) == 0) {
        buildN(Zerg_Drone, 90);
      }
    }

    if (has(st, Zerg_Missile_Attacks_3)) {
      upgrade(Zerg_Missile_Attacks_3);
    }
    if (has(st, Zerg_Missile_Attacks_1)) {
      upgrade(Zerg_Missile_Attacks_2);
    }
    upgrade(Zerg_Missile_Attacks_1);

    if (has(st, Zerg_Flyer_Attacks_3)) {
      upgrade(Zerg_Flyer_Attacks_3);
    }
    if (has(st, Zerg_Flyer_Attacks_1)) {
      upgrade(Zerg_Flyer_Attacks_2);
    }
    upgrade(Zerg_Flyer_Attacks_1);

    if (armySupply > enemyAttackingArmySupply * 2 - enemyVultureCount * 1.5) {
      buildN(Zerg_Drone, 34);
    } else {
      buildN(Zerg_Hydralisk, 20);
    }

    if (droneCount >= mineralFields * 1.8 && canExpand && !st.isExpanding) {
      build(Zerg_Hatchery, nextBase);
    }

    if (droneCount >= 34) {
      buildN(Zerg_Hatchery, 8);
      if (armySupply > enemyAttackingArmySupply) {
        takeNBases(st, 5);
      }
    }

    upgrade(Pneumatized_Carapace);

    if (has(st, Zerg_Spire)) {
      if ((mutaliskCount < 7 ||
           hydraliskCount / armySupply >=
               enemyAntiAirArmySupply / enemyArmySupply) &&
          (mutaliskCount * 2.0 / armySupply <
           1.0 - enemyAntiAirArmySupply / enemyArmySupply)) {
        build(Zerg_Mutalisk);
      } else if (hydraliskCount >= 20 && mutaliskCount < hydraliskCount / 2) {
        build(Zerg_Mutalisk);
      }
    }

    if (droneCount >= 58) {
      buildN(Zerg_Evolution_Chamber, 3);
      upgrade(Zerg_Flyer_Carapace_3);
      upgrade(Zerg_Flyer_Attacks_3);
      upgrade(Zerg_Melee_Attacks_3);
      upgrade(Zerg_Carapace_3);
      upgrade(Zerg_Missile_Attacks_3);
    }

    buildN(Zerg_Spire, 1);
    buildN(Zerg_Hydralisk, 4);

    upgrade(Metabolic_Boost);
    buildN(Zerg_Extractor, 2);
    buildN(Zerg_Drone, 26);

    if (baseSunkenPos != kInvalidPosition &&
        countPlusProduction(st, Zerg_Creep_Colony) == 0) {
      build(Zerg_Creep_Colony, baseSunkenPos);
    }

    upgrade(Muscular_Augments) && upgrade(Grooved_Spines);
    buildSunkens(st, 1);
    buildN(Zerg_Lair, 1);

    buildN(Zerg_Drone, 20);

    if (armySupply < 10.0) {
      buildN(Zerg_Hydralisk, 4 + enemyVultureCount / 2);
    } else {
      buildN(Zerg_Zergling, 4);
    }

    if (droneCount >= 28) {
      if (countProduction(st, Zerg_Overlord) < 1 &&
          st.usedSupply[tc::BW::Race::Zerg] >=
              st.maxSupply[tc::BW::Race::Zerg] - 14) {
        build(Zerg_Overlord);
      }
    }

    if (countPlusProduction(st, Zerg_Creep_Colony)) {
      build(Zerg_Sunken_Colony);
    }
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOzvtantimech, UpcId, State*, Module*);
}
