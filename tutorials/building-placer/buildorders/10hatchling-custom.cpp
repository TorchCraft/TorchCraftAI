/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 */

#include "buildorders/base.h"
#include "registry.h"

namespace cherrypi {

/**
 * A variant of ABBO10HatchLing that is more vulnerable to early air attacks.
 */
class ABBO10HatchLingCustom : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  bool buildExtractor = false;
  bool hasBuiltExtractor = false;
  bool hasmadelings = false;
  bool hasBuiltHatchery = false;

  Position nextSporePos;
  virtual void preBuild2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    postBlackboardKey(Blackboard::kMinScoutFrameKey, 0);

    if (!hasBuiltExtractor && countPlusProduction(st, Zerg_Drone) == 9 &&
        countPlusProduction(st, Zerg_Overlord) == 1) {
      buildExtractor = true;
      hasBuiltExtractor = cancelGas();
    } else {
      buildExtractor = false;
    }

    bool attack = true;
    if (st.frame < 24 * 60 * 15) {
      if (enemyMutaliskCount && !has(st, Zerg_Spire) &&
          armySupply < enemyArmySupply) {
        attack = false;
      }
    }
    if (weArePlanningExpansion) {
      attack = true;
    }
    postBlackboardKey("TacticsAttack", attack);

    nextSporePos = findSunkenPos(Zerg_Spore_Colony, false, false);

    if (!hasmadelings) {
      if (countPlusProduction(st, Zerg_Zergling) >= 6) {
        hasmadelings = true;
      }
    }
    if (!hasBuiltHatchery && countPlusProduction(st, Zerg_Hatchery) >= 2) {
      hasBuiltHatchery = true;
    }
  }

  virtual void buildStep2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    st.autoBuildRefineries = st.workers >= 16 || st.frame >= 24 * 60 * 14;

    if (enemyRace != +tc::BW::Race::Terran &&
        enemyRace != +tc::BW::Race::Protoss) {
      if (!hasmadelings) {
        buildN(Zerg_Zergling, 6);

        buildN(Zerg_Spawning_Pool, 1);
        buildN(Zerg_Drone, 9);
        return;
      }
    }

    if (hasOrInProduction(st, Zerg_Creep_Colony)) {
      build(Zerg_Spore_Colony);
      return;
    }

    if (has(st, Zerg_Spawning_Pool)) {
      build(Zerg_Zergling);
      if (countPlusProduction(st, Zerg_Zergling) >= 40 || has(st, Zerg_Spire)) {
        build(Zerg_Mutalisk);
      }
      if (st.frame >= 15 * 60 * 7) {
        if (countProduction(st, Zerg_Drone) == 0 &&
            armySupply > enemyArmySupply + (st.workers >= 16 ? 8 : 0)) {
          build(Zerg_Drone);
        }
        if (st.workers >= 12) {
          buildN(Zerg_Spire, 1);
        }
      }
      if (st.workers >= 11) {
        build(Metabolic_Boost);
        buildN(Zerg_Extractor, 1);
      }
      buildN(Zerg_Zergling, 6);
    }
    buildN(Zerg_Overlord, 2);
    buildN(Zerg_Spawning_Pool, 1);
    if (countPlusProduction(st, Zerg_Hatchery) == 1 && !hasBuiltHatchery) {
      build(Zerg_Hatchery, nextBase);
      if (!hasBuiltExtractor && buildExtractor) {
        buildN(Zerg_Extractor, 1);
      }
      buildN(Zerg_Drone, 9);
    }
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBO10HatchLingCustom, UpcId, State*, Module*);
}
