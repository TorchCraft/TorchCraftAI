/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "base.h"
#include "registry.h"

namespace cherrypi {

class ABBO9PoolSpeedLingMuta : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  Position nextSunkenPos;
  bool waitForSpire = false;

  virtual void preBuild2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    postBlackboardKey(Blackboard::kMinScoutFrameKey, 0);

    bool attack = armySupply >= enemyArmySupply ||
        !state_->unitsInfo().myUnitsOfType(Zerg_Mutalisk).empty();
    postBlackboardKey("TacticsAttack", attack);

    nextSunkenPos = findSunkenPos(Zerg_Sunken_Colony);

    waitForSpire = false;
    if (armySupply >= enemyArmySupply) {
      if (!state_->unitsInfo().myUnitsOfType(Zerg_Spire).empty() &&
          state_->unitsInfo().myCompletedUnitsOfType(Zerg_Spire).empty()) {
        int larvaTime =
            342 * ((state_->unitsInfo().myUnitsOfType(Zerg_Hatchery).size() +
                    state_->unitsInfo().myUnitsOfType(Zerg_Lair).size()) *
                       3 -
                   state_->unitsInfo().myUnitsOfType(Zerg_Larva).size() + 1);
        for (Unit* u : state_->unitsInfo().myUnitsOfType(Zerg_Spire)) {
          if (u->remainingBuildTrainTime <= larvaTime) {
            waitForSpire = true;
            break;
          }
        }
        if (st.gas > st.minerals) {
          waitForSpire = true;
        }
      }
    }
  }

  virtual void buildStep2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    if (hasOrInProduction(st, Zerg_Creep_Colony)) {
      build(Zerg_Sunken_Colony);
      return;
    }

    if (waitForSpire) {
      build(Zerg_Mutalisk);
      buildN(Zerg_Drone, 12);
      return;
    }

    auto placeSunkens = [&](int n) {
      if (countPlusProduction(st, Zerg_Sunken_Colony) < n) {
        build(Zerg_Creep_Colony, nextSunkenPos);
      }
    };

    if (countPlusProduction(st, Zerg_Sunken_Colony) &&
        enemyArmySupply * 0.75 > armySupply) {
      placeSunkens(4);
      build(Zerg_Zergling);
      buildN(Zerg_Drone, 14);
      placeSunkens(3);
    } else {
      placeSunkens(2);
      build(Zerg_Zergling);
    }
    if (st.gas >= 100.0) {
      build(Metabolic_Boost);
      buildN(Zerg_Lair, 1);
    }
    int mutaCount = countPlusProduction(st, Zerg_Mutalisk);
    if (has(st, Zerg_Lair)) {
      build(Zerg_Mutalisk);
      if (enemyRace == +tc::BW::Race::Zerg &&
          (mutaCount < 6 || enemyMutaliskCount >= mutaCount / 2)) {
        buildN(Zerg_Scourge, 1 + mutaCount / 2);
      }
    }
    if (enemyArmySupply >
        armySupply + countPlusProduction(st, Zerg_Sunken_Colony) * 3) {
      placeSunkens(4);
    }
    if (armySupply >= enemyArmySupply ||
        countPlusProduction(st, Zerg_Sunken_Colony)) {
      buildN(Zerg_Drone, 11);
      if (enemyMutaliskCount > mutaCount && enemyMutaliskCount < 9) {
        buildN(Zerg_Scourge, std::min(enemyMutaliskCount + 2, 8));
      }
    }
    if (st.frame < 15 * 60 * 4) {
      buildN(Zerg_Zergling, 6);
    }
    buildN(Zerg_Extractor, 1);
    if (countPlusProduction(st, Zerg_Spawning_Pool) == 0) {
      build(Zerg_Spawning_Pool);
      buildN(Zerg_Drone, 9);
    }
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBO9PoolSpeedLingMuta, UpcId, State*, Module*);

} // namespace cherrypi
