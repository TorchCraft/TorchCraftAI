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

class ABBOpvzffe5gategoon : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  virtual void preBuild2(autobuild::BuildState& bst) override {
    postBlackboardKey(
        Blackboard::kMinScoutFrameKey,
        hasOrInProduction(bst, Protoss_Pylon) ? 1 : 0);
  }

  virtual void buildStep2(autobuild::BuildState& bst) override {
    takeNBases(bst, 4);
    buildN(Protoss_Gateway, 4 * bases);
    takeNBases(bst, 3);
    upgrade(Protoss_Ground_Weapons_3) && upgrade(Protoss_Ground_Armor_3);
    buildN(Protoss_Gateway, 5, homePosition);
    buildN(Protoss_Assimilator, 2);
    buildN(Protoss_Photon_Cannon, 4, naturalDefencePos);
    if (has(bst, Protoss_Cybernetics_Core)) {
      build(Protoss_Dragoon);
    } else {
      build(Protoss_Zealot);
    }
    upgrade(Singularity_Charge);
    buildN(Protoss_Cybernetics_Core, 1, homePosition);
    buildN(Protoss_Probe, 21 * bases);
    buildN(Protoss_Assimilator, 1);
    buildN(Protoss_Gateway, 1);
    buildN(Protoss_Probe, 19);
    takeNBases(bst, 2);
    buildN(Protoss_Probe, 18);
    buildN(Protoss_Pylon, 2);
    buildN(Protoss_Probe, 15);

    buildN(Protoss_Photon_Cannon, 2, naturalDefencePos);
    buildN(Protoss_Probe, 14);
    buildN(Protoss_Forge, 1, naturalDefencePos);
    buildN(Protoss_Probe, 11);
    buildN(Protoss_Pylon, 1, naturalDefencePos);
    buildN(Protoss_Probe, 8);
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOpvzffe5gategoon, UpcId, State*, Module*);
} // namespace cherrypi
