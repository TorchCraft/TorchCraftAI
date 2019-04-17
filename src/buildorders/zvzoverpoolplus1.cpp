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

class ABBOzvzoverpoolplus1 : public ABBOBase {
  using ABBOBase::ABBOBase;

 public:
  // Overpool into +1 Zergling
  // Go for Zergling speed and +1 Melee attacks into Zergling pressure.
  // Against Mutalisks, drops Spore Colonies and transitions.

  Position mainSpore;
  Position naturalSpore;
  bool transition = false;
  virtual void preBuild2(autobuild::BuildState& bst) override {
    transition = transition || enemyMutaliskCount;
    if (transition) {
      mainSpore = findSunkenPosNear(Zerg_Spore_Colony, homePosition, true);
      naturalSpore = findSunkenPosNear(Zerg_Spore_Colony, naturalPos, true);
    } else {
      const int gasNeeded =
          (hasOrInProduction(bst, Metabolic_Boost) ? 0 : 100) +
          (hasOrInProduction(bst, Zerg_Melee_Attacks_1) ? 0 : 100) - bst.gas;
      const int gasWorkers = std::max(0, gasNeeded / 8);
      postBlackboardKey(Blackboard::kGathererMinGasWorkers, gasWorkers);
      postBlackboardKey(Blackboard::kGathererMaxGasWorkers, gasWorkers);
      postBlackboardKey(Blackboard::kMinScoutFrameKey, 0);
      postBlackboardKey("TacticsAttack", true);
    }
  }

  virtual void buildStep2(BuildState& bst) override {
    buildN(
        Zerg_Hatchery,
        (2 + countPlusProduction(bst, Zerg_Drone)) / 3,
        naturalPos);
    takeNBases(bst, 2);
    build(Zerg_Zergling);
    if (countPlusProduction(bst, Zerg_Zergling) >=
        std::max(enemyZerglingCount, 10)) {
      buildN(Zerg_Drone, 9);
    }
    upgrade(Metabolic_Boost);
    upgrade(Zerg_Melee_Attacks_1);
    buildN(Zerg_Zergling, 6);
    buildN(Zerg_Evolution_Chamber, 1);
    buildN(Zerg_Extractor, 1);
    buildN(Zerg_Spawning_Pool, 1);
    if (transition) {
      build(Zerg_Zergling);
      buildN(Zerg_Drone, 19);
      buildN(Zerg_Extractor, 2);
      build(Zerg_Mutalisk);
      buildN(Zerg_Drone, 12);
      buildN(Zerg_Spire, 1);
      buildN(Zerg_Zergling, enemyZerglingCount + 2);
      buildN(Zerg_Drone, 9);
      const int sporeCount = 1 + (enemyMutaliskCount - myMutaliskCount) / 4;
      for (int i = 0; i < sporeCount; ++i) {
        buildSpores(bst, 2 * i, naturalSpore);
        buildSpores(bst, 2 * i - 1, mainSpore);
      }
    }
    buildN(Zerg_Overlord, 2);
    if (!has(bst, Zerg_Spawning_Pool)) {
      buildN(Zerg_Drone, 9);
    }
    buildN(Zerg_Drone, 6);
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOzvzoverpoolplus1, UpcId, State*, Module*);
} // namespace cherrypi
