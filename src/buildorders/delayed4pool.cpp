/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "base.h"

namespace cherrypi {

/**
 * Dummy build for verifying that opponents are functioning correctly.
 * This often proves difficult because there are a lot of bots we beat 100-0.
 *
 * Does a truly horrendous 4pool that should never win vs. any bot that's
 * even slightly functional.
 */
class ABBOdelayed4Pool : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  virtual void preBuild2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    // Waste a worker's time -- sort of scout but sort of don't
    auto shouldScout = st.frame % 5 > 0;
    postBlackboardKey(Blackboard::kMinScoutFrameKey, shouldScout ? 1 : 0);
  }

  virtual void buildStep2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    buildN(Zerg_Zergling, 2);

    // Two Spawning Pools to make sure the build is super bad
    buildN(Zerg_Spawning_Pool, 2);
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOdelayed4Pool, UpcId, State*, Module*);
}
