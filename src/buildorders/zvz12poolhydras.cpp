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

class ABBOzvz12poolhydras : public ABBOBase {
  using ABBOBase::ABBOBase;

 public:
  // Pro players don't use Hydralisks in ZvZ, but it's a mostly-valid strategy.
  // Bots, in particular, are likely unable to take full advantage of the
  // mobility of Mutalisks.
  //
  // This build turtles on two bases to mass upgraded Hydralisk with Lurker

  bool readyToAttack = false;
  virtual void preBuild2(autobuild::BuildState& bst) override {
    readyToAttack =
        readyToAttack || (myLurkerCount >= 2 && myHydraliskCount >= 30);
    postBlackboardKey("TacticsAttack", readyToAttack || weArePlanningExpansion);
    postBlackboardKey(Blackboard::kMinScoutFrameKey, 0);
  }

  virtual void buildStep2(BuildState& bst) override {
    autoUpgrade = countUnits(bst, Zerg_Extractor) > 2;
    autoExpand = readyToAttack;
    buildExtraOverlordsIfLosingThem = false;
    bst.autoBuildRefineries = bases > 2;

    build(Zerg_Zergling);
    buildN(Zerg_Hatchery, 6);
    upgrade(Lurker_Aspect);
    build(Zerg_Hydralisk);
    buildN(Zerg_Lurker, (countPlusProduction(bst, Zerg_Hydralisk) - 10) / 4);
    if (countPlusProduction(bst, Zerg_Lurker) >= 2) {
      takeNBases(bst, 3);
    }
    buildN(Zerg_Evolution_Chamber, 2);
    upgrade(Grooved_Spines) && upgrade(Muscular_Augments);
    buildN(Zerg_Extractor, 2);
    buildN(Zerg_Hydralisk_Den, 1);
    buildN(Zerg_Hatchery, 4);
    buildN(Zerg_Evolution_Chamber, 1);
    if (myZerglingCount >= enemyZerglingCount) {
      buildN(Zerg_Hatchery, 3);
      takeNBases(bst, 2);
    } else {
      takeNBases(bst, 2);
      buildN(Zerg_Hatchery, 2);
    }
    (has(bst, Zerg_Hydralisk_Den)
         ? buildN(
               Zerg_Hydralisk,
               6 + 2 * enemyMutaliskCount +
                   (enemyZerglingCount - myZerglingCount) / 3)
         : buildN(Zerg_Zergling, 10 + enemyZerglingCount)) &&
        buildN(Zerg_Drone, std::min(bases * 20, 40));
    const int evolutionChambers = countUnits(bst, Zerg_Evolution_Chamber);
    if (evolutionChambers > 1) {
      upgrade(Zerg_Carapace_1) && upgrade(Zerg_Carapace_2);
      upgrade(Zerg_Missile_Attacks_1) && upgrade(Zerg_Missile_Attacks_2);
    } else if (evolutionChambers > 0) {
      upgrade(Zerg_Carapace_1) && upgrade(Zerg_Missile_Attacks_1) &&
          upgrade(Zerg_Carapace_2) && upgrade(Zerg_Missile_Attacks_2);
    }
    if (evolutionChambers > 0) {
      if (countPlusProduction(bst, Zerg_Drone) >= 14) {
        buildSpores(bst, 4, naturalPos);
        buildSpores(bst, bases > 1 ? 3 : 2, homePosition);
      }
      if (countPlusProduction(bst, Zerg_Drone) >= 8) {
        buildSpores(bst, 2, naturalPos);
        buildSpores(bst, 1, homePosition);
      }
    }
    upgrade(Metabolic_Boost);
    const int emergencySunkens =
        std::min(2, (enemyZerglingCount - myZerglingCount) / 4);
    if (emergencySunkens > 0 && bases < 2) {
      buildSunkens(bst, emergencySunkens, homePosition);
    }
    buildN(Zerg_Drone, 10);
    buildN(Zerg_Zergling, 10);
    buildN(Zerg_Extractor, 1);
    buildN(Zerg_Zergling, 8);
    buildN(Zerg_Hatchery, 2);
    if (countPlusProduction(bst, Zerg_Spawning_Pool) < 1) {
      buildN(Zerg_Drone, 12) && buildN(Zerg_Spawning_Pool, 1);
    }
    buildN(Zerg_Overlord, 2);
    buildN(Zerg_Drone, 9);
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOzvz12poolhydras, UpcId, State*, Module*);
} // namespace cherrypi
