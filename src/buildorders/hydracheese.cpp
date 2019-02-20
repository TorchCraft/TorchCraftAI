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

class ABBOhydracheese : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  virtual void preBuild2(autobuild::BuildState& bst) override {
    postBlackboardKey(Blackboard::kMinScoutFrameKey, 0);
    postBlackboardKey("TacticsAttack", armySupply >= 12 || myMutaliskCount);
  }

  virtual void buildStep2(autobuild::BuildState& bst) override {
    preferSafeExpansions = false;
    autoUpgrade = autoExpand = currentFrame > 24 * 60 * 12;
    bst.autoBuildRefineries = true;

    buildN(Zerg_Drone, 75);
    build(Zerg_Mutalisk);
    buildN(Zerg_Hydralisk, 2 * myMutaliskCount);

    buildN(Zerg_Drone, 45);
    takeNBases(bst, 5);
    buildN(Zerg_Hydralisk, 18);
    buildN(Zerg_Mutalisk, 12);
    buildN(Zerg_Drone, 30);
    takeNBases(bst, 4);
    buildN(Zerg_Guardian, 4);
    buildN(Zerg_Mutalisk, 12);
    upgrade(Pneumatized_Carapace);
    buildN(Zerg_Hydralisk, 18);
    buildN(Zerg_Guardian, 2);
    takeNBases(bst, 3);
    buildN(Zerg_Hive, 1);
    buildN(Zerg_Hydralisk, 9);
    buildN(Zerg_Drone, 24);
    upgrade(Muscular_Augments);
    buildN(Zerg_Hydralisk, 6);
    upgrade(Grooved_Spines);
    buildN(Zerg_Hatchery, 3);
    buildN(Zerg_Mutalisk, 12);
    buildN(Zerg_Extractor, bases);
    buildN(Zerg_Spire, 1);
    buildN(Zerg_Hydralisk, 3);
    if (has(bst, Zerg_Spire)) {
      buildN(Zerg_Mutalisk, 12);
    }
    buildN(Zerg_Drone, armySupply);
    if (!has(bst, Zerg_Hive)) {
      buildN(Zerg_Lair, 1);
    }
    buildN(Zerg_Drone, 18);
    buildSunkens(bst, 1);
    buildN(Zerg_Hydralisk_Den, 1);
    takeNBases(bst, 2);
    buildN(Zerg_Extractor, 1);
    buildN(Zerg_Spawning_Pool, 1);
    buildN(Zerg_Drone, 12);
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOhydracheese, UpcId, State*, Module*);
} // namespace cherrypi
