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

class ABBOzvzoverpool : public ABBOBase {
  using ABBOBase::ABBOBase;

 public:
  // Goals:
  // Play the most economic Zerg build that is 100% safe against everything.
  // * Attempt to kill anyone playing a greedier build.
  // * Outproduce anyone playing a more aggressive build.
  //
  // Overpool should be equal or favored against most builds. 12 Pool is a
  // notable exception, but is uncommon in bot land because most bots lack the
  // defensive skills (drone drilling and ramp blocking) to support it.
  //
  // Vs. 9 Pool (or earlier):
  // They will likely get Metabolic Boost before we do and can match our
  // Zergling count for a while. Thus, it's not safe to be on the map early.
  // The big advantage is our ability to afford a second Hatchery, and can
  // thus edge ahead while Mutalisks are on the field for both sides.
  //
  // Vs. 12 Pool/12 Hatch:
  // We'll have earlier Mutalisks. 12 Pool is favored but the game will go
  // long, which is good because it reduces variance. 12 Hatch likely dies
  // to our initial zerglings, but in the worst case the game goes long.

  bool completedMutalisks = false;
  bool completedBuildOrder = false;
  bool completedSpire = false;
  bool enemyHasAir = false;
  int netGroundStrength = 0;
  int netGroundStrengthInside = 0;
  int netGroundStrengthOutside = 0;
  int netAirStrength = 0;
  int netAirStrengthInside = 0;
  int netAirStrengthOutside = 0;
  int netStrengthInside = 0;
  Position sunkenPosition;
  Position sporePosition;
  virtual void preBuild2(autobuild::BuildState& bst) override {
    const int netZerglings = 1 * (myZerglingCount - enemyZerglingCount);
    const int netHydralisks = 2 * (myHydraliskCount - enemyHydraliskCount);
    const int netMutalisks = 2 * (myMutaliskCount - enemyMutaliskCount);
    const int netScourge = 1 * (myScourgeCount - enemyScourgeCount);
    netAirStrength = netAirStrengthInside = netAirStrengthOutside =
        netMutalisks + netScourge;
    auto lst = std::vector<decltype(netAirStrength)>(
        {0, netMutalisks, netAirStrength});
    std::sort(lst.begin(), lst.end());
    netGroundStrength = netGroundStrengthInside = netGroundStrengthOutside =
        netZerglings + netHydralisks + lst[1];
    netGroundStrengthInside += 3 * mySunkenCount + 2 - 4 * enemyProximity;
    netGroundStrengthOutside -= 3 * mySunkenCount;
    netAirStrengthInside += 6 * mySporeCount;
    netAirStrengthOutside -= 6 * enemySporeCount;
    netStrengthInside = std::min(netGroundStrengthInside, netAirStrengthInside);

    completedBuildOrder = completedBuildOrder ||
        state_->unitsInfo().myCompletedUnitsOfType(Zerg_Spawning_Pool).size();
    completedSpire = completedSpire ||
        state_->unitsInfo().myCompletedUnitsOfType(Zerg_Spire).size();
    completedMutalisks = completedMutalisks || myMutaliskCount;
    enemyHasAir = enemyHasAir || enemyMutaliskCount || enemyScourgeCount ||
        enemySpireCount;

    sunkenPosition = findSunkenPos(Zerg_Sunken_Colony, bases < 2, true);
    sporePosition = findSunkenPos(Zerg_Spore_Colony, bases < 2, true);

    // Attack if we have Mutalisks
    // If we're not likely to get backstabbed by Zerglings we don't see:
    // * Attack if we have Zergling Speed
    // * Attack if the enemy has two bases and we are not outnumbered
    // * Attack if we haven't found the enemy yet
    // The backdoor restriction renders us weak to 12 Hatch on 4-Player maps
    // but greatly reduces the chance we get backstabbed by 5-9 Pools.
    const bool backdoorLikely = enemyBuildingCount == 0 &&
        state_->tcstate()->start_locations.size() >= 4;
    const bool shouldAttack = myMutaliskCount ||
        (!backdoorLikely &&
         (state_->getUpgradeLevel(Metabolic_Boost) ||
          (enemyHasExpanded && netGroundStrength >= 0) ||
          (enemyBuildingCount == 0 && enemyZerglingCount == 0)));
    postBlackboardKey("TacticsAttack", shouldAttack);
    postBlackboardKey(Blackboard::kMinScoutFrameKey, 0);
  }

  void addSecondHatchery(BuildState& bst) {
    if (countPlusProduction(bst, Zerg_Hatchery) > 1) {
      return;
    }
    // Expand if we can; take a macro hatch at home if we can't
    //
    // If we're weak on the ground or opponent has 2 Hatcheries of Zerglings,
    // we need to stay in our base for Sunken coverage.
    if (netGroundStrength < 2) {
      buildN(Zerg_Hatchery, 2);
    } else {
      expand(bst);
    }
  }

  void addEmergencySpores(BuildState& bst) {
    // This should be pretty rare -- mostly only if we lose our Spire
    if (netAirStrengthInside < 0 && enemyMutaliskCount &&
        !hasOrInProduction(bst, Zerg_Spire)) {
      buildSpores(bst, std::min(bases, myDroneCount / 6), sporePosition);
    }
  }

  void addEmergencySunkens(BuildState& bst) {
    int droneCount = countPlusProduction(bst, Zerg_Drone);
    int threshold = 0;
    if (enemyProximity > 0.55) {
      threshold += 2;
    }
    if (hasOrInProduction(bst, Zerg_Lair)) {
      threshold += 2;
    }
    if (hasOrInProduction(bst, Zerg_Spire)) {
      threshold += 1;
    }
    if (myMutaliskCount > 0) {
      threshold -= 2;
    }
    if (droneCount < 9) {
      threshold -= 2;
    }
    if (droneCount > 11) {
      threshold += 2;
    }
    if (netGroundStrengthInside < threshold) {
      buildSunkens(bst, std::min(bases, myDroneCount / 4), sunkenPosition);
    }
  }

  void lateGame(BuildState& bst) {
    bool scaredVsGround = netGroundStrengthInside < 0;
    bool scaredVsAir = enemyMutaliskCount && netAirStrengthInside < 2;
    bool enemyTurtlingVsGround =
        enemySunkenCount && netGroundStrengthOutside < 0;
    bool enemyTurtlingVsAir = enemySporeCount && netAirStrengthOutside < 0;

    if (countPlusProduction(bst, Zerg_Drone) >= bases * 12) {
      expand(bst);
    }

    build(Zerg_Zergling);
    build(Zerg_Mutalisk);
    buildN(Zerg_Drone, bases * 13);

    addSecondHatchery(bst);
    addEmergencySpores(bst);
    addEmergencySunkens(bst);

    // Prioritize units if:
    // * We need them for defense
    // * We can apply pressure
    if (scaredVsGround || !enemyTurtlingVsGround) {
      build(Zerg_Zergling);
    }
    if (scaredVsAir || scaredVsGround || !enemyTurtlingVsAir) {
      build(Zerg_Mutalisk);
    }

    if (netStrengthInside > 0) {
      buildN(Zerg_Drone, 18, 1);
    }

    if (countPlusProduction(bst, Zerg_Mutalisk) >= 6) {
      upgrade(Zerg_Flyer_Carapace_1) && upgrade(Zerg_Flyer_Attacks_1) &&
          upgrade(Zerg_Flyer_Carapace_2) && upgrade(Zerg_Flyer_Attacks_2);
    }
    buildN(Zerg_Extractor, std::min(geysers, bst.workers / 7));
    buildN(Zerg_Drone, 10);
    buildN(Zerg_Scourge, enemyMutaliskCount * 2 + (enemyMutaliskCount ? 4 : 0));
    buildN(Zerg_Mutalisk, 5);
    buildN(Zerg_Drone, 8);
  }

  void doBuildOrder(BuildState& bst) {
    build(Zerg_Mutalisk);
    upgrade(Metabolic_Boost);

    // Hacky. Needs access to actual larva counts
    // Make Zerglings/Drones while banking Larva for Mutalisks.
    if (framesUntil(bst, Zerg_Spire) >
        3 * kLarvaFrames - countUnits(bst, Zerg_Larva)) {
      buildN(Zerg_Drone, 10) &&
          buildN(Zerg_Zergling, std::max(8, enemyZerglingCount + 4)) &&
          buildN(Zerg_Drone, 18);
    }

    buildN(Zerg_Spire, 1);
    addEmergencySunkens(bst);
    buildN(Zerg_Lair, 1);
    buildN(Zerg_Zergling, 6);
    if (!has(bst, Zerg_Spawning_Pool)) {
      buildN(Zerg_Drone, 10);
    }
    buildN(Zerg_Extractor, 1);
    buildN(Zerg_Spawning_Pool, 1);
    buildN(Zerg_Overlord, 2);
    buildN(Zerg_Drone, 9);
  }

  virtual void buildStep2(BuildState& bst) override {
    autoUpgrade = countUnits(bst, Zerg_Extractor) > 2;
    autoExpand = bst.frame > 24 * 60 * 8;
    buildExtraOverlordsIfLosingThem = false;
    bst.autoBuildRefineries = false;

    if (completedSpire) {
      lateGame(bst);
    } else {
      doBuildOrder(bst);
    }
    if (!hasOrInProduction(bst, Zerg_Evolution_Chamber)) {
      morphSunkens(bst);
    }
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOzvzoverpool, UpcId, State*, Module*);
} // namespace cherrypi
