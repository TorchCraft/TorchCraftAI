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
  enum Style { Defilers, Ultralisks, Guardians };
  virtual Style style() const = 0;
  bool goingDefilers() const {
    return style() == Style::Defilers;
  };
  bool goingUltralisks() const {
    return style() == Style::Ultralisks;
  };
  bool goingGuardians() const {
    return style() == Style::Guardians;
  };

 public:
  using ABBOBase::ABBOBase;

  // Three build orders:
  //
  // 2 Base Defiler
  // * Pro-style 2-Base Defiler rush
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

  enum Progress { Opening, Spire, LateGame };
  Progress progress = Progress::Opening;
  bool readyToAttack = false;

  virtual void preBuild2(autobuild::BuildState& bst) override {
    if (progress != Progress::LateGame) {
      if (has(bst, Zerg_Spire)) {
        progress = Progress::Spire;
      }
      if (goingDefilers()) {
        if (has(bst, Zerg_Defiler_Mound)) {
          progress = Progress::LateGame;
        }
      } else if (goingUltralisks()) {
        if (has(bst, Zerg_Ultralisk_Cavern)) {
          progress = Progress::LateGame;
        }
      } else if (goingGuardians()) {
        if (has(bst, Zerg_Greater_Spire)) {
          progress = Progress::LateGame;
        }
      }
    }

    // We want to see Factory/Barracks count so we can add appropriate defense.
    const bool scout = countPlusProduction(bst, Zerg_Hatchery) > 1;
    postBlackboardKey(Blackboard::kMinScoutFrameKey, scout ? 1 : 0);

    readyToAttack = readyToAttack || myMutaliskCount || myUltraliskCount;
    postBlackboardKey("TacticsAttack", readyToAttack || enemyVultureCount == 0);

    int gasDrones =
        std::max(0, int(myDroneCount - 16 + (bst.minerals - bst.gas) / 50));
    postBlackboardKey(Blackboard::kGathererMinGasWorkers, gasDrones);
  }

  void getUpgrades() {
    upgrade(Zerg_Carapace_1) && upgrade(Zerg_Carapace_2) &&
        upgrade(Zerg_Carapace_3) && upgrade(Zerg_Melee_Attacks_1) &&
        upgrade(Zerg_Melee_Attacks_2) && upgrade(Zerg_Melee_Attacks_3);
  }

  void buildThreeBaseDrones() {
    buildN(Zerg_Drone, std::min(60, bases * 15));
  }

  void buildRequiredLurkers() {
    buildN(
        Zerg_Lurker,
        (enemyMarineCount + enemyFirebatCount + enemyMedicCount) / 4);
  }

  void doLateGameDefilers(BuildState& bst) {
    buildN(Zerg_Hatchery, countPlusProduction(bst, Zerg_Drone) / 6);
    takeNBases(bst, 4);
    build(Zerg_Zergling);
    build(Zerg_Mutalisk);
    buildN(Zerg_Lurker, 12, 4);
    buildN(Zerg_Mutalisk, std::min(enemyVultureCount, 3));
    buildN(Zerg_Scourge, 2 * enemyScienceVesselCount);
    buildN(Zerg_Defiler, 3);
    getUpgrades();
    upgrade(Plague);
    if (myZerglingCount >= 18) {
      buildThreeBaseDrones();
    }
    buildRequiredLurkers();
    buildN(Zerg_Zergling, enemyGroundArmySupply);
    buildN(Zerg_Mutalisk, enemyWraithCount + 4 * enemyBattlecruiserCount);
    takeNBases(bst, 3);
    buildN(Zerg_Defiler, 2);
    upgrade(Adrenal_Glands);
    upgrade(Consume);
    upgrade(Lurker_Aspect);
    upgrade(Metabolic_Boost);
    buildN(Zerg_Extractor, bases);
    buildN(Zerg_Drone, 30);
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
    buildN(Zerg_Extractor, bases);
    buildN(Zerg_Drone, 30);
  }

  void doLateGameGuardians(BuildState& bst) {
    buildN(Zerg_Hatchery, countPlusProduction(bst, Zerg_Drone) / 6);
    takeNBases(bst, 4);
    build(Zerg_Zergling);
    build(Zerg_Mutalisk);
    if (myGuardianCount >= 4) {
      buildThreeBaseDrones();
    }
    buildN(Zerg_Zergling, enemyGroundArmySupply);
    buildN(Zerg_Guardian, 8);
    buildN(
        Zerg_Scourge,
        3 * enemyScienceVesselCount + 3 * enemyWraithCount +
            4 * enemyValkyrieCount + 5 * enemyBattlecruiserCount);
    takeNBases(bst, 3);
    getUpgrades();
    upgrade(Adrenal_Glands);
    upgrade(Metabolic_Boost);
    buildN(Zerg_Extractor, bases);
    buildN(Zerg_Drone, 30);
  }

  void doOpening(BuildState& bst) {
    constexpr int sunkenStrength = 3;
    const int enemyStrength = std::max(
        4 * enemyBarracksCount,
        1 +
            int(std::min(1.0, 3 * enemyProximity) *
                (2 * enemyMarineCount + 3 * enemyMedicCount +
                 3 * enemyFirebatCount)));
    const int sunkensToBuild = utils::safeClamp(
        enemyStrength / sunkenStrength - myZerglingCount - 3 * myMutaliskCount,
        std::min(1, enemyVultureCount),
        5);
    const int zerglingsToBuild =
        utils::safeClamp(enemyStrength - mySunkenCount * sunkenStrength, 2, 18);

    build(Zerg_Zergling);
    build(Zerg_Mutalisk);
    buildN(Zerg_Drone, 45 - int(12 * enemyProximity));
    if (progress == Progress::Spire) {
      buildN(Zerg_Hatchery, 5);
      buildN(Zerg_Extractor, bases);
      takeNBases(bst, 3);
      if (enemyVultureCount > 2) {
        buildN(Zerg_Hatchery, 4);
      }
      if (goingDefilers()) {
        buildRequiredLurkers();
        upgrade(Lurker_Aspect);
        buildN(Zerg_Defiler, 1);
        buildN(Zerg_Hydralisk_Den, 1, homePosition);
        upgrade(Consume);
        buildN(Zerg_Defiler_Mound, 1, homePosition);
      } else {
        getUpgrades();
        buildN(Zerg_Evolution_Chamber, 1, homePosition);
        if (goingUltralisks()) {
          buildN(Zerg_Ultralisk_Cavern, 1, homePosition);
        } else if (goingGuardians()) {
          buildN(Zerg_Greater_Spire, 1);
        }
      }
      buildN(Zerg_Mutalisk, 9);
      buildN(Zerg_Hive, 1);
    }
    buildN(Zerg_Queens_Nest, 1);
    buildN(Zerg_Mutalisk, 6);
    // Autobuild tends to underproduce Overlords here which delays the
    // Mutalisks by a full Overlord production cycle.
    buildN(Zerg_Overlord, 6);
    buildN(Zerg_Drone, 24);
    if (!hasOrInProduction(bst, Zerg_Greater_Spire)) {
      buildN(Zerg_Spire, 1, homePosition);
    }
    upgrade(Metabolic_Boost);
    // 2.5 Hatch Muta
    buildN(Zerg_Extractor, 2);
    buildN(Zerg_Hatchery, 3, naturalPos);
    buildN(Zerg_Drone, 14);
    buildN(Zerg_Lair, 1);
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
    buildN(Zerg_Extractor, 1);
    buildN(Zerg_Drone, 13);
    buildN(Zerg_Spawning_Pool, 1);
    takeNBases(bst, 2);
    buildN(Zerg_Drone, 12);
    buildN(Zerg_Overlord, 2);
    buildN(Zerg_Drone, 9);
  }

  virtual void buildStep2(BuildState& bst) override {
    bool defilerTechSatisfied = !goingDefilers() ||
        (countPlusProduction(bst, Zerg_Defiler) > 2 &&
         hasOrInProduction(bst, Consume) && hasOrInProduction(bst, Plague));

    bool ultraliskTechSatisfied = !goingUltralisks() ||
        (countPlusProduction(bst, Zerg_Ultralisk) > 3 &&
         hasOrInProduction(bst, Chitinous_Plating) &&
         hasOrInProduction(bst, Anabolic_Synthesis));

    bool guardianTechSatisfied =
        !goingGuardians() || countPlusProduction(bst, Zerg_Guardian) > 3;

    autoUpgrade = progress == Progress::LateGame &&
        countUnits(bst, Zerg_Extractor) && defilerTechSatisfied &&
        ultraliskTechSatisfied && guardianTechSatisfied;

    autoExpand = progress == Progress::LateGame ||
        countPlusProduction(bst, Zerg_Hatchery) > 3;
    bst.autoBuildHatcheries = progress == Progress::LateGame;
    bst.autoBuildRefineries = progress != Progress::Opening;

    if (progress == Progress::LateGame) {
      if (goingDefilers()) {
        doLateGameDefilers(bst);
      } else if (goingUltralisks()) {
        doLateGameUltralisks(bst);
      } else {
        doLateGameGuardians(bst);
      }
    } else {
      doOpening(bst);
    }
  }
};

class ABBOzvt2basedefiler : public ABBOzvt2basehivebase {
 public:
  using ABBOzvt2basehivebase::ABBOzvt2basehivebase;

 protected:
  virtual Style style() const override {
    return Style::Defilers;
  }
};
class ABBOzvt2baseultra : public ABBOzvt2basehivebase {
 public:
  using ABBOzvt2basehivebase::ABBOzvt2basehivebase;

 protected:
  virtual Style style() const override {
    return Style::Ultralisks;
  }
};
class ABBOzvt2baseguardian : public ABBOzvt2basehivebase {
 public:
  using ABBOzvt2basehivebase::ABBOzvt2basehivebase;

 protected:
  virtual Style style() const override {
    return Style::Guardians;
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOzvt2basedefiler, UpcId, State*, Module*);
REGISTER_SUBCLASS_3(ABBOBase, ABBOzvt2baseultra, UpcId, State*, Module*);
REGISTER_SUBCLASS_3(ABBOBase, ABBOzvt2baseguardian, UpcId, State*, Module*);
} // namespace cherrypi
