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

class ABBOpvpgategateobs : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  // Make sure Robotics Facility isn't delayed, to get Observer in time for a DT
  // rush
  bool addedRobotics = false;

  virtual void preBuild2(autobuild::BuildState& bst) override {
    postBlackboardKey(Blackboard::kMinScoutFrameKey, 24 * 2 * 60);
    addedRobotics =
        addedRobotics || hasOrInProduction(bst, Protoss_Robotics_Facility);
  }

  virtual void buildStep2(autobuild::BuildState& bst) override {
    if (addedRobotics) {
      build(Protoss_Nexus, nextBase);
      build(Protoss_Zealot);
      buildN(Protoss_Assimilator, bases);
      buildN(Protoss_Gateway, bases * 3 + 1);

      upgrade(Singularity_Charge);
      build(Protoss_Dragoon);
      buildN(Protoss_Probe, 20 * bases);
      if (countUnits(bst, Protoss_Dragoon) >= 15) {
        buildN(Protoss_Nexus, 2, nextBase);
      }
      buildN(Protoss_Observer, 3);
    } else {
      buildN(Protoss_Robotics_Facility, 1);
      buildN(Protoss_Probe, 25);
      buildN(Protoss_Dragoon, 4);
      buildN(Protoss_Probe, 24);
      buildN(Protoss_Pylon, 4);
      buildN(Protoss_Probe, 22);
      buildN(Protoss_Dragoon, 2);
      buildN(Protoss_Probe, 21);
      buildN(Protoss_Gateway, 2);
      buildN(Protoss_Probe, 20);
      buildN(Protoss_Dragoon, 1);
      buildN(Protoss_Probe, 19);
      buildN(Protoss_Pylon, 3);
      buildN(Protoss_Probe, 18);
      buildN(Protoss_Zealot, 2);
      buildN(Protoss_Probe, 17);
      buildN(Protoss_Pylon, 2);
      buildN(Protoss_Probe, 16);
      buildN(Protoss_Cybernetics_Core, 1);
      buildN(Protoss_Probe, 14);
      buildN(Protoss_Zealot, 1);
      buildN(Protoss_Probe, 13);
      buildN(Protoss_Assimilator, 1);
      buildN(Protoss_Probe, 12);
      buildN(Protoss_Gateway, 1);
      buildN(Protoss_Probe, 10);
      buildN(Protoss_Pylon, 1);
      buildN(Protoss_Probe, 8);
    }
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOpvpgategateobs, UpcId, State*, Module*);
} // namespace cherrypi
