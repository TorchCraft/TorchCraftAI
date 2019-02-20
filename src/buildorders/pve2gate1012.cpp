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

class ABBOpve2gate1012 : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  virtual void preBuild2(autobuild::BuildState& bst) override {
    postBlackboardKey(
        Blackboard::kMinScoutFrameKey,
        hasOrInProduction(bst, Protoss_Pylon) ? 1 : 0);
  }

  virtual void buildStep2(autobuild::BuildState& bst) override {
    bool earlyZealots = !has(bst, Protoss_Cybernetics_Core);

    buildN(Protoss_Gateway, 5 * bases);
    buildN(Protoss_Nexus, bases + 1, nextBase);
    if (countPlusProduction(bst, Protoss_Gateway) > 5) {
      upgrade(Leg_Enhancements);
      upgrade(Protoss_Ground_Weapons_3) && upgrade(Protoss_Ground_Armor_3);
    }
    buildN(Protoss_Observer, bases > 1 ? 2 : 0);
    buildN(Protoss_Gateway, 4 * bases - 1);

    build(Protoss_Zealot);
    build(Protoss_Dragoon);
    if (hasOrInProduction(bst, Leg_Enhancements)) {
      buildN(Protoss_Zealot, countPlusProduction(bst, Protoss_Dragoon));
    }

    upgrade(Singularity_Charge);
    buildN(Protoss_Gateway, 3);
    buildN(Protoss_Cybernetics_Core, 1);
    buildN(Protoss_Assimilator, bases);
    buildN(Protoss_Probe, bases * 22);
    earlyZealots&& buildN(Protoss_Zealot, 5);
    earlyZealots&& buildN(Protoss_Zealot, 2);
    buildN(Protoss_Probe, 15);
    buildN(Protoss_Pylon, 2);
    earlyZealots&& buildN(Protoss_Zealot, 1);
    buildN(Protoss_Probe, 13);
    buildN(Protoss_Gateway, 2);
    buildN(Protoss_Probe, 12);
    buildN(Protoss_Gateway, 1);
    buildN(Protoss_Probe, 10);
    buildN(Protoss_Pylon, 1);
    buildN(Protoss_Probe, 8);
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOpve2gate1012, UpcId, State*, Module*);
} // namespace cherrypi
