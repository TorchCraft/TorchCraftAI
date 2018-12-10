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
/*
Our goal early on is to build just enough army to survive, while spending the
rest of our resources on Drones and tech.
In a world where we have map hack and units build instantly, we'd want 1.0 + ϵ
slope and 0.0 offset.
So any deviations from that are simply accommodating our sadly non-omnipotent
capabilities.
*/
DEFINE_double(
    zvpoverpool_distance_fear,
    0.6,
    "How heavily to weigh enemy army units that are far from our base. "
    "Reasonably ranges on [0.0, 1.0]");
DEFINE_double(
    zvpoverpool_distance_fear_limit,
    6.0,
    "Maximum amount by which to diminish enemy army size due to distance");
DEFINE_double(
    zvpoverpool_army_slope,
    1.1,
    "How many times larger to make our army with respect to the enemy army's "
    "threat level.");
DEFINE_double(
    zvpoverpool_army_offset,
    1.0,
    "How many zealots-worth larger to make our ground army with respect to the "
    "enemy army's threat level.");
DEFINE_double(
    zvpoverpool_hidden_threat_multiplier,
    0.9,
    "Multiplier of hidden enemy army strength to consider.");
DEFINE_double(
    zvpoverpool_hidden_threat_cap,
    20.0,
    "Maximum amount of hidden enemy army strength to infer.");

class FogParameters {
 public:
  const double distantEnemyFear = FLAGS_zvpoverpool_distance_fear;
  const double distantEnemyFearLimit = FLAGS_zvpoverpool_distance_fear_limit;
  const double enemyOffset = FLAGS_zvpoverpool_army_offset;
  const double enemySlope = FLAGS_zvpoverpool_army_slope;
  const double enemyHiddenThreatMax = FLAGS_zvpoverpool_hidden_threat_cap;
};

class CombatParameters {
  // Parameters that adjust the relative strength we assign units.
 public:
  const double threatZealot = 1.0;
  const double threatDragoon = 1.0;
  const double threatDarkTemplar = 2.0;
  const double threatHighTemplar = 2.0;
  const double threatArchon = 3.0;
  const double threatReaver = 3.0;

  const double strengthZergling = 0.25;
  const double strengthHydralisk = 0.9;
  const double strengthMutalisk = 1.1;
  const double strengthSunkenColony = 2.0;

  const double threatCorsairScaling = 1.06;
  const double strengthZerglingScaling = 0.985;
};

class MacroParameters {
 public:
  const int maximumSunkenCount = 6;
  const int corsairThresholdToCedeAir = 1;
};

using namespace cherrypi::buildtypes;
using namespace cherrypi::autobuild;

class ABBOzvpoverpoolbase : public ABBOBase {
 protected:
  virtual bool preferHydras() const = 0;

 public:
  using ABBOBase::ABBOBase;

  // zvpoverpool: A build order for Zerg vs. Protoss only.
  //
  // Strategy:
  // * Open Overpool
  // * Vs. one base: 3 Hatch Ling into 3 Hatch Mutalisks
  // * Vs. two base: 3 Hatch Spire; Mutalisk+Zerglings if they lack Corsairs,
  // and 5 Hatch Hydralisks otherwise
  //
  // Things this should be strong against:
  // * Hatchery blocking
  // * Gateway-first FE (since we have early Zerglings anyway)
  // * Any sort of greedy builds like Nexus-first or Forge FE skipping the
  // second cannon
  // * Proxy Gateways
  //
  // Related:
  // https://liquipedia.net/starcraft/3_Hatch_Spire_(vs._Protoss)
  // https://liquipedia.net/starcraft/3_Base_Spire_into_5_Hatch_Hydra_(vs._Protoss)

  ////////////////
  // Parameters //
  ////////////////

  FogParameters fogParameters;
  CombatParameters combatParameters;
  MacroParameters macroParameters;

  ///////////
  // State //
  ///////////

  bool trainedInitialZerglings = false;
  bool completedThreeBases = false;
  bool completedNatural = false;

  //////////////
  // Scouting //
  //////////////

  bool enemyGoingGround = false;
  bool enemyExpanded = false;
  double enemyProximity = 0.0;
  double enemyGroundThreatEstimated = 0.0;
  double enemyGroundThreatMeasured = 0.0;
  double enemyGroundThreatOutside = 0.0;
  double enemyGroundThreatAtHome = 0.0;
  double friendlyGroundThreatOutside = 0.0;
  double friendlyGroundThreatAtHome = 0.0;
  double netGroundThreatOutside = 0.0;
  double netGroundThreatAtHome = 0.0;
  int sunkensRequired = 0;
  bool weAreSafeOutside = false;
  bool weAreSafeAtHome = false;

  /////////////////
  // Diagnostics //
  /////////////////

  int lastStatusUpdate = 0;
  time_t timeSecondsStart = 0;
  time_t timeSecondsNow = 0;

  ///////////////
  // Pre-build //
  ///////////////

  /// Estimate how big an army the enemy could have based on hypothetical
  /// production
  double estimateEnemyGroundThreat() {
    // "Average" length of Gateway unit build time.
    // Somewhere between Zealot (25s) and Dragoon (32s) skewed towards Zealot.
    constexpr int gatewayUnitBuildFrames = 24 * 27;

    // Taken from a replay of Locutus, who ramps up Gateways fairly
    // aggressively.
    // Other possible benchmarks:
    // * Wulibot's gasless one-base 5-Gate
    // * tscmoo's gasless two-base 5-Gate
    // * PurpleWave's one-base 4-Gate
    // * PurpleWave's two-base 8-Gate
    std::vector<double> gatewayCompletionFrames = {
        24 * 60 * 4,
        24 * 60 * 5,
        24 * 60 * 6.5,
        24 * 60 * 6.9,
        24 * 60 * 7.15,
        24 * 60 * 8.75,
        24 * 60 * 9,
    };

    double expectedGroundThreat = 0.0;
    for (auto gatewayCompletionFrame : gatewayCompletionFrames) {
      double expectedProductionFrames =
          std::max(0.0, state_->currentFrame() - gatewayCompletionFrame);
      double expectedUnits = expectedProductionFrames / gatewayUnitBuildFrames;
      expectedGroundThreat += combatParameters.threatZealot * expectedUnits;
    }

    int deadGroundThreat = 0;
    for (auto* unit : state_->unitsInfo().enemyUnits()) {
      if (unit->dead) {
        if (unit->type == Protoss_Zealot) {
          deadGroundThreat += combatParameters.threatZealot;
        } else if (unit->type == Protoss_Dragoon) {
          deadGroundThreat += combatParameters.threatDragoon;
        } else if (unit->type == Protoss_Corsair) {
          // That's a lot of money that's not ground units!
          deadGroundThreat += 2 * combatParameters.threatDragoon;
        } else if (unit->type == Protoss_Dark_Templar) {
          deadGroundThreat += combatParameters.threatDarkTemplar;
        } else if (unit->type == Protoss_High_Templar) {
          deadGroundThreat += combatParameters.threatHighTemplar;
        } else if (unit->type == Protoss_Archon) {
          deadGroundThreat += combatParameters.threatArchon;
        } else if (unit->type == Protoss_Reaver) {
          deadGroundThreat += combatParameters.threatReaver;
        }
      }
    }
    deadGroundThreat += 2 * combatParameters.threatDragoon * enemyCorsairCount;

    return std::max(
        0.0,
        std::min(
            fogParameters.enemyHiddenThreatMax,
            expectedGroundThreat * FLAGS_zvpoverpool_hidden_threat_multiplier -
                deadGroundThreat));
  }

  double measureEnemyGroundThreat() {
    return fogParameters.enemyOffset +
        fogParameters.enemySlope *
        (combatParameters.threatZealot * enemyZealotCount +
         combatParameters.threatDragoon * enemyDragoonCount +
         combatParameters.threatHighTemplar * enemyHighTemplarCount +
         combatParameters.threatDarkTemplar * enemyDarkTemplarCount +
         combatParameters.threatArchon * enemyArchonCount +
         combatParameters.threatReaver * enemyReaverCount);
  }

  void measureEnemyThreat() {
    enemyGroundThreatEstimated = estimateEnemyGroundThreat();
    enemyGroundThreatMeasured = measureEnemyGroundThreat();
    auto enemyProximityConcern = fogParameters.distantEnemyFear +
        (1.0 - fogParameters.distantEnemyFear) * enemyProximity;
    enemyGroundThreatOutside =
        std::max(enemyGroundThreatEstimated, enemyGroundThreatMeasured);
    enemyGroundThreatAtHome = std::max(
        std::max(
            enemyGroundThreatEstimated,
            enemyGroundThreatOutside * enemyProximityConcern),
        enemyGroundThreatOutside - fogParameters.distantEnemyFearLimit);
    friendlyGroundThreatOutside = combatParameters.strengthZergling *
            myZerglingCount *
            std::max(0.7,
                     pow(combatParameters.strengthZerglingScaling,
                         myZerglingCount)) +
        combatParameters.strengthHydralisk * myHydraliskCount +
        combatParameters.strengthMutalisk * myMutaliskCount;
    friendlyGroundThreatAtHome = friendlyGroundThreatOutside +
        combatParameters.strengthSunkenColony * mySunkenCount;
    netGroundThreatOutside =
        enemyGroundThreatOutside - friendlyGroundThreatOutside;
    netGroundThreatAtHome =
        enemyGroundThreatAtHome - friendlyGroundThreatAtHome;
    sunkensRequired = utils::safeClamp(
        int(0.75 +
            netGroundThreatAtHome / combatParameters.strengthSunkenColony),
        std::min(1, int(enemyGroundThreatAtHome)),
        macroParameters.maximumSunkenCount);
    weAreSafeAtHome = netGroundThreatAtHome <= 0;
    weAreSafeOutside = netGroundThreatOutside <= 0;
  }

  void detectEnemyBuild() {
    enemyGoingGround = enemyGroundArmySupply > 0;
    enemyExpanded =
        enemyHasExpanded || enemyForgeCount || enemyStaticDefenceCount;
  }

  const char* debool(bool value) {
    return value ? "True" : "False";
  }

  virtual void preBuild2(autobuild::BuildState& bst) override {
    time(&timeSecondsNow);
    if (timeSecondsStart == 0) {
      timeSecondsStart = timeSecondsNow;
    }

    // Scout on Pool so we can determine quickly enough whether to take a third
    // base
    // or build our third Hatchery at home.
    postBlackboardKey(
        Blackboard::kMinScoutFrameKey,
        (state_->unitsInfo().myUnitsOfType(Zerg_Spawning_Pool).empty() ||
         enemyExpanded || enemyForgeCount || enemyStaticDefenceCount ||
         enemyZealotCount || enemyDragoonCount)
            ? 0
            : 24 * 90);

    auto shouldAttack = bases >= 3 || enemyProximity < 0.8 ||
        myMutaliskCount > 0 || weAreSafeAtHome || weArePlanningExpansion ||
        armySupply > 2 * 100;
    postBlackboardKey("TacticsAttack", shouldAttack);

    trainedInitialZerglings = trainedInitialZerglings || myZerglingCount >= 6;
    completedNatural = completedNatural || bases >= 2;
    completedThreeBases = completedThreeBases || bases >= 3;

    measureEnemyThreat();
    detectEnemyBuild();

    if (bst.frame > lastStatusUpdate + 24 * 10) {
      lastStatusUpdate = bst.frame;
      VLOG(1) << "Enemy proximity:              " << int(100 * enemyProximity)
              << "%";
      VLOG(1) << "";
      VLOG(1) << "Enemy ground threat estimate: " << enemyGroundThreatEstimated;
      VLOG(1) << "Enemy ground threat measure:  " << enemyGroundThreatMeasured;
      VLOG(1) << "Enemy ground threat @home:    " << enemyGroundThreatAtHome;
      VLOG(1) << "Enemy ground threat outside:  " << enemyGroundThreatOutside;
      VLOG(1) << "Friendly strength @home:      " << friendlyGroundThreatAtHome;
      VLOG(1) << "Friendly strength outside:    "
              << friendlyGroundThreatOutside;
      VLOG(1) << "Net ground threat @home:      " << netGroundThreatAtHome;
      VLOG(1) << "Net ground threat outside:    " << netGroundThreatOutside;
      VLOG(1) << "Sunkens required:             " << sunkensRequired;
      VLOG(1) << "";
      VLOG(1) << "Are we safe at home?          " << debool(weAreSafeAtHome);
      VLOG(1) << "Are we safe outside?          " << debool(weAreSafeOutside);
      VLOG(1) << "";
      VLOG(1) << "Enemy Workers:                " << enemyWorkerCount;
      VLOG(1) << "Enemy Zealots:                " << enemyZealotCount;
      VLOG(1) << "Enemy Dragoons:               " << enemyDragoonCount;
      VLOG(1) << "Enemy Dark Templar:           " << enemyDarkTemplarCount;
      VLOG(1) << "Enemy High Templar:           " << enemyHighTemplarCount;
      VLOG(1) << "Enemy Archons:                " << enemyArchonCount;
      VLOG(1) << "Enemy Reavers:                " << enemyReaverCount;
      VLOG(1) << "Enemy Corsairs:               " << enemyCorsairCount;
      VLOG(1) << "Enemy Scouts:                 " << enemyScoutCount;
      VLOG(1) << "Our Drones:                   " << myDroneCount;
      VLOG(1) << "Our Zerglings:                " << myZerglingCount;
      VLOG(1) << "Our Hydralisks:               " << myHydraliskCount;
      VLOG(1) << "Our Mutalisks:                " << myMutaliskCount;
      VLOG(1) << "Our Sunkens:                  " << mySunkenCount;
      VLOG(1) << "";
      VLOG(1) << "Enemy going ground?           " << debool(enemyGoingGround);
      VLOG(1) << "Enemy expanded?               " << debool(enemyExpanded);
      VLOG(1) << "";
      VLOG(1) << "Completed initial zerglings:  "
              << debool(trainedInitialZerglings);
      VLOG(1) << "Completed three bases:        "
              << debool(completedThreeBases);
      VLOG(1) << "-------------------------------------";
    }
  }

  ////////////////
  // Build step //
  ////////////////

  bool shouldGoAir(autobuild::BuildState& bst) {
    if (preferHydras()) {
      return false;
    }

    auto mutalisks = countPlusProduction(bst, Zerg_Mutalisk);
    if (mutalisks < fogParameters.enemySlope * enemyCorsairCount *
                pow(combatParameters.threatCorsairScaling, enemyCorsairCount) -
            macroParameters.corsairThresholdToCedeAir) {
      // The opponent has an insurmountable number of Corsairs
      return false;
    }

    if (enemyDragoonCount - mutalisks > 12) {
      // Hydra + Zergling is a more efficient composition
      return false;
    }

    return true;
  }

  void goHatcheries(autobuild::BuildState& bst) {
    auto hatcheriesNow = countPlusProduction(bst, Zerg_Hatchery);
    auto hatcheriesMax = bst.minerals / 600 + bst.workers / 7.0;
    auto hatcheriesToAdd = hatcheriesMax - hatcheriesNow;
    if (hatcheriesToAdd > 0) {
      build(Zerg_Hatchery);
      if (weAreSafeOutside ||
          countPlusProduction(bst, Zerg_Hatchery) >= bases * 2) {
        expand(bst);
      }
    }
  }

  void goDrones(autobuild::BuildState& bst) {
    if (countPlusProduction(bst, Zerg_Drone) < 40 ||
        countProduction(bst, Zerg_Drone) < 2) {
      buildN(
          Zerg_Drone,
          std::min(
              75,
              6 + 2 * mineralFields +
                  3 * countPlusProduction(bst, Zerg_Extractor)));
    }
  }

  void buildDenInMain() {
    buildN(Zerg_Hydralisk_Den, 1, homePosition);
  }

  void buildSpireInMain() {
    buildN(Zerg_Spire, 1, homePosition);
  }

  void goArmy(autobuild::BuildState& bst) {
    build(Zerg_Zergling);
    if (enemyGoingGround && myCompletedHatchCount < 4 && bases < 3) {
      buildSunkens(bst, sunkensRequired);
    }
    auto excessDragoons = enemyDragoonCount - enemyZealotCount;
    if (shouldGoAir(bst) || !has(bst, Zerg_Hydralisk_Den)) {
      // Mutalisk composition
      buildN(Zerg_Zergling, 12);
      build(Zerg_Mutalisk);
      buildN(
          Zerg_Zergling,
          std::min(
              6 + 2 * countPlusProduction(bst, Zerg_Mutalisk),
              3 * excessDragoons));
    } else {
      // Hydralisk composition
      build(Zerg_Hydralisk);
      if (has(bst, Adrenal_Glands)) {
        buildN(Zerg_Zergling, 6 + 2 * countPlusProduction(bst, Zerg_Hydralisk));
      }

      // Get Lurkers against lots of Zealots; punish lack of detection
      const int lurkerGoal = std::max(
          enemyZealotCount / 6 - 2,
          (enemyZealotCount -
           enemyDragoonCount * std::min(2, enemyObserverCount)) /
              4);
      if (lurkerGoal > 0 || hasOrInProduction(bst, Lurker_Aspect)) {
        buildN(Zerg_Lurker, lurkerGoal);
      }
      if (enemyGoingGround) {
        buildN(
            Zerg_Zergling,
            std::min(
                6 + countPlusProduction(bst, Zerg_Hydralisk),
                3 * excessDragoons));
      }

      // Make sure we can answer Reavers/Shuttles
      if (enemyCorsairCount < 4 && (enemyReaverCount + enemyShuttleCount) > 0) {
        buildN(Zerg_Mutalisk, 6);
      }

      // Tactics relies on Zerglings for scouting
      buildN(Zerg_Zergling, 4);

      // Get enough Hydralisks to fend off flyers
      buildN(
          Zerg_Hydralisk,
          std::max(5, 2 * (enemyCorsairCount + enemyScoutCount)));
      buildDenInMain();
    }

    // TODO: Build Scourge here once our micro is better
    // buildN(Zerg_Scourge, 2 * enemyCorsairCount + (enemyCorsairCount > 0 ? 2 :
    // 0));

    if (enemyGoingGround && hasUnit(bst, Zerg_Creep_Colony)) {
      build(Zerg_Sunken_Colony);
    }
  }

  bool upgradeFlyerAttackAndCarapace() {
    return upgrade(Zerg_Flyer_Carapace_1) && upgrade(Zerg_Flyer_Carapace_2) &&
        upgrade(Zerg_Flyer_Attacks_1) && upgrade(Zerg_Flyer_Attacks_2) &&
        upgrade(Zerg_Flyer_Carapace_3) && upgrade(Zerg_Flyer_Attacks_3);
  }

  bool upgradeMissileAttacks(autobuild::BuildState& bst) {
    auto onHive = hasOrInProduction(bst, Zerg_Hive);
    auto onLair = onHive || hasOrInProduction(bst, Zerg_Lair);
    return upgrade(Zerg_Missile_Attacks_1) &&
        (onLair && upgrade(Zerg_Missile_Attacks_2)) &&
        (onHive && upgrade(Zerg_Missile_Attacks_3));
  }

  bool upgradeMeleeAttacks(autobuild::BuildState& bst) {
    auto onHive = hasOrInProduction(bst, Zerg_Hive);
    auto onLair = onHive || hasOrInProduction(bst, Zerg_Lair);
    return upgrade(Zerg_Missile_Attacks_1) &&
        (onLair && upgrade(Zerg_Missile_Attacks_2)) &&
        (onHive && upgrade(Zerg_Missile_Attacks_3));
  }

  bool upgradeCarapace(autobuild::BuildState& bst) {
    auto onHive = hasOrInProduction(bst, Zerg_Hive);
    auto onLair = onHive || hasOrInProduction(bst, Zerg_Lair);
    return upgrade(Zerg_Carapace_1) && (onLair && upgrade(Zerg_Carapace_2)) &&
        (onHive && upgrade(Zerg_Carapace_3));
  }

  void goUpgrades(
      autobuild::BuildState& bst,
      int thresholdZerglings,
      int thresholdHydralisks,
      int thresholdMutalisks) {
    auto onHive = hasOrInProduction(bst, Zerg_Hive);
    auto onDen = hasOrInProduction(bst, Zerg_Hydralisk_Den);

    auto readyToUpgrade = bases >= 3 && bst.workers >= 30;
    auto upgradeAir =
        countPlusProduction(bst, Zerg_Mutalisk) >= thresholdMutalisks;
    auto upgradeMissile =
        countPlusProduction(bst, Zerg_Hydralisk) >= thresholdHydralisks;
    auto upgradeMelee =
        countPlusProduction(bst, Zerg_Zergling) >= thresholdZerglings;

    if (readyToUpgrade) {
      if (bst.workers > 40) {
        buildN(Zerg_Hive, 1);
      }

      auto evolutionChambersRequired = 0;
      upgradeAir&& upgradeFlyerAttackAndCarapace();
      if (upgradeMelee && !upgradeMeleeAttacks(bst)) {
        ++evolutionChambersRequired;
      }
      if (upgradeMelee || upgradeMissile) {
        if (!upgradeCarapace(bst)) {
          ++evolutionChambersRequired;
        }
      }
      if (upgradeMissile && !upgradeMissileAttacks(bst)) {
        ++evolutionChambersRequired;
      }

      evolutionChambersRequired = std::min(
          evolutionChambersRequired,
          (weAreSafeAtHome || bst.workers >= 40) ? 2 : 1);
      buildN(Zerg_Evolution_Chamber, evolutionChambersRequired);
      upgrade(Pneumatized_Carapace);
      buildN(Zerg_Lair, 1);
    }
    if (onHive) {
      upgrade(Adrenal_Glands);
    }
    if (enemyDarkTemplarCount) {
      upgrade(Pneumatized_Carapace);
    }
    if (onDen) {
      upgrade(Muscular_Augments) && upgrade(Grooved_Spines);
    }
    if (enemyGoingGround) {
      upgrade(Metabolic_Boost);
    }
  }

  void goSneakDrones(autobuild::BuildState& bst) {
    if (currentFrame > 24 * 60 * 6) {
      if (countProduction(bst, Zerg_Drone) < 1) {
        goDrones(bst);
      }
    }
  }

  void respondToCatastrophe(autobuild::BuildState& bst) {
    if (bst.workers < 6) {
      if (weAreSafeAtHome) {
        build(Zerg_Drone);
      } else {
        build(Zerg_Zergling);
      }
    }
  }

  void respondToCorsairs(autobuild::BuildState& bst) {
    int toBuild = std::max(
        3 * enemyStargateCount, enemyCorsairCount + enemyStargateCount);
    if (toBuild > 0) {
      if (shouldGoAir(bst) && hasOrInProduction(bst, Zerg_Spire)) {
        buildN(Zerg_Mutalisk, toBuild);
        // TODO: Build Scourge once our Scourge micro is better
      } else {
        buildN(Zerg_Hydralisk, toBuild);
      }
    }
  }

  void respondToMiningOut(autobuild::BuildState& bst) {
    if (mineralFields < 14 && countPlusProduction(bst, Zerg_Hatchery) > 2) {
      expand(bst);
    } else if (mineralFields < 7) {
      expand(bst);
    }
  }

  void goDevelop(autobuild::BuildState& bst) {
    auto goSpireTech = [&]() {
      buildSpireInMain();
      upgrade(Metabolic_Boost);
      buildN(Zerg_Lair, 1);
      buildN(Zerg_Extractor, 1);
    };
    auto goHydraliskTech = [&]() {
      upgrade(Muscular_Augments) && upgrade(Grooved_Spines);
      buildDenInMain();
      upgrade(Metabolic_Boost);
      buildN(Zerg_Extractor, 1);
    };

    goHydraliskTech();
    if (shouldGoAir(bst)) {
      if (countPlusProduction(bst, Zerg_Drone) >= 22) {
        buildN(Zerg_Extractor, 3);
      }
      if (countPlusProduction(bst, Zerg_Drone) >= 18) {
        buildN(Zerg_Extractor, 2);
      }
      if (countPlusProduction(bst, Zerg_Drone) >= 16) {
        goSpireTech();
      }
    } else {
      if (countPlusProduction(bst, Zerg_Drone) >= 32 &&
          bst.gas < 300) { // Think about this number more
        buildN(Zerg_Extractor, 3);
      }
      if (countPlusProduction(bst, Zerg_Drone) >=
          30) { // Think about this number more
        buildN(Zerg_Hatchery, 6);
      }
      if (countPlusProduction(bst, Zerg_Drone) >= 26 &&
          bst.gas < 200) { // Think about this number more
        buildN(Zerg_Extractor, 2);
      }
      if (countPlusProduction(bst, Zerg_Drone) >= 22) {
        buildN(Zerg_Hatchery, 5);
      }
      if (countPlusProduction(bst, Zerg_Drone) >= 16) {
        buildN(Zerg_Hatchery, 4);
      }
      if (countPlusProduction(bst, Zerg_Drone) >= 16) {
        goHydraliskTech();
      }
    }
  }

  void lateGame(autobuild::BuildState& bst) {
    if (enemyResourceDepots >= bases) {
      // They're being greedy -- let's go kill them!
      goUpgrades(bst, 12, 5, 5);
      goDevelop(bst);
      goHatcheries(bst);
      takeNBases(bst, std::min(enemyResourceDepots, bst.workers / 6));
      goArmy(bst);
    } else if (weAreSafeAtHome) {
      // We can be greedy!
      goHatcheries(bst);
      goArmy(bst);
      goUpgrades(bst, 12, 5, 5);
      goDrones(bst);
      if (shouldGoAir(bst)) {
        buildN(Zerg_Mutalisk, 12);
      }
      goDevelop(bst);
    } else {
      // We need to survive!
      goDrones(bst);
      goHatcheries(bst);
      goDevelop(bst);
      goArmy(bst);
      goUpgrades(bst, 18, 9, 9);
    }

    takeNBases(bst, 3);
    buildN(Zerg_Drone, 13);
  }

  bool readyToExpandVsOneBase(autobuild::BuildState& bst) {
    return countPlusProduction(bst, Zerg_Zergling) +
        6 * countPlusProduction(bst, Zerg_Mutalisk) +
        4 * countPlusProduction(bst, Zerg_Hydralisk) - 8 * enemyCorsairCount >=
        50;
  }

  void transitionVsOneBase(autobuild::BuildState& bst) {
    const bool goHydras = preferHydras();
    const int droneCount = countPlusProduction(bst, Zerg_Drone);

    buildN(Zerg_Hatchery, 5);
    build(Zerg_Zergling);
    buildN(Zerg_Drone, 30);
    build(goHydras ? Zerg_Hydralisk : Zerg_Mutalisk);
    if (readyToExpandVsOneBase(bst)) {
      takeNBases(bst, 3);
      buildN(Zerg_Drone, 24);
    }
    buildN(Zerg_Drone, 18);

    if (goHydras) {
      if (countPlusProduction(bst, Zerg_Hatchery) >= 5) {
        buildN(Zerg_Extractor, 2);
      }
      int droneCount = countPlusProduction(bst, Zerg_Drone);
      if (droneCount >= 16) {
        upgrade(Muscular_Augments) && upgrade(Grooved_Spines);
        buildDenInMain();
      }
      upgrade(Metabolic_Boost);
    } else {
      if (droneCount >= 18 && hasOrInProduction(bst, Zerg_Lair)) {
        buildN(Zerg_Extractor, 2);
      }
      if (bst.workers >= 14) {
        buildSpireInMain();
        upgrade(Metabolic_Boost);
        buildN(Zerg_Lair, 1);
      }
    }

    if (droneCount >= 14) {
      buildN(Zerg_Extractor, 1);
    }
    if (droneCount >= 13) {
      buildN(Zerg_Hatchery, 3, naturalPos);
    }

    // Avoid death
    if (!weAreSafeAtHome && enemyGoingGround) {
      buildN(Zerg_Zergling, 12);
      buildSunkens(
          bst,
          std::max(enemyGatewayCount, sunkensRequired),
          {},
          enemyProximity > 0.5);
      if (myCompletedHatchCount > 2) {
        buildN(Zerg_Zergling, 18);
      }
      if (hasOrInProduction(bst, Zerg_Spire)) {
        buildN(Zerg_Mutalisk, 12);
      }
    }

    // Keep vision on the enemy
    buildN(Zerg_Zergling, 2);

    if (hasOrInProduction(bst, Zerg_Spire)) {
      buildN(Zerg_Mutalisk, 8);
    }
  }

  void openOverpool(autobuild::BuildState& bst) {
    // It'd be simpler if we just kept a "units all time" count
    // -- lots of build orders would use that
    if (!trainedInitialZerglings) {
      buildN(Zerg_Zergling, 6);
    }
    takeNBases(bst, 2);
    if (countPlusProduction(bst, Zerg_Hatchery) <= 1) {
      buildN(Zerg_Drone, 11);
    }
    buildN(Zerg_Drone, 10);
    buildN(Zerg_Spawning_Pool, 1);
    buildN(Zerg_Overlord, 2);
    buildN(Zerg_Drone, 9);
  }

  virtual void buildStep2(autobuild::BuildState& bst) override {
    autoUpgrade = false;
    bst.autoBuildRefineries = countPlusProduction(bst, Zerg_Drone) >= 30;

    if (completedThreeBases || enemyExpanded) {
      lateGame(bst);
    } else {
      transitionVsOneBase(bst);
    }
    goSneakDrones(bst);
    respondToCorsairs(bst);
    respondToCatastrophe(bst);
    respondToMiningOut(bst);
    openOverpool(bst);
  }
};

class ABBOzvpohydras : public ABBOzvpoverpoolbase {
 public:
  using ABBOzvpoverpoolbase::ABBOzvpoverpoolbase;

 protected:
  virtual bool preferHydras() const override {
    return true;
  }
};
class ABBOzvpomutas : public ABBOzvpoverpoolbase {
 public:
  using ABBOzvpoverpoolbase::ABBOzvpoverpoolbase;

 protected:
  virtual bool preferHydras() const override {
    return false;
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOzvpohydras, UpcId, State*, Module*);
REGISTER_SUBCLASS_3(ABBOBase, ABBOzvpomutas, UpcId, State*, Module*);
}
