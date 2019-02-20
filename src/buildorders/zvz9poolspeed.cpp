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

class ABBOzvz9poolspeed : public ABBOBase {
  using ABBOBase::ABBOBase;

 public:
  // This is the pro-style ZvZ 9 Pool Speed build. This is intended to be a
  // core build order that's competitive against all possible openings.
  // https://liquipedia.net/starcraft/9_Pool_Speed_into_1_Hatch_Spire_(vs._Zerg)
  //
  // Goal:
  // Pressure the opponent with Speedlings, then transition into Mutalisks.
  // Aim to force the opponent to add static defense, allowing us to add Drones.

  Position sunkenPosition;
  virtual void preBuild2(autobuild::BuildState& bst) override {
    postBlackboardKey(Blackboard::kMinScoutFrameKey, 0);
    postBlackboardKey("TacticsAttack", true);
    sunkenPosition = findSunkenPos(Zerg_Sunken_Colony, bases < 2, true);

    if (!hasOrInProduction(bst, Zerg_Spire)) {
      postBlackboardKey(Blackboard::kGathererMinGasWorkers, 0);
      postBlackboardKey(
          Blackboard::kGathererMaxGasWorkers, 2 - int(bst.gas / 200));
    }
  }

  void zerglingDefense(BuildState& bst) {
    int zerglingDelta = countUnits(bst, Zerg_Zergling) - enemyZerglingCount;
    if (zerglingDelta < -1 && enemyResourceDepots > 1) {
      buildSunkens(bst, 2, sunkenPosition);
    }
    int larvaAtSpire = countUnits(bst, Zerg_Larva) +
        framesUntil(bst, Zerg_Spire) / kLarvaFrames;
    if (zerglingDelta < 8 * enemyProximity - 3 &&
        larvaAtSpire > std::min({3., bst.minerals / 100., bst.gas / 100.})) {
      build(Zerg_Zergling);
    }
    if (zerglingDelta < 0 && enemyProximity > 0.6) {
      buildSunkens(bst, 1, sunkenPosition);
    }
  }

  void lateGame(BuildState& bst) {
    // Time delayed because sometimes it clogs the queue at inappropriate times
    if (bst.frame > 24 * 60 * 5) {
      buildN(Zerg_Hatchery, 1 + countPlusProduction(bst, Zerg_Drone) / 8);
    }
    build(Zerg_Zergling);
    if (myMutaliskCount > std::max(6, enemyMutaliskCount) ||
        myZerglingCount > std::max(18, enemyZerglingCount)) {
      buildN(Zerg_Drone, 12 * bases, 1);
      takeNBases(bst, 2);
    }
    buildN(Zerg_Zergling, enemyZerglingCount);
    buildN(
        Zerg_Drone,
        9 + myMutaliskCount / 3 + 4 * enemySunkenCount + 6 * enemySporeCount);

    build(Zerg_Mutalisk);
    if (countPlusProduction(bst, Zerg_Mutalisk) >= 8) {
      upgrade(Zerg_Flyer_Carapace_1) && upgrade(Zerg_Flyer_Attacks_1) &&
          upgrade(Zerg_Flyer_Carapace_2) && upgrade(Zerg_Flyer_Attacks_2);
    }

    buildN(Zerg_Extractor, std::min(geysers, bst.workers / 7));
    buildN(Zerg_Drone, 10);
    zerglingDefense(bst);
    buildN(Zerg_Scourge, 2 * enemyMutaliskCount);
    buildN(Zerg_Mutalisk, 5);
    buildN(Zerg_Drone, 8);
  }

  void doBuildOrder(BuildState& bst) {
    build(Zerg_Zergling);
    buildN(Zerg_Lair, 1);
    upgrade(Metabolic_Boost);
    zerglingDefense(bst);
    buildN(Zerg_Zergling, 10);
    buildN(Zerg_Drone, 9);
    buildN(Zerg_Overlord, 2);
    buildN(Zerg_Extractor, 1);
    buildN(Zerg_Spawning_Pool, 1);
    if (!hasOrInProduction(bst, Zerg_Extractor)) {
      buildN(Zerg_Drone, 9);
    }
    buildN(Zerg_Drone, 8);
  }

  virtual void buildStep2(BuildState& bst) override {
    autoUpgrade = countUnits(bst, Zerg_Extractor) > 2;
    autoExpand = bst.frame > 24 * 60 * 8;
    buildExtraOverlordsIfLosingThem = false;
    bst.autoBuildRefineries = false;

    if (hasOrInProduction(bst, Metabolic_Boost)) {
      lateGame(bst);
    } else {
      doBuildOrder(bst);
    }
    morphSunkens(bst);
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOzvz9poolspeed, UpcId, State*, Module*);
} // namespace cherrypi
