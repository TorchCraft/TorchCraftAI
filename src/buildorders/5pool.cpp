/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "base.h"

namespace cherrypi {

class ABBO5Pool : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  virtual void preBuild2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;
    postBlackboardKey(Blackboard::kMinScoutFrameKey, 0);
  }

  virtual void buildStep2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    build(Zerg_Zergling);

    buildN(Zerg_Spawning_Pool, 1);
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBO5Pool, UpcId, State*, Module*);
}
