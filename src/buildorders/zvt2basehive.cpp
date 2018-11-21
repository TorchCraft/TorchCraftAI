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

class ABBOzvt2basehivebase : public ABBOBase {
 protected:
  virtual bool guardians() const = 0;

 public:
  using ABBOBase::ABBOBase;

  // Two build orders:
  //
  // 2 Base Ultralisks
  // * https://www.twitch.tv/videos/295002459?t=01h19m12s
  //
  // 2 Base Guardians
  // * Not really a meta build, but gives our build order switcher access to
  // Guardians.
  //
  // Opens 3 Hatch Mutalisk, then transitions into an Ultralisk+Zergling
  // (or Guardian+Zergling) composition on 2 bases.

  enum Progress { Opening, LateGame };

  Progress progress = Progress::Opening;
  bool readyToAttack = false;

  virtual void preBuild2(autobuild::BuildState& bst) override {
    if (progress != Progress::LateGame) {
      if (guardians()) {
        if (has(bst, Zerg_Greater_Spire)) {
          progress = Progress::LateGame;
        }
      } else if (has(bst, Zerg_Ultralisk_Cavern)) {
        progress = Progress::LateGame;
      }
    }

    // We want to see Factory/Barracks count so we can add appropriate defense.
    const bool scout = countPlusProduction(bst, Zerg_Hatchery) > 1;
    postBlackboardKey(Blackboard::kMinScoutFrameKey, scout ? 1 : 0);

    readyToAttack = readyToAttack || myMutaliskCount || myUltraliskCount;
    postBlackboardKey("TacticsAttack", readyToAttack || enemyVultureCount == 0);
  }

  void getUpgrades() {
    upgrade(Zerg_Carapace_1) && upgrade(Zerg_Carapace_2) &&
        upgrade(Zerg_Carapace_3) && upgrade(Zerg_Melee_Attacks_1) &&
        upgrade(Zerg_Melee_Attacks_2) && upgrade(Zerg_Melee_Attacks_3);
  }

  void buildTwoBaseDrones() {
    buildN(Zerg_Drone, 45 - int(12 * enemyProximity));
  }

  void buildThreeBaseDrones() {
    buildN(Zerg_Drone, std::min(60, bases * 15));
  }

  void doLateGameUltralisks(BuildState& bst) {
    buildN(Zerg_Hatchery, countPlusProduction(bst, Zerg_Drone) / 6);
    takeNBases(bst, 4);
    build(Zerg_Zergling);
    if (myUltraliskCount >= 4) {
      buildThreeBaseDrones();
    }
    buildN(Zerg_Zergling, enemyGroundArmySupply);
    build(Zerg_Ultralisk);
    buildN(Zerg_Mutalisk, enemyWraithCount + 4 * enemyBattlecruiserCount);
    takeNBases(bst, 3);
    getUpgrades();
    upgrade(Chitinous_Plating) && upgrade(Anabolic_Synthesis);
    upgrade(Adrenal_Glands);
    upgrade(Metabolic_Boost);
    buildN(Zerg_Drone, 30);
  }

  void doLateGameGuardians(BuildState& bst) {
    buildN(Zerg_Hatchery, countPlusProduction(bst, Zerg_Drone) / 6);
    takeNBases(bst, 4);
    build(Zerg_Zergling);
    if (myGuardianCount >= 4) {
      buildThreeBaseDrones();
    }
    buildN(Zerg_Zergling, enemyGroundArmySupply);
    build(Zerg_Guardian);
    buildN(
        Zerg_Scourge,
        3 * enemyScienceVesselCount + 3 * enemyWraithCount +
            4 * enemyValkyrieCount + 5 * enemyBattlecruiserCount);
    takeNBases(bst, 3);
    getUpgrades();
    upgrade(Adrenal_Glands);
    upgrade(Metabolic_Boost);
    buildN(Zerg_Drone, 30);
  }

  void doOpening(BuildState& bst) {
    constexpr int sunkenStrength = 3;
    const int enemyStrength = std::max(
        4 * enemyBarracksCount,
        1 + int(std::min(1.0, 3 * enemyProximity) *
                (2 * enemyMarineCount + 3 * enemyMedicCount +
                 3 * enemyFirebatCount)));
    const int sunkensToBuild = utils::safeClamp(
        enemyStrength / sunkenStrength - myZerglingCount - 3 * myMutaliskCount,
        std::min(1, enemyVultureCount),
        5);
    const int zerglingsToBuild =
        utils::safeClamp(enemyStrength - mySunkenCount * sunkenStrength, 2, 18);

    if (hasOrInProduction(bst, Zerg_Hive)) {
      buildN(Zerg_Hatchery, 4);
    }
    build(Zerg_Zergling);
    if (guardians() || countPlusProduction(bst, Zerg_Mutalisk) < 12) {
      build(Zerg_Mutalisk);
    }
    upgrade(Adrenal_Glands);
    if (enemyVultureCount < 2) {
      takeNBases(bst, 3);
    }
    if (guardians()) {
      buildN(Zerg_Greater_Spire, 1);
    } else {
      buildN(Zerg_Ultralisk_Cavern, 1, homePosition);
    }
    buildN(Zerg_Mutalisk, 9);
    getUpgrades();
    buildN(Zerg_Evolution_Chamber, 1, homePosition);
    buildN(Zerg_Hive, 1);
    buildN(Zerg_Zergling, 18);
    buildN(Zerg_Mutalisk, 6);
    buildTwoBaseDrones();
    if (!hasOrInProduction(bst, Zerg_Greater_Spire)) {
      buildN(Zerg_Spire, 1, homePosition);
    }
    if (hasOrInProduction(bst, Zerg_Lair) &&
        countPlusProduction(bst, Zerg_Drone) >= 16) {
      buildN(Zerg_Extractor, 2);
    }
    upgrade(Metabolic_Boost);
    buildN(Zerg_Lair, 1);
    buildN(Zerg_Extractor, 1);
    buildN(Zerg_Hatchery, 3, naturalPos);
    buildN(Zerg_Drone, 14);
    buildN(Zerg_Zergling, zerglingsToBuild);
    if (enemyTankCount == 0) {
      buildSunkens(bst, sunkensToBuild);
      if (enemyBarracksCount > 2) {
        buildSunkens(bst, 5);
      }
      if (enemyBarracksCount > 1) {
        buildSunkens(bst, 2);
      }
    }
    buildN(Zerg_Spawning_Pool, 1);
    if (!hasOrInProduction(bst, Zerg_Spawning_Pool)) {
      buildN(Zerg_Drone, 13);
    }
    takeNBases(bst, 2);
    buildN(Zerg_Drone, 12);
    buildN(Zerg_Overlord, 2);
    buildN(Zerg_Drone, 9);
  }

  virtual void buildStep2(BuildState& bst) override {
    autoExpand = progress == Progress::LateGame;
    autoUpgrade = progress == Progress::LateGame && geysers > 3;
    bst.autoBuildRefineries = progress != Progress::Opening;

    if (progress == Progress::Opening) {
      doOpening(bst);
    } else if (guardians()) {
      doLateGameGuardians(bst);
    } else {
      doLateGameUltralisks(bst);
    }
  }
};

class ABBOzvt2baseultra : public ABBOzvt2basehivebase {
 public:
  using ABBOzvt2basehivebase::ABBOzvt2basehivebase;

 protected:
  virtual bool guardians() const override {
    return false;
  }
};
class ABBOzvt2baseguardian : public ABBOzvt2basehivebase {
 public:
  using ABBOzvt2basehivebase::ABBOzvt2basehivebase;

 protected:
  virtual bool guardians() const override {
    return true;
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOzvt2baseultra, UpcId, State*, Module*);
REGISTER_SUBCLASS_3(ABBOBase, ABBOzvt2baseguardian, UpcId, State*, Module*);
}
