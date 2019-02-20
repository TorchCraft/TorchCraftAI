/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "base.h"

namespace cherrypi {

using namespace cherrypi::buildtypes;
using namespace cherrypi::autobuild;

class ABBOzve9poolspeed : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  // 9 Pool Speedlings
  // Because sometimes you just need Zerglings, right now.
  //
  // A crude build order, largely designed to give the Build Order Switcher
  // a muscular strategy to follow in dicey situations.

  int gasNeeded = 0;
  int gasDrones = 0;
  bool shouldScout = false;
  virtual void preBuild2(autobuild::BuildState& bst) override {
    gasNeeded = hasOrInProduction(bst, Metabolic_Boost)
        ? 0
        : std::max(0, 100 - int(bst.gas));
    gasDrones = utils::clamp(gasNeeded / 8, 0, std::max(0, myDroneCount - 3));
    shouldScout = shouldScout ||
        (countPlusProduction(bst, Zerg_Drone) >= 9 &&
         isInProduction(bst, Zerg_Spawning_Pool));
    postBlackboardKey("GathererMinGasGatherers", gasDrones);
    postBlackboardKey("GathererMaxGasGatherers", gasDrones);
    postBlackboardKey(Blackboard::kMinScoutFrameKey, shouldScout ? 1 : 0);
    postBlackboardKey("TacticsAttack", true);
  }

  virtual void buildStep2(BuildState& bst) override {
    int hatcheryCount = 1 + countPlusProduction(bst, Zerg_Drone) / 3;
    if (enemyRace == +tc::BW::Race::Zerg) {
      buildN(Zerg_Hatchery, hatcheryCount);
    } else {
      takeNBases(bst, hatcheryCount);
    }
    build(Zerg_Zergling);
    upgrade(Metabolic_Boost);
    buildN(Zerg_Zergling, 12);
    if (gasNeeded > 0 && countPlusProduction(bst, Zerg_Drone) > 5) {
      buildN(Zerg_Extractor, 1);
    }
    buildN(Zerg_Spawning_Pool, 1);
    if (groundArmySupply >= enemyGroundArmySupply) {
      buildN(Zerg_Drone, 9);
    }
    buildN(
        Zerg_Drone,
        utils::clamp(
            countPlusProduction(bst, Zerg_Zergling),
            2 * myCompletedHatchCount,
            3 * myCompletedHatchCount));
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOzve9poolspeed, UpcId, State*, Module*);
} // namespace cherrypi
