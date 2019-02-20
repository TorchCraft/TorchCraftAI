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

class ABBOzvz9gas10pool : public ABBOBase {
  using ABBOBase::ABBOBase;

 public:
  // Goal:
  // Hit the fastest possible 6 Mutalisk timing by taking gas at 9 and turtling
  //
  // Abuses opponents who can't make the correct macro adaptations to either
  // the hard turtle or the Mutalisk timing. Dies to 4-7 Pool. So it's a strong
  // build to have in the arsenal but isn't a core strategy.

  bool completed6Mutalisks = false;
  bool completed12Drones = false;

  Position sunkenPosition;
  virtual void preBuild2(autobuild::BuildState& bst) override {
    // Don't scout. Minerals/Drones are too valuable here.
    postBlackboardKey(Blackboard::kMinScoutFrameKey, 0);

    completed6Mutalisks = completed6Mutalisks || myMutaliskCount >= 6;
    completed12Drones = completed12Drones || myDroneCount >= 12;

    sunkenPosition = findSunkenPos(Zerg_Sunken_Colony, bases < 2, true);

    // We are very weak until our timing attack
    postBlackboardKey("TacticsAttack", myMutaliskCount || completed6Mutalisks);

    const int gasWorkers = myDroneCount * 3 / 8;
    postBlackboardKey(Blackboard::kGathererMinGasWorkers, gasWorkers);
    postBlackboardKey(Blackboard::kGathererMaxGasWorkers, gasWorkers);
  }

  void lateGame(BuildState& bst) {
    takeNBases(bst, 2);
    build(Zerg_Zergling);
    if (bst.gas >= std::min(100, (int)bst.minerals)) {
      build(Zerg_Mutalisk);
    }
    buildN(Zerg_Drone, 18 * bases, 1);
    buildN(Zerg_Mutalisk, 8);
    if (countPlusProduction(bst, Zerg_Mutalisk) >= 6) {
      if (enemyMutaliskCount) {
        upgrade(Zerg_Flyer_Carapace_1) && upgrade(Zerg_Flyer_Attacks_1) &&
            upgrade(Zerg_Flyer_Carapace_2) && upgrade(Zerg_Flyer_Attacks_2);
      }
      upgrade(Metabolic_Boost);
    }
    buildN(Zerg_Extractor, std::min(geysers, bst.workers / 7));
    buildN(Zerg_Drone, 9);
  }

  void doBuildOrder(BuildState& bst) {
    if (hasOrInProduction(bst, Zerg_Spire)) {
      build(Zerg_Mutalisk);
      buildN(Zerg_Overlord, 4);
      upgrade(Zerg_Flyer_Carapace_1);
      buildN(Zerg_Mutalisk, 6);
      buildN(Zerg_Overlord, 3);
    }
    buildN(Zerg_Spire, 1);
    buildN(Zerg_Hatchery, 2);
    buildN(Zerg_Drone, 14);
    buildN(Zerg_Lair, 1);
    buildN(Zerg_Drone, 12);
    buildSunkens(bst, 2, sunkenPosition);
    if (!completed12Drones) {
      buildN(Zerg_Drone, 12);
    }
    buildN(Zerg_Spawning_Pool, 1);
    buildN(Zerg_Drone, 10);
    buildN(Zerg_Extractor, 1);
    buildN(Zerg_Overlord, 2);
    buildN(Zerg_Drone, 9);
  }

  virtual void buildStep2(BuildState& bst) override {
    autoUpgrade = countUnits(bst, Zerg_Extractor) > 2;
    autoExpand = bst.frame > 24 * 60 * 8;
    buildExtraOverlordsIfLosingThem = false;
    bst.autoBuildRefineries = false;

    if (completed6Mutalisks) {
      lateGame(bst);
    } else {
      doBuildOrder(bst);
    }
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOzvz9gas10pool, UpcId, State*, Module*);
} // namespace cherrypi
