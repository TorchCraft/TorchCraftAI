/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "base.h"

namespace cherrypi {

class ABBO2BaseMutas : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  virtual void buildStep2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    st.autoBuildRefineries = countPlusProduction(st, Zerg_Extractor) == 0;

    build(Zerg_Zergling);
    build(Zerg_Mutalisk);
    buildN(Zerg_Drone, 66);

    if (st.workers >= 32) {
      upgrade(Zerg_Carapace_2) && upgrade(Zerg_Flyer_Carapace_2) &&
          upgrade(Zerg_Carapace_3) && upgrade(Zerg_Flyer_Carapace_3) &&
          upgrade(Zerg_Flyer_Attacks_3);
    }

    if (countProduction(st, Zerg_Mutalisk) >= 32) {
      upgrade(Adrenal_Glands);
    }

    if (st.workers >= 25) {
      upgrade(Zerg_Carapace_1) && upgrade(Zerg_Flyer_Carapace_1) &&
          upgrade(Zerg_Flyer_Attacks_1);
    }

    buildN(Zerg_Zergling, 12);
    buildN(Zerg_Mutalisk, 6);
    upgrade(Metabolic_Boost);
    buildN(Zerg_Drone, 30);

    if (!has(st, Zerg_Spire) && isInProduction(st, Zerg_Spire)) {
      buildN(Zerg_Overlord, 7);
      buildN(Zerg_Extractor, 2);
      buildN(Zerg_Drone, 24);
      return;
    }

    buildN(Zerg_Spire, 1);
    buildN(Zerg_Drone, 20);

    if (st.frame < 15 * 60 * 9) {
      if (myCompletedHatchCount >= 2) {
        if (hasOrInProduction(st, Zerg_Creep_Colony)) {
          build(Zerg_Sunken_Colony);
        } else {
          if (countPlusProduction(st, Zerg_Sunken_Colony) < 3 &&
              !isInProduction(st, Zerg_Creep_Colony)) {
            build(Zerg_Creep_Colony, nextStaticDefencePos);
          }
        }
      }
    }

    buildN(Zerg_Drone, 16);
    buildN(Zerg_Spawning_Pool, 1);
    if (countPlusProduction(st, Zerg_Hatchery) == 1) {
      build(Zerg_Hatchery, nextBase);
      buildN(Zerg_Drone, 12);
    }
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBO2BaseMutas, UpcId, State*, Module*);
} // namespace cherrypi
