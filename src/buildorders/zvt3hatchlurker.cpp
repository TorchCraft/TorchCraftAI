/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "base.h"
#include <bwem/map.h>
#include <math.h>

namespace cherrypi {

using namespace cherrypi::buildtypes;
using namespace cherrypi::autobuild;

class ABBOzvt3hatchlurker : public ABBOBase {
  using ABBOBase::ABBOBase;

 public:
  // ZvT 3 Hatch Lurker
  // https://liquipedia.net/starcraft/3_Hatch_Lurker_(vs._Terran)
  //
  // Open with a muscular build that can react appropriately to all Terran
  // builds.
  //
  // Against bio/unknown:
  // * Open with standard 3-Hatch Lurker
  // * Transition into Defiler -> Ling-Lurker-Ultra-Defiler
  //
  // Against mech/2-port:
  // * Get an early Sunken and Hydralisks to prevent Vulture runbys
  // * Hydra-Muta composition -> Third base
  // * Get Consume -> Transition to Ling-Ultra-Defiler

  bool readyToScout = false;
  bool completedMutalisks = false;
  bool tookThirdBase = false;
  bool enemyOpenedBio = false;
  bool enemyOpenedMech = false;
  bool enemyMoreBio = false;
  int netGroundStrength = 0;

  void updateArmyStrength() {
    netGroundStrength = 1 * myZerglingCount + 2 * myHydraliskCount +
        3 * myMutaliskCount + 4 * myLurkerCount + 5 * myUltraliskCount +
        5 * myDefilerCount - 1 * enemyMarineCount - 2 * enemyMedicCount -
        2 * enemyVultureCount - 2 * enemyGoliathCount - 4 * enemyTankCount;
  }

  void updateBuildProgress() {
    readyToScout = readyToScout || bases > 1 || state_->resources().ore >= 276;
    completedMutalisks = completedMutalisks ||
        !state_->unitsInfo().myCompletedUnitsOfType(Zerg_Mutalisk).empty();
    tookThirdBase = tookThirdBase || bases >= 3;
  }

  void detectEnemyBuild() {
    enemyMoreBio =
        3 * enemyMarineCount - 2 * enemyVultureCount - 3 * enemyGoliathCount >
        0;
    if (enemyOpenedBio || enemyOpenedMech) {
      return;
    }
    enemyOpenedMech = enemyOpenedMech || enemyVultureCount >= 3 ||
        enemyGoliathCount || enemyTankCount || enemyWraithCount ||
        enemyFactoryCount;
    enemyOpenedBio = enemyOpenedBio || enemyBarracksCount > 1 ||
        enemyMarineCount >= 8 || enemyMedicCount || enemyFirebatCount ||
        (enemyAcademyCount && !enemyOpenedMech);
  }

  Position vultureSunken;
  virtual void preBuild2(autobuild::BuildState& bst) override {
    detectEnemyBuild();
    updateArmyStrength();
    updateBuildProgress();

    // Scout as we take our natural (readyToScout)
    // Reclaim the Drone once we figure out what they're doing
    auto scout = readyToScout && !enemyOpenedBio && !enemyOpenedMech;
    postBlackboardKey(Blackboard::kMinScoutFrameKey, scout ? 1 : 0);

    vultureSunken = findSunkenPos(Zerg_Sunken_Colony, false, true);

    postBlackboardKey(
        "TacticsAttack",
        tookThirdBase || enemyMoreBio || completedMutalisks ||
            !enemyVultureCount);
  }

  void sneakDrones(BuildState& bst, int consecutive) {
    if (countProduction(bst, Zerg_Drone) < consecutive &&
        (myHydraliskCount >= 3 || myZerglingCount >= 12 ||
         myMutaliskCount >= 6)) {
      buildN(Zerg_Drone, 75);
    }
  }

  void lateGameBio(BuildState& bst) {
    build(Zerg_Zergling);
    buildN(Zerg_Drone, 50);
    takeNBases(bst, 1 + countPlusProduction(bst, Zerg_Drone) / 12);
    build(Zerg_Ultralisk);
    upgrade(Chitinous_Plating) && upgrade(Anabolic_Synthesis);
    upgrade(Plague);
    upgrade(Adrenal_Glands);
    upgrade(Zerg_Carapace_3) && upgrade(Zerg_Melee_Attacks_3);
    upgrade(Consume);
    buildN(
        Zerg_Zergling,
        8 + 5 * enemyTankCount + 2 * enemyMarineCount + 3 * enemyMedicCount +
            3 * enemyGoliathCount);
    buildN(
        Zerg_Lurker,
        std::min(
            8,
            (enemyMarineCount + enemyMedicCount + 2 * enemyFirebatCount) / 3));
    if (hasOrInProduction(bst, Consume)) {
      buildN(Zerg_Defiler, 2 + myUltraliskCount / 4);
    }
    int hydralisks = enemyAirArmySupply + enemyVultureCount;
    hydralisks > 1 && upgrade(Grooved_Spines) && hydralisks > 3 &&
        upgrade(Muscular_Augments);
    buildN(Zerg_Hydralisk, hydralisks);
    upgrade(Metabolic_Boost);
    buildN(Zerg_Extractor, std::min(myDroneCount / 9, bases));
    sneakDrones(bst, netGroundStrength > 0 ? 2 : 1);
  }

  void lateGameMech(BuildState& bst) {
    bool goLingUltraDefiler = hasOrInProduction(bst, Consume);

    if (goLingUltraDefiler) {
      build(Zerg_Zergling);
      build(Zerg_Ultralisk);
      buildN(Zerg_Scourge, 3 * enemyScienceVesselCount);
    }
    upgrade(Zerg_Melee_Attacks_3) && upgrade(Zerg_Carapace_3);
    upgrade(Adrenal_Glands);
    upgrade(Anabolic_Synthesis) && upgrade(Chitinous_Plating);
    takeNBases(bst, 1 + countPlusProduction(bst, Zerg_Drone) / 12);
    buildN(Zerg_Defiler, 2 + myUltraliskCount / 4);
    upgrade(Consume);
    if (!goLingUltraDefiler) {
      if (enemyCloakedUnitCount || enemyVultureCount > 5) {
        upgrade(Pneumatized_Carapace);
      }
      buildN(
          Zerg_Hydralisk,
          enemyVultureCount + 2 * enemyWraithCount + 3 * enemyValkyrieCount +
              5 * enemyBattlecruiserCount);
      upgrade(Grooved_Spines) && upgrade(Muscular_Augments);
      buildN(Zerg_Mutalisk, 2 * enemyTankCount - enemyGoliathCount);
    }
    buildN(Zerg_Extractor, std::min(myDroneCount / 9, bases));
    sneakDrones(bst, netGroundStrength > 0 ? 3 : 1);
  }

  void buildOrder(BuildState& bst) {
    const int hydraTarget = (enemyOpenedMech ? 3 : 0) + enemyVultureCount +
        enemyWraithCount + 2 * enemyGoliathCount;

    build(Zerg_Zergling);
    buildN(Zerg_Drone, 44);
    takeNBases(bst, 3);
    buildN(Zerg_Drone, 30);
    if (enemyOpenedBio) {
      build(Zerg_Lurker);
    }
    buildN(Zerg_Hydralisk_Den, 1);
    if (enemyOpenedMech) {
      buildN(Zerg_Hydralisk, hydraTarget);
      sneakDrones(bst, 1);
      if (bst.gas >= std::min(100.0, bst.minerals)) {
        build(Zerg_Mutalisk);
      }
      upgrade(Grooved_Spines) && upgrade(Muscular_Augments);
      buildN(Zerg_Spire, 1, homePosition);
    }
    if (hasOrInProduction(bst, Zerg_Lair) &&
        countPlusProduction(bst, Zerg_Drone) >= 18) {
      buildN(Zerg_Extractor, 2);
    }
    if (enemyOpenedBio || countPlusProduction(bst, Zerg_Zergling) >= 6) {
      upgrade(Metabolic_Boost);
    }
    buildN(Zerg_Lair, 1);
    buildN(Zerg_Hydralisk, std::min(hydraTarget, 3));
    buildN(Zerg_Drone, 20);
    buildSunkens(bst, 1);
    buildN(Zerg_Extractor, 1);
    buildN(
        Zerg_Zergling, std::max(4, enemyOpenedMech ? 0 : 2 * enemyMarineCount));
    buildN(Zerg_Hatchery, 3, naturalPos);
    buildN(Zerg_Spawning_Pool, 1);
    buildN(Zerg_Drone, 13);
    takeNBases(bst, 2);
    buildN(Zerg_Drone, 12);
    buildN(Zerg_Overlord, 2);
    buildN(Zerg_Drone, 9);
  }

  virtual void buildStep2(BuildState& bst) override {
    autoUpgrade = false;
    preferSafeExpansions = !enemyOpenedMech;
    bst.autoBuildRefineries = countPlusProduction(bst, Zerg_Drone) >= 26;

    if (tookThirdBase) {
      if (enemyMoreBio) {
        lateGameBio(bst);
      } else {
        lateGameMech(bst);
      }
    } else {
      buildOrder(bst);
    }
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOzvt3hatchlurker, UpcId, State*, Module*);
} // namespace cherrypi
