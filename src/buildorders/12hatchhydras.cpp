/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "base.h"

namespace cherrypi {

class ABBO12HatchHydras : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  virtual void preBuild2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;
  }

  virtual void buildStep2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    st.autoBuildRefineries = countPlusProduction(st, Zerg_Extractor) == 0;

    build(Zerg_Hydralisk);
    upgrade(Grooved_Spines) && upgrade(Muscular_Augments);

    buildN(Zerg_Hydralisk, 6);
    buildN(Zerg_Drone, 20);
    if (st.frame < 15 * 60 * 9) {
      if (myCompletedHatchCount >= 2) {
        if (hasOrInProduction(st, Zerg_Creep_Colony)) {
          build(Zerg_Sunken_Colony);
        } else {
          int thresholdSunkens = 0;
          if (enemyArmySupply > armySupply)
            thresholdSunkens = 1;
          if (enemyArmySupply > 1.5 * armySupply)
            thresholdSunkens = 2;
          if (enemyArmySupply > 2.5 * armySupply)
            thresholdSunkens = 3;
          if ((countPlusProduction(st, Zerg_Sunken_Colony) +
               countPlusProduction(st, Zerg_Creep_Colony)) < thresholdSunkens) {
            build(Zerg_Creep_Colony, nextStaticDefencePos);
          }
        }
      }
    }
    buildN(Zerg_Drone, 15);

    buildN(Zerg_Spawning_Pool, 1);
    if (countPlusProduction(st, Zerg_Hatchery) == 1) {
      build(Zerg_Hatchery, nextBase);
      buildN(Zerg_Drone, 12);
    }
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBO12HatchHydras, UpcId, State*, Module*);
}
