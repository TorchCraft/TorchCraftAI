/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "base.h"

namespace cherrypi {

using namespace buildtypes;
using namespace autobuild;

class ABBOpvp2gatedt : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  bool openingComplete = false;
  bool readyToAttack = false;
  virtual void preBuild2(autobuild::BuildState& bst) override {
    openingComplete =
        openingComplete || countPlusProduction(bst, Protoss_Dark_Templar) > 1;
    readyToAttack = readyToAttack || has(bst, Protoss_Dark_Templar);
    postBlackboardKey(Blackboard::kMinScoutFrameKey, 24 * 2 * 60);
    postBlackboardKey("TacticsAttack", readyToAttack);
  }

  virtual void buildStep2(autobuild::BuildState& bst) override {
    build(Protoss_Zealot);
    buildN(Protoss_Gateway, 5 * bases);
    buildN(Protoss_Nexus, 1 + countPlusProduction(bst, Protoss_Probe) / 20);
    buildN(Protoss_Assimilator, bases);
    buildN(Protoss_Gateway, 3 * bases);
    upgrade(Leg_Enhancements);
    build(Protoss_Dragoon);
    if (hasOrInProduction(bst, Leg_Enhancements)) {
      buildN(Protoss_Zealot, countPlusProduction(bst, Protoss_Dragoon));
    }
    if (countPlusProduction(bst, Protoss_Assimilator) > 2) {
      upgrade(Protoss_Ground_Weapons_3) && upgrade(Protoss_Ground_Armor_3);
    }
    buildN(Protoss_Probe, 22 * bases);
    upgrade(Singularity_Charge);
    buildN(Protoss_Nexus, 2, nextBase);
    buildN(Protoss_Dark_Templar, 2);
    buildN(Protoss_Probe, 23);
    buildN(Protoss_Pylon, 5);
    openingComplete || buildN(Protoss_Zealot, 4);
    buildN(Protoss_Probe, 22);
    buildN(Protoss_Templar_Archives, 1);
    buildN(Protoss_Pylon, 4);
    buildN(Protoss_Gateway, 2);
    openingComplete || buildN(Protoss_Dragoon, 2);
    buildN(Protoss_Probe, 21);
    buildN(Protoss_Citadel_of_Adun, 1);
    buildN(Protoss_Probe, 20);
    openingComplete || buildN(Protoss_Dragoon, 1);
    buildN(Protoss_Probe, 19);
    buildN(Protoss_Pylon, 3);
    buildN(Protoss_Probe, 18);
    openingComplete || buildN(Protoss_Zealot, 2);
    buildN(Protoss_Probe, 17);
    buildN(Protoss_Cybernetics_Core, 1);
    buildN(Protoss_Probe, 16);
    buildN(Protoss_Pylon, 2);
    buildN(Protoss_Probe, 14);
    openingComplete || buildN(Protoss_Zealot, 1);
    if (has(bst, Protoss_Templar_Archives)) {
      buildN(Protoss_Dark_Templar, 2);
    }
    buildN(Protoss_Probe, 13);
    buildN(Protoss_Assimilator, 1);
    buildN(Protoss_Probe, 12);
    buildN(Protoss_Gateway, 1);
    buildN(Protoss_Probe, 10);
    buildN(Protoss_Pylon, 1);
    buildN(Protoss_Probe, 8);
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOpvp2gatedt, UpcId, State*, Module*);
} // namespace cherrypi
