/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "buildorders/base.h"
#include "registry.h"

#include <gflags/gflags.h>

DEFINE_int32(
    sunken_modifier,
    -2,
    "Controls the amount of sunken colonies built");

namespace cherrypi {

/**
 * A varitation of ABBO9PoolSpeedLingMuta with a configurable amount of sunken
 * colonies.
 *
 * Another difference is hat a certain number of sunkens will always be
 * constructed, irrespective of the opponent's army supply.
 */
class ABBO9PoolSpeedLingMutaCustom : public ABBOBase {
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

    if (countPlusProduction(st, Zerg_Drone) >= 9 && st.gas < 600.0 &&
        (hasOrInProduction(st, Zerg_Lair) || st.gas < 100.0)) {
      if (armySupply < enemyGroundArmySupply) {
        postBlackboardKey("GathererMinGasGatherers", 2);
        postBlackboardKey("GathererMaxGasGatherers", 2);
      } else {
        postBlackboardKey("GathererMinGasGatherers", 3);
        postBlackboardKey("GathererMaxGasGatherers", 3);
      }
    } else {
      postBlackboardKey("GathererMinGasGatherers", 0);
      postBlackboardKey("GathererMaxGasGatherers", 0);
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
      placeSunkens(4 + FLAGS_sunken_modifier);
      build(Zerg_Zergling);
      buildN(Zerg_Drone, 14);
      placeSunkens(3 + FLAGS_sunken_modifier);
    } else {
      placeSunkens(2 + FLAGS_sunken_modifier);
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
    placeSunkens(4 + FLAGS_sunken_modifier);
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

REGISTER_SUBCLASS_3(
    ABBOBase,
    ABBO9PoolSpeedLingMutaCustom,
    UpcId,
    State*,
    Module*);

} // namespace cherrypi
