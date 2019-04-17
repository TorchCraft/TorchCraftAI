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

class ABBOpve4gate : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  virtual void preBuild2(autobuild::BuildState& bst) override {
    postBlackboardKey(Blackboard::kMinScoutFrameKey, 24 * 2 * 60);
  }

  virtual void buildStep2(autobuild::BuildState& bst) override {
    build(Protoss_Nexus, nextBase);
    build(Protoss_Zealot);
    buildN(Protoss_Assimilator, bases);
    buildN(Protoss_Gateway, bases * 3);
    buildN(Protoss_Probe, 20 * bases);
    build(Protoss_Dragoon);
    buildN(Protoss_Pylon, 4);
    buildN(Protoss_Dragoon, 3);
    buildN(Protoss_Gateway, 4);
    buildN(Protoss_Probe, 23);
    buildN(Protoss_Dragoon, 2);
    buildN(Protoss_Probe, 21);
    upgrade(Singularity_Charge);
    buildN(Protoss_Probe, 20);
    buildN(Protoss_Dragoon, 1);
    buildN(Protoss_Probe, 19);
    buildN(Protoss_Pylon, 3);
    buildN(Protoss_Probe, 18);
    buildN(Protoss_Zealot, 2);
    buildN(Protoss_Probe, 16);
    buildN(Protoss_Cybernetics_Core, 1);
    buildN(Protoss_Probe, 15);
    buildN(Protoss_Assimilator, 1);
    buildN(Protoss_Probe, 14);
    buildN(Protoss_Zealot, 1);
    buildN(Protoss_Probe, 13);
    buildN(Protoss_Pylon, 2);
    buildN(Protoss_Probe, 12);
    buildN(Protoss_Gateway, 1);
    buildN(Protoss_Probe, 10);
    buildN(Protoss_Pylon, 1);
    buildN(Protoss_Probe, 8);
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOpve4gate, UpcId, State*, Module*);
} // namespace cherrypi
