/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "base.h"
#include <bwem/map.h>
#include <math.h>

namespace cherrypi {

using namespace cherrypi::buildtypes;
using namespace cherrypi::autobuild;

class ABBOzvp6hatchhydra : public ABBOBase {
  using ABBOBase::ABBOBase;

 public:
  // 5 Hatch Before Gas into Hydralisks
  // https://liquipedia.net/starcraft/5_Hatch_before_Gas_(vs._Protoss)
  //
  // A low-tech, high-econ macro build.

  bool transitionToSpeedlings = false;
  virtual void preBuild2(autobuild::BuildState& bst) override {
    transitionToSpeedlings = transitionToSpeedlings || armySupply >= 40;
    const bool scout = countPlusProduction(bst, Zerg_Overlord) > 1;
    postBlackboardKey(Blackboard::kMinScoutFrameKey, scout ? 1 : 0);
    postBlackboardKey("TacticsAttack", true);
  }

  void lateGame(BuildState& bst) {
    const int hydralisks = countPlusProduction(bst, Zerg_Hydralisk);
    const int baseTarget = enemyResourceDepots + 1 + hydralisks / 24;
    const int droneTarget =
        std::min(int(14 * baseTarget * (1.0 - enemyProximity)), 75);
    takeNBases(bst, baseTarget + 1);
    build(Zerg_Hydralisk);
    if (framesUntil(bst, Adrenal_Glands) < 24 * 10) {
      buildN(Zerg_Zergling, 2 * hydralisks);
    }
    if (transitionToSpeedlings) {
      buildN(Zerg_Evolution_Chamber, 2);
      buildN(Zerg_Hive, 1);
      upgrade(Metabolic_Boost) && upgrade(Adrenal_Glands);
      upgrade(Zerg_Melee_Attacks_3);
      upgrade(Zerg_Carapace_3);
      upgrade(Pneumatized_Carapace);
      buildN(Zerg_Lair, 1);
    }
    if (hydralisks >= 18) {
      upgrade(Zerg_Missile_Attacks_1) && upgrade(Zerg_Missile_Attacks_2) &&
          upgrade(Zerg_Missile_Attacks_3);
    }
    if (enemyDarkTemplarCount) {
      upgrade(Pneumatized_Carapace);
    }
    buildN(Zerg_Drone, droneTarget);
    takeNBases(bst, baseTarget);
    buildN(Zerg_Hydralisk, std::min(18, int(enemyArmySupply)));
    upgrade(Muscular_Augments) && upgrade(Grooved_Spines);
  }

  void opening(BuildState& bst) {
    bst.autoBuildRefineries = countPlusProduction(bst, Zerg_Hatchery) >= 6;
    const bool enemyExpanded =
        enemyResourceDepots > 1 || enemyForgeCount || enemyStaticDefenceCount;
    constexpr int zerglingMax = 18;
    const int zerglingTarget =
        int(1 + (enemyExpanded ? 0 : 4 * enemyGatewayCount) +
            1.5 * enemyGroundArmySupply +
            3.5 * enemyGroundArmySupply * enemyProximity - 4 * mySunkenCount);

    auto goHatcheries = [&](int count) {
      buildN(Zerg_Hatchery, count, count == 3 ? naturalPos : Position());
      if (enemyExpanded) {
        takeNBases(bst, 3);
      }
    };

    goHatcheries(6);
    build(Zerg_Hydralisk);
    buildN(Zerg_Drone, 40 - 20 * enemyProximity);
    upgrade(Muscular_Augments) && upgrade(Grooved_Spines);
    buildN(Zerg_Extractor, 2);
    goHatcheries(5);
    buildN(Zerg_Drone, 24);
    goHatcheries(4);
    buildN(Zerg_Drone, 15);
    buildN(Zerg_Zergling, std::min(zerglingTarget, zerglingMax));
    if (zerglingTarget >= zerglingMax || enemyCorsairCount) {
      buildN(Zerg_Hydralisk_Den, 1);
    }
    if (myZerglingCount > 8) {
      upgrade(Metabolic_Boost);
      buildN(Zerg_Extractor, 1);
    }
    buildN(Zerg_Spawning_Pool, 1);
    goHatcheries(3);
    if (!has(bst, Zerg_Spawning_Pool)) {
      buildN(Zerg_Drone, 14);
    }
    if (myCompletedHatchCount < 3 && !enemyExpanded) {
      buildSunkens(
          bst,
          std::min(
              std::max(enemyGatewayCount, int(enemyGroundArmySupply / 4)), 5));
    }
    if (enemyGatewayCount || enemyGroundArmySupply || !enemyExpanded) {
      buildN(Zerg_Spawning_Pool, 1);
    }
    takeNBases(bst, 2);
    if (countPlusProduction(bst, Zerg_Hatchery) < 2) {
      buildN(Zerg_Drone, 12);
    }
    buildN(Zerg_Overlord, 2);
    buildN(Zerg_Drone, 9);
  }

  virtual void buildStep2(BuildState& bst) override {
    if (has(bst, Zerg_Hydralisk_Den)) {
      lateGame(bst);
    } else {
      opening(bst);
    }
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOzvp6hatchhydra, UpcId, State*, Module*);
}
