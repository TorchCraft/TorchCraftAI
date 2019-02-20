/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "base.h"

namespace cherrypi {

class ABBOultras : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  virtual void preBuild2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;
  }

  virtual void buildStep2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    st.autoBuildRefineries = countPlusProduction(st, Zerg_Extractor) == 0 ||
        st.minerals >= 200 || st.frame >= 24 * 60 * 8;

    int droneCount = countPlusProduction(st, Zerg_Drone);

    if (st.minerals >= 220) {
      if (countProduction(st, Zerg_Drone) < 2 &&
          armySupply >
              enemyArmySupply * 0.66 + enemyAttackingArmySupply * 0.75 &&
          droneCount >= 22) {
        build(Zerg_Drone);
      } else {
        build(Zerg_Zergling);
      }
    }

    build(Zerg_Ultralisk);

    if (droneCount >= 26 && armySupply >= enemyArmySupply) {
      if (countProduction(st, Zerg_Drone) == 0) {
        buildN(Zerg_Drone, 64);
      }
    }

    upgrade(Chitinous_Plating) && upgrade(Anabolic_Synthesis);

    if (shouldExpand && !st.isExpanding) {
      build(Zerg_Hatchery, nextBase);
    }
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOultras, UpcId, State*, Module*);
} // namespace cherrypi
