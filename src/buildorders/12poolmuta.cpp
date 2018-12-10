/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "base.h"

namespace cherrypi {

class ABBO12PoolMuta : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  bool hasSpire = false;
  bool hasCompletedSpire = false;
  bool hasMutas = false;
  Position baseSunkenPos;
  bool hasCompletedNatural = false;

  virtual void preBuild2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    bool attack = st.frame >= 24 * 60 * 6;
    if (enemyZealotCount * 2.0 == enemyArmySupply &&
        enemyStaticDefenceCount == 0) {
      attack = true;
    }
    postBlackboardKey("TacticsAttack", attack);

    hasSpire = !state_->unitsInfo().myUnitsOfType(Zerg_Spire).empty();
    hasCompletedSpire =
        !state_->unitsInfo().myCompletedUnitsOfType(Zerg_Spire).empty();
    hasMutas = !state_->unitsInfo().myUnitsOfType(Zerg_Mutalisk).empty();

    baseSunkenPos = kInvalidPosition;
    for (int i = state_->areaInfo().numMyBases(); i != 0;) {
      --i;
      Unit* depot = state_->areaInfo().myBase(i)->resourceDepot;
      if (depot) {
        baseSunkenPos = findSunkenPosNear(Zerg_Sunken_Colony, depot->pos());
        if (baseSunkenPos != kInvalidPosition) {
          Unit* sunken = utils::getBestScoreCopy(
              state_->unitsInfo().myBuildings(),
              [&](Unit* u) {
                if (u->type != Zerg_Sunken_Colony &&
                    u->type != Zerg_Creep_Colony) {
                  return kfInfty;
                }
                float d = utils::distance(u, baseSunkenPos);
                if (d > 4 * 12) {
                  return kfInfty;
                }
                return d;
              },
              kfInfty);
          if (sunken) {
            baseSunkenPos = kInvalidPosition;
          } else {
            break;
          }
        }
      }
    }
    hasCompletedNatural = false;
    Tile* naturalTile =
        state_->tilesInfo().tryGetTile(naturalPos.x, naturalPos.y);
    if (naturalTile && naturalTile->building && naturalTile->building->isMine &&
        naturalTile->building->completed()) {
      hasCompletedNatural = true;
    }

    preferSafeExpansions = false;
  }

  virtual void buildStep2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    st.autoBuildRefineries = countPlusProduction(st, Zerg_Extractor) == 0 ||
        currentFrame >= 15 * 60 * 8 || hasCompletedSpire;
    autoExpand = currentFrame >= 15 * 60 * 9;

    int droneCount = countPlusProduction(st, Zerg_Drone);
    int zerglingCount = countPlusProduction(st, Zerg_Zergling);
    int mutaliskCount = countPlusProduction(st, Zerg_Mutalisk);
    int sunkenCount = countPlusProduction(st, Zerg_Sunken_Colony);

    if (st.frame < 15 * 60 * 12 && !hasSpire) {
      buildN(Zerg_Drone, 18);
      buildN(Zerg_Spire, 1);
      buildSunkens(st, 1);
      if (enemySupplyInOurBase) {
        buildN(Zerg_Zergling, (int)(enemyAttackingGroundArmySupply * 0.875));
      }
      buildN(Zerg_Overlord, 2);
      buildN(Zerg_Drone, 14);
      buildN(Zerg_Zergling, 4);
      buildN(Zerg_Lair, 1);

      bool beingProxied = enemyProxyBarracksCount + enemyProxyGatewayCount +
          enemyProxyForgeCount + enemyProxyCannonCount;
      bool beingAttacked = zerglingCount < enemyArmySupply - sunkenCount * 2 ||
          (beingProxied && armySupply < 4.0);
      if (beingAttacked) {
        if (droneCount >= 11) {
          if (enemyAttackingArmySupply) {
            buildN(Zerg_Hatchery, 3);
          }
          int nSunkens = 1;
          if (enemyArmySupply == enemyZealotCount * 2.0 &&
              enemyArmySupply >= 10.0) {
            nSunkens = 2;
          }
          buildN(Zerg_Sunken_Colony, nSunkens, baseSunkenPos);
          if (hasCompletedNatural) {
            buildSunkens(st, 2);
          }
          if (enemyAttackingArmySupply == 0.0) {
            buildN(Zerg_Hatchery, 3);
          }
          buildN(Zerg_Zergling, 8 + enemyArmySupply / 2.0);
        } else {
          buildN(Zerg_Zergling, 4 + enemyArmySupply);
        }
      }

      if (countPlusProduction(st, Zerg_Hatchery) == 1) {
        build(Zerg_Hatchery, nextBase);
      }
      buildN(Zerg_Extractor, 1);
      buildN(Zerg_Spawning_Pool, 1);
      if (enemyArmySupplyInOurBase == 0.0 &&
          countPlusProduction(st, Zerg_Sunken_Colony) == 0 && !beingAttacked) {
        buildN(Zerg_Drone, 13);
      } else {
        buildN(Zerg_Zergling, 6);
      }
      return;
    }

    if (countPlusProduction(st, Zerg_Hatchery) >= 3 && hasMutas) {
      if (bases < 3 && canExpand && !st.isExpanding) {
        build(Zerg_Hatchery, nextBase);
      }
    }

    build(Zerg_Zergling);
    build(Zerg_Mutalisk);

    if (st.frame >= 15 * 60 * 9) {
      if (mutaliskCount >= 9 || zerglingCount >= 12) {
        upgrade(Metabolic_Boost);
      }

      if (zerglingCount < enemyMissileTurretCount * 3 - enemyVultureCount * 4 +
              enemyGoliathCount * 2) {
        buildN(Zerg_Zergling, mutaliskCount * 3);
      }

      if (mutaliskCount >= 24) {
        buildN(Zerg_Zergling, mutaliskCount);
      }
    }

    if (armySupply >= droneCount * 0.66 &&
        countProduction(st, Zerg_Drone) <
            (groundArmySupply > enemyGroundArmySupply ? 2 : 1) *
                (bases >= 4 && armySupply >= 34.0 ? 2 : 1)) {
      buildN(Zerg_Drone, 70);
    }

    if (droneCount >= 30) {
      if (baseSunkenPos != kInvalidPosition &&
          countPlusProduction(st, Zerg_Creep_Colony) == 0) {
        buildN(Zerg_Creep_Colony, bases + 2, baseSunkenPos);
      }
    }

    if (st.frame >= 15 * 60 * 7 + 15 * 30 && !enemyHasExpanded &&
        enemyFactoryCount == 0 && mutaliskCount == 0) {
      buildSunkens(st, 3);
      if (enemyAttackingArmySupply - enemyVultureCount * 2 >= 4.0) {
        buildSunkens(st, 4);
      }
    }
    if (droneCount < 22 && !hasSpire) {
      if (droneCount >= 16 && enemyMarineCount >= 8) {
        buildSunkens(st, std::max((int)(enemyArmySupply / 2.5), 5));
      }
    }

    if (mutaliskCount >= 10) {
      upgrade(Zerg_Flyer_Carapace_1) && upgrade(Zerg_Flyer_Attacks_1) &&
          upgrade(Zerg_Flyer_Carapace_2) && upgrade(Zerg_Flyer_Attacks_2);
      if (mutaliskCount >= 20) {
        upgrade(Zerg_Melee_Attacks_1) && upgrade(Zerg_Carapace_1);
      }
    }

    if (!enemyHasExpanded) {
      if (enemyStaticDefenceCount) {
        buildSunkens(st, 1);
      } else {
        buildSunkens(st, 2);
      }
    }

    if (enemySupplyInOurBase && !hasCompletedSpire) {
      buildN(Zerg_Zergling, (int)(enemyAttackingGroundArmySupply * 0.875));
    }

    if ((std::max(enemyArmySupply - 6.0, enemyAttackingArmySupply) >= 4.0 &&
         armySupply < 8.0) ||
        enemyVultureCount) {
      buildSunkens(st, 2);
    }

    if (st.frame >= 15 * 60 * 15) {
      // if (countProduction(st, Zerg_Drone) == 0 && totalDronesMade < 6 +
      // st.frame / 15 * 30) {
      if (countProduction(st, Zerg_Drone) == 0 &&
          droneCount < 12 + st.frame / 15 * 30) {
        buildN(Zerg_Drone, 48);
      }

      if (droneCount >= 29) {
        if (upgrade(Pneumatized_Carapace) && has(st, Pneumatized_Carapace)) {
          if (bases >= 3) {
            buildN(Zerg_Hive, 1) && upgrade(Adrenal_Glands);
          }
        }
      }
    }

    if (armySupply > enemyArmySupply || armySupply >= 6.0) {
      if (enemyCloakedUnitCount) {
        upgrade(Pneumatized_Carapace);
      }
      buildN(Zerg_Drone, 20);
      if (hasCompletedSpire) {
        buildN(Zerg_Mutalisk, 4);
      }
    }
    if (mutaliskCount >= 11 &&
        mutaliskCount * 2 >= std::max(7, (int)(enemyArmySupply / 2.0))) {
      buildN(Zerg_Drone, 32);
    }

    if (enemyVultureCount) {
      buildSunkens(st, 1);
    }

    if (st.frame < 24 * 60 * 6 && !hasCompletedSpire) {
      bool makeLings = zerglingCount < (enemyZealotCount - sunkenCount) * 3;
      if (enemyRace == +tc::BW::Race::Protoss || enemyZealotCount) {
        if (droneCount >= 14 && zerglingCount < 8) {
          makeLings = true;
        }
      }
      if (makeLings) {
        upgrade(Metabolic_Boost);
        buildSunkens(st, enemyArmySupply >= 6.0 ? 3 : 2);
        build(Zerg_Zergling);
      }
    }

    if (countPlusProduction(st, Zerg_Creep_Colony)) {
      build(Zerg_Sunken_Colony);
    }
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBO12PoolMuta, UpcId, State*, Module*);
}
