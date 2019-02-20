/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "base.h"

namespace cherrypi {

using namespace cherrypi::buildtypes;
using namespace cherrypi::autobuild;

class ABBOzvp3hatchhydra : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  // 3 Hatch Hydra
  // https://liquipedia.net/starcraft/3_Hatch_Hydralisk_(vs._Protoss)
  //
  // Idea: Break a fast-expanding Protoss with a timed influx of Hydralisks
  // Timed to hit before the Protoss' two-base production or Templar tech come
  // online. Protoss need to add lots of Photon Cannons or they will die.
  //
  // Against one-base Protoss, just plays a muscular Zergling-Hydralisk-Lurker
  // composition.

  enum Progress { Opening, LateGame };

  static constexpr double kHydraliskRatio = 1.6;

  Progress progress = Progress::Opening;
  bool readyToAttack = false;

  virtual void preBuild2(autobuild::BuildState& bst) override {
    const bool scout = countPlusProduction(bst, Zerg_Overlord) > 1 &&
        enemyStaticDefenceCount == 0 && enemyZealotCount < 2;

    // Attack with initial Zerglings to get an update on their Cannon count.
    // Time the bust to land when we have enough Hydralisks to break them.
    readyToAttack = readyToAttack ||
        (hasOrInProduction(bst, Grooved_Spines) &&
         myHydraliskCount > 4 + 3 * enemyStaticDefenceCount +
                 1 * enemyZealotCount + 2 * enemyDragoonCount);

    if (progress == Progress::Opening) {
      // If they have Dark Templar, Reavers, or too many cannons, we can't bust
      // them
      // End the bust once they get a substantial army
      if (has(bst, Metabolic_Boost) || enemyReaverCount ||
          enemyDarkTemplarCount || enemyStaticDefenceCount > 3 ||
          enemyZealotCount + enemyDragoonCount >
              std::max(6, myHydraliskCount / 2)) {
        progress = Progress::LateGame;
      }
    }

    // Scout on Overlord, to ensure we detect one/two base play in time to make
    // the correct decision about where to place our third Hatchery.
    postBlackboardKey(Blackboard::kMinScoutFrameKey, scout ? 1 : 0);

    // Stay at home while developing the Hydralisk bust, so we avoid bleeding
    // units (and hide the bust from our opponents)
    postBlackboardKey(
        "TacticsAttack",
        myHydraliskCount == 0 || readyToAttack ||
            progress == Progress::LateGame);
  }

  void doLateGame(BuildState& bst) {
    const int zealots = 4 + enemyZealotCount;
    const int dragoons = 4 + enemyDragoonCount;
    const int zerglingGoal = utils::safeClamp(2 * (dragoons - zealots), 6, 24);
    const int hydraliskGoal = int(kHydraliskRatio * (zealots + dragoons)) -
        countPlusProduction(bst, Zerg_Zergling) / 3;
    build(Zerg_Zergling);
    build(Zerg_Hydralisk);
    takeNBases(bst, countPlusProduction(bst, Zerg_Drone) / 14);
    buildN(Zerg_Drone, std::min(60, bases * 15));
    buildN(Zerg_Hatchery, myDroneCount / 5);
    buildN(Zerg_Hydralisk, hydraliskGoal);
    buildN(Zerg_Lurker, enemyZealotCount / 3);
    // if (framesBefore(bst, Metabolic_Boost) <= 24 + Zerg_Zergling->buildTime)
    // {
    if (has(bst, Metabolic_Boost)) {
      buildN(Zerg_Zergling, zerglingGoal);
    }

    if (countPlusProduction(bst, Zerg_Hatchery) >= 5 &&
        countPlusProduction(bst, Zerg_Drone) >= 30) {
      upgrade(Zerg_Missile_Attacks_1) && upgrade(Zerg_Missile_Attacks_2) &&
          upgrade(Zerg_Carapace_1) && upgrade(Zerg_Carapace_2) &&
          upgrade(Zerg_Missile_Attacks_3) && upgrade(Zerg_Carapace_3);
    }
    if (enemyZealotCount > 2) {
      upgrade(Lurker_Aspect);
    }
    if (zerglingGoal > 4) {
      if (has(bst, Zerg_Hive)) {
        upgrade(Adrenal_Glands);
      }
      upgrade(Metabolic_Boost);
    }
    if (enemyDarkTemplarCount || enemyCorsairCount ||
        (enemyObserverCount && myLurkerCount)) {
      upgrade(Pneumatized_Carapace);
    }
    if (hydraliskGoal > 0) {
      upgrade(Muscular_Augments) && upgrade(Grooved_Spines);
    }
    takeNBases(bst, myDroneCount / 12);
    buildN(
        Zerg_Sunken_Colony,
        countPlusProduction(bst, Zerg_Sunken_Colony) +
            countUnits(bst, Zerg_Creep_Colony));
  }

  static constexpr int kBustDrones = 20;
  void doOpening(BuildState& bst) {
    const double enemyProximity = 0.8;
    const int extraCannons =
        utils::safeClamp(enemyStaticDefenceCount - 2, 0, 2);
    const int hatcheryTarget = 3 + extraCannons;

    build(Zerg_Hydralisk);
    upgrade(Muscular_Augments) && upgrade(Grooved_Spines);
    if (hasOrInProduction(bst, Zerg_Hydralisk_Den)) {
      buildN(Zerg_Extractor, 2);
    }
    buildN(Zerg_Hydralisk_Den, 1);
    if (readyToAttack || countPlusProduction(bst, Zerg_Zergling) >= 8) {
      upgrade(Metabolic_Boost);
    }
    buildN(Zerg_Extractor, 1);

    if (!hasOrInProduction(bst, Zerg_Hydralisk_Den)) {
      buildN(Zerg_Hatchery, hatcheryTarget);
    }
    buildN(
        Zerg_Drone,
        std::max(18, 2 + 6 * countPlusProduction(bst, Zerg_Hatchery)));
    if (!has(bst, Zerg_Hydralisk_Den)) {
      // Build just enough units to survive
      buildN(
          Zerg_Zergling,
          utils::safeClamp(
              2 + int(6 * enemyZealotCount * enemyProximity) -
                  4 * mySunkenCount,
              3,
              16));
      // Only build Sunkens vs. one-base Protoss
      if (!enemyStaticDefenceCount && !enemyForgeCount) {
        buildSunkens(
            bst,
            utils::safeClamp(
                int(
                    (enemyZealotCount + enemyDragoonCount * enemyProximity -
                     myZerglingCount / 4.0)),
                enemyGatewayCount,
                4),
            {},
            true);
      }
    }
    // Take a third base if they're opening FE
    // Build the third Hatchery at home otherwise
    if (enemyStaticDefenceCount || enemyForgeCount ||
        (!enemyGatewayCount && !enemyZealotCount)) {
      takeNBases(bst, 3);
    } else {
      buildN(Zerg_Hatchery, 3);
    }
    buildN(Zerg_Drone, 14);
    buildN(Zerg_Spawning_Pool, 1);
    takeNBases(bst, 2);
    if (countPlusProduction(bst, Zerg_Hatchery) < 2) {
      buildN(Zerg_Drone, 12);
    }
    buildN(Zerg_Overlord, 1);
    buildN(Zerg_Drone, 9);
  }

  virtual void buildStep2(BuildState& bst) override {
    preferSafeExpansions = false;
    autoExpand = progress == Progress::LateGame;
    autoUpgrade = progress == Progress::LateGame && geysers > 3;
    bst.autoBuildRefineries = countPlusProduction(bst, Zerg_Drone) >= 30;

    if (progress == Progress::Opening) {
      doOpening(bst);
    } else {
      doLateGame(bst);
    }
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOzvp3hatchhydra, UpcId, State*, Module*);
} // namespace cherrypi
