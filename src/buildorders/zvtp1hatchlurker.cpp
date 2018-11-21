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

class ABBOzvtp1hatchlurker : public ABBOBase {
  using ABBOBase::ABBOBase;

 public:
  // A cheesy Lurker rush.
  // In theory, a weak build that's easily answered. In practice, bots struggle
  // with the challenges posed by early Lurkers.
  //
  // Transitions into Mutalisks after establishing early Lurkers.

  Position sunkenPosition;
  virtual void preBuild2(autobuild::BuildState& bst) override {
    sunkenPosition = findSunkenPos(Zerg_Sunken_Colony, false, true);
    postBlackboardKey("TacticsAttack", true);
    postBlackboardKey(Blackboard::kMinScoutFrameKey, 0);
  }

  virtual void buildStep2(BuildState& bst) override {
    autoUpgrade = false;
    autoExpand = mineralFields < 7;
    buildExtraOverlordsIfLosingThem = false;
    bst.autoBuildRefineries = false;

    if (has(bst, Lurker_Aspect)) {
      build(Zerg_Zergling);
      buildN(Zerg_Extractor, geysers);
      build(Zerg_Mutalisk);
      if (countPlusProduction(bst, Zerg_Zergling) > 8) {
        upgrade(Metabolic_Boost);
      }
      takeNBases(bst, 1 + myDroneCount / 8);
      buildN(Zerg_Mutalisk, 6);
      buildN(Zerg_Drone, utils::safeClamp(9 * myCompletedHatchCount, 12, 40));
      buildN(Zerg_Extractor, myCompletedHatchCount);
      if (!enemyVultureCount) {
        takeNBases(bst, 2);
      }
      buildN(Zerg_Spire, 1);
      buildN(Zerg_Lurker, 5);
      if (enemyVultureCount) {
        buildSunkens(bst, myCompletedHatchCount, sunkenPosition);
      }
    } else {
      build(Zerg_Zergling);
      if (has(bst, Zerg_Spawning_Pool)) {
        buildN(Zerg_Drone, 18);
      }
      buildN(Zerg_Hydralisk, 5);
      upgrade(Lurker_Aspect);
      buildN(Zerg_Hydralisk_Den, 1);
      buildN(Zerg_Lair, 1);
      if (has(bst, Zerg_Spawning_Pool)) {
        buildN(Zerg_Drone, 11);
      }
      if (!has(bst, Zerg_Hydralisk_Den)) {
        buildN(Zerg_Zergling, 6);
      }
      buildN(Zerg_Drone, 9);
      buildN(Zerg_Overlord, 2);
      buildN(Zerg_Extractor, 1);
      buildN(Zerg_Spawning_Pool, 1);
      if (!hasOrInProduction(bst, Zerg_Extractor)) {
        buildN(Zerg_Drone, 9);
      }
      buildN(Zerg_Drone, 8);
    }
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOzvtp1hatchlurker, UpcId, State*, Module*);
}
