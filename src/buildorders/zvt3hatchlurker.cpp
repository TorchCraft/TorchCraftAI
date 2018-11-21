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
  // * Transition into Lurkers, then take a third base
  // * Get upgrades, up to Adrenal Glands
  // * Build out Lurker-Ling army
  // * Take a fourth base
  //
  // Against mech/2-port:
  // * Get an early Sunken and Hydralisks to prevent Vulture runbys
  // * Get a Hydra-Muta composition, then take a third base
  // * Get upgrades; if 2-Port, include Overlord speed
  // * Build out Hydra-Muta army
  // * Take a fourth base
  //
  // Late game:
  // * Continue with appropriate composition
  // * Add Ultralisks

  bool readyToScout = false;
  bool completedBuildOrder = false;
  bool completedMutalisks = false;
  bool tookThirdBase = false;
  bool enemyIsAffirmativelyBio = false;
  bool enemyIsAffirmativelyMech = false;
  int netGroundStrength = 0;

  void updateArmyStrength() {
    netGroundStrength = 1 * myZerglingCount + 2 * myHydraliskCount +
        3 * myMutaliskCount + 4 * myLurkerCount + 5 * myUltraliskCount -
        1 * enemyMarineCount - 2 * enemyMedicCount - 2 * enemyVultureCount -
        2 * enemyGoliathCount - 4 * enemyTankCount;
  }

  void updateBuildProgress() {
    readyToScout = readyToScout || bases > 1 || state_->resources().ore >= 276;
    completedBuildOrder = completedBuildOrder ||
        !state_->unitsInfo().myUnitsOfType(Zerg_Lair).empty();
    completedMutalisks = completedMutalisks ||
        !state_->unitsInfo().myCompletedUnitsOfType(Zerg_Mutalisk).empty();
    tookThirdBase = tookThirdBase || bases >= 3;
  }

  void detectEnemyBuild() {
    if (enemyIsAffirmativelyBio || enemyIsAffirmativelyMech) {
      return;
    }
    enemyIsAffirmativelyMech = enemyIsAffirmativelyMech ||
        enemyVultureCount >= 3 || enemyGoliathCount || enemyTankCount ||
        enemyWraithCount || enemyFactoryCount;
    enemyIsAffirmativelyBio = enemyIsAffirmativelyBio ||
        enemyBarracksCount > 1 || enemyMarineCount >= 8 || enemyMedicCount ||
        enemyFirebatCount || (enemyAcademyCount && !enemyIsAffirmativelyMech);
  }

  Position vultureSunken;
  virtual void preBuild2(autobuild::BuildState& bst) override {
    detectEnemyBuild();
    updateArmyStrength();
    updateBuildProgress();

    // Scout as we take our natural (readyToScout)
    // Reclaim the Drone once we figure out what they're doing
    auto scout =
        readyToScout && !enemyIsAffirmativelyBio && !enemyIsAffirmativelyMech;
    postBlackboardKey(Blackboard::kMinScoutFrameKey, scout ? 1 : 0);

    vultureSunken = findSunkenPos(Zerg_Sunken_Colony, false, true);

    // Attack once we have Mutalisks
    // Attack if the opponent has no Vultures
    auto shouldAttack = completedMutalisks || enemyVultureCount < 2;
    postBlackboardKey("TacticsAttack", shouldAttack);
  }

  void expand(BuildState& bst) {
    if (!bst.isExpanding) {
      build(Zerg_Hatchery, nextBase);
    }
  }

  void sneakDrones(BuildState& bst, int consecutive) {
    if (countProduction(bst, Zerg_Drone) < consecutive &&
        (myHydraliskCount >= 3 || myZerglingCount >= 12 ||
         myMutaliskCount >= 6)) {
      buildN(Zerg_Drone, 75);
    }
  }

  void lateGame(BuildState& bst) {
    const int lurkerTarget =
        std::max(
            0, enemyMarineCount + enemyMedicCount + 2 * enemyFirebatCount) /
        3;
    const int hydraTarget = enemyVultureCount + 2 * enemyWraithCount +
        3 * enemyValkyrieCount + 5 * enemyBattlecruiserCount;
    const int zerglingTarget = 4 + 5 * enemyTankCount +
        (enemyIsAffirmativelyMech ? 0 : 12 + 2 * enemyMarineCount +
                 3 * enemyMedicCount + 3 * enemyGoliathCount);

    build(Zerg_Zergling);
    buildN(Zerg_Drone, 50);
    if (bases < countPlusProduction(bst, Zerg_Drone) / 12) {
      expand(bst);
    }
    if (countPlusProduction(bst, Zerg_Extractor) >= 4) {
      build(Zerg_Ultralisk);
      upgrade(Chitinous_Plating) && upgrade(Anabolic_Synthesis);
    } else if (enemyIsAffirmativelyBio) {
      build(Zerg_Lurker);
    } else {
      build(Zerg_Hydralisk);
      buildN(Zerg_Mutalisk, countPlusProduction(bst, Zerg_Hydralisk) / 3);
    }
    upgrade(Zerg_Flyer_Carapace_2) && upgrade(Zerg_Flyer_Attacks_2);
    upgrade(Pneumatized_Carapace);
    upgrade(Adrenal_Glands);
    if (enemyIsAffirmativelyBio) {
      buildN(Zerg_Zergling, bases * 8);
      buildN(Zerg_Mutalisk, bases * 4);
      buildN(Zerg_Lurker, bases * 4);
      upgrade(Zerg_Carapace_2) && upgrade(Zerg_Melee_Attacks_2) &&
          upgrade(Zerg_Carapace_3) && upgrade(Zerg_Melee_Attacks_3);
    } else if (enemyIsAffirmativelyMech) {
      buildN(Zerg_Mutalisk, bases * 4);
      buildN(Zerg_Hydralisk, bases * 10);
      upgrade(Zerg_Missile_Attacks_2) && upgrade(Zerg_Carapace_2) &&
          upgrade(Zerg_Missile_Attacks_3) && upgrade(Zerg_Carapace_3);
    }
    if (enemyCloakedUnitCount || enemyVultureCount) {
      upgrade(Pneumatized_Carapace);
    }
    if (enemyIsAffirmativelyMech && enemyVultureCount) {
      buildSunkens(bst, bases - 1, vultureSunken, true);
    }
    buildN(Zerg_Zergling, zerglingTarget);
    buildN(Zerg_Hydralisk, hydraTarget);
    if (countPlusProduction(bst, Zerg_Zergling) >= 6) {
      upgrade(Metabolic_Boost);
    }
    if (countPlusProduction(bst, Zerg_Hydralisk) >= 3) {
      upgrade(Grooved_Spines) && upgrade(Muscular_Augments);
    }
    buildN(Zerg_Mutalisk, 2 * enemyTankCount);
    buildN(Zerg_Lurker, lurkerTarget);
    buildN(Zerg_Extractor, std::min(myDroneCount / 9, bases));
    sneakDrones(bst, netGroundStrength > 0 ? 2 : 1);
  }

  void buildOrder(BuildState& bst) {
    const int hydraTarget = (enemyIsAffirmativelyMech ? 3 : 0) +
        enemyVultureCount + enemyWraithCount + 2 * enemyGoliathCount;

    build(Zerg_Zergling);
    buildN(Zerg_Drone, 44);
    expand(bst);
    buildN(Zerg_Drone, 30);
    if (enemyIsAffirmativelyBio) {
      build(Zerg_Lurker);
    }
    buildN(Zerg_Hydralisk_Den, 1);
    if (enemyIsAffirmativelyMech) {
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
    if (enemyIsAffirmativelyBio ||
        countPlusProduction(bst, Zerg_Zergling) >= 6) {
      upgrade(Metabolic_Boost);
    }
    buildN(Zerg_Lair, 1);
    buildN(Zerg_Hydralisk, std::min(hydraTarget, 3));
    buildN(Zerg_Drone, 20);
    buildSunkens(bst, 1);
    buildN(Zerg_Extractor, 1);
    buildN(
        Zerg_Zergling,
        std::max(4, enemyIsAffirmativelyMech ? 0 : 2 * enemyMarineCount));
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
    preferSafeExpansions = !enemyIsAffirmativelyMech;
    bst.autoBuildRefineries = countPlusProduction(bst, Zerg_Drone) >= 26;

    if (tookThirdBase) {
      lateGame(bst);
    } else {
      buildOrder(bst);
    }
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOzvt3hatchlurker, UpcId, State*, Module*);
}
