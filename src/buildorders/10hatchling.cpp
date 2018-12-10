/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "base.h"
#include "registry.h"

namespace cherrypi {

class ABBO10HatchLing : public ABBOBase {
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

    auto placeSpores = [&](int n) {
      if (countPlusProduction(st, Zerg_Spore_Colony) < n) {
        build(Zerg_Creep_Colony, nextSporePos);
      }
      buildN(Zerg_Evolution_Chamber, 1);
    };

    if (has(st, Zerg_Spawning_Pool)) {
      build(Zerg_Zergling);
      if (enemyRace == +tc::BW::Race::Zerg) {
        if (st.workers < 12 && enemyStaticDefenceCount >= 2 &&
            armySupply >= enemyArmySupply + 6.0) {
          build(Zerg_Drone);
        }
        if (st.frame >= 24 * 60 * 5 &&
            armySupply >= enemyArmySupply + 8.0 - enemyStaticDefenceCount -
                    std::max(st.workers - 11, 0)) {
          int n = enemyLairCount + enemySpireCount ? 2 : 1;
          if (bases >= 2 && st.workers >= 11) {
            n += 2 + std::max((st.workers - 11) / 2, 2);
          }
          placeSpores(n);
          buildN(Zerg_Evolution_Chamber, 1);
        }
      }
      if (countPlusProduction(st, Zerg_Zergling) >= 80 ||
          (enemyRace == +tc::BW::Race::Zerg && has(st, Zerg_Spire))) {
        build(Zerg_Mutalisk);
      }
      if (st.frame >= 15 * 60 * 7 && enemyRace == +tc::BW::Race::Zerg) {
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

      if (enemyRace == +tc::BW::Race::Zerg) {
        if (countPlusProduction(st, Zerg_Hydralisk) +
                countPlusProduction(st, Zerg_Scourge) +
                countPlusProduction(st, Zerg_Mutalisk) <
            enemyAirArmySupply) {
          if (has(st, Zerg_Spire)) {
            buildN(
                Zerg_Scourge,
                (int)(enemyAirArmySupply - std::max(enemyMutaliskCount - countPlusProduction(st, Zerg_Mutalisk), 0) * 2));
          } else {
            if (enemyMutaliskCount) {
              buildN(Zerg_Spire, 1);
              placeSpores(std::max(enemyMutaliskCount / 3, 2));
            } else {
              build(Zerg_Hydralisk);
            }
          }
        }
      }
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

REGISTER_SUBCLASS_3(ABBOBase, ABBO10HatchLing, UpcId, State*, Module*);
}
