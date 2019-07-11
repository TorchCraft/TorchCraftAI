/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "unitsinfo.h"

#include "cherrypi.h"
#include "common/rand.h"
#include "state.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <fmt/format.h>
#include <gflags/gflags.h>

DEFINE_bool(
    inferEnemyPositions,
    false,
    "Whether to make a guess about the positions about enemy units which are "
    "not visible, by assuming that a unit moved a short distance or moved with "
    "other units");

DEFINE_double(
    inferEnemyPositionsChance,
    0.66,
    "When an enemy unit is spotted, this is the chance of moving other enemy "
    "units that have been seen near it to a nearby location");

namespace cherrypi {

double Unit::damageMultiplier(int dtype, int usz) const {
  if (dtype == +tc::BW::DamageType::Concussive) {
    if (usz == +tc::BW::UnitSize::Large) {
      return 0.25;
    } else if (usz == +tc::BW::UnitSize::Medium) {
      return 0.5;
    }
    return 1.0;
  } else if (dtype == +tc::BW::DamageType::Explosive) {
    if (usz == +tc::BW::UnitSize::Small) {
      return 0.5;
    } else if (usz == +tc::BW::UnitSize::Medium) {
      return 0.75;
    }
    return 1.0;
  }
  return 1.0;
}

double Unit::damageMultiplier(Unit const* dest) const {
  if (!canAttack(dest)) {
    return 0.0;
  }
  return damageMultiplier(
      dest->flying() ? unit.airDmgType : unit.groundDmgType, dest->unit.size);
}

bool Unit::canKite(Unit const* dest) const {
  if (!dest || dest->type->isBuilding || !canAttack(dest)) {
    return false;
  }
  auto myRange = rangeAgainst(dest);
  auto thRange = dest->rangeAgainst(this);

  // This may be incorrect due to use of current CD rather than maximum CD.
  auto myDist = 0.5 * (this->topSpeed * this->unit.maxCD);
  auto thDist = 0.5 * (dest->topSpeed * dest->unit.maxCD);

  bool output = myDist + myRange >= thRange + thDist;
  if (output && VLOG_IS_ON(1)) {
    VLOG(1) << utils::unitString(this) << " can kite "
            << utils::unitString(dest);
  }
  return output;
}

double Unit::cd() const {
  // There's no meaningful difference between a unit's
  // remaining air/ground cooldown.
  //
  // Air/ground attacks can set it to
  // different values but both weapons go on cooldown
  // for the same length of time.
  //
  // Note that the only units with different air/ground weapons in the
  // first place are Wraiths, Goliaths, and Scouts.
  return std::max(this->unit.airCD, this->unit.groundCD);
}

double Unit::cdMultiplier() const {
  // There's some debate about Ensnare's effect on various units
  // and its interaction with Stimpacks. So here's an approximate version.
  // (For example, it may not affect Ultralisks at all, or maybe it does a
  // little)
  // See http://liquipedia.net/starcraft/Ensnare
  // and http://www.teamliquid.net/forum/closed-threads/41799-queen-muta-zvt#15
  // Here's a simple estimation. Let's revisit it if Ensnare proves important.
  if (ensnared()) {
    return 1.18;
  } else if (stimmed()) {
    return 0.5;
  }
  return 1.0;
}

double Unit::maxCdAir() const {
  return cdMultiplier() * type->airWeaponCooldown;
}

double Unit::maxCdGround() const {
  return cdMultiplier() * this->unit.maxCD;
}

Vec2 Unit::velocity() const {
  return pxVelocity() / tc::BW::XYPixelsPerWalktile;
}

Vec2 Unit::pxVelocity() const {
  return Vec2(unit.velocityX, unit.velocityY);
}

double Unit::pxTopSpeed() const {
  return tc::BW::XYPixelsPerWalktile * topSpeed;
}

double Unit::maxCdAgainst(Unit const* target) const {
  return target->flying() ? this->maxCdAir() : this->maxCdGround();
}

double Unit::rangeAgainst(Unit const* target) const {
  return target->flying() ? this->unit.airRange : this->unit.groundRange;
}

double Unit::pxRangeAgainst(Unit const* target) const {
  return tc::BW::XYPixelsPerWalktile * rangeAgainst(target);
}

Vec2 Unit::posf() const {
  return Vec2(
      (float)unit.pixel_x / tc::BW::XYPixelsPerWalktile,
      (float)unit.pixel_y / tc::BW::XYPixelsPerWalktile);
}

bool Unit::inRangeOf(Unit const* source, double frames) const {
  if (!source->canAttack(this)) {
    return false;
  }
  auto pxRange = source->pxRangeAgainst(this);
  auto pxDistance = utils::pxDistanceBB(this, source);
  auto pxTraveled = frames * source->topSpeed * tc::BW::XYPixelsPerWalktile;
  return pxDistance <= pxRange + pxTraveled;
}

void Unit::computeDamageTo(
    Unit const* dest,
    int* hpDamage,
    int* shieldDamage,
    int destShield) const {
  *shieldDamage = 0;
  *hpDamage = 0;
  if (!canAttack(dest)) {
    return;
  }

  auto dmg = dest->flying() ? unit.airATK : unit.groundATK;
  if (destShield > 0) {
    auto tmp = computeShieldDamage(dest);
    if (destShield >= tmp) {
      *shieldDamage = tmp;
      return;
    }
    *shieldDamage = dest->unit.shield;
    dmg = dmg - dest->unit.shield;
  }

  *hpDamage = computeHPDamage(dest, dmg);
}

void Unit::computeDamageTo(Unit const* dest, int* hpDamage, int* shieldDamage)
    const {
  computeDamageTo(dest, hpDamage, shieldDamage, dest->unit.shield);
}

// Can never be 0
double Unit::computeEHP(Unit const* dest) const {
  if (!canAttack(dest)) {
    return kdInfty;
  }

  auto numAttacks =
      dest->flying() ? type->numAirAttacks : type->numGroundAttacks;

  // TODO Deal with spells
  // TODO Deal with splash damage
  // TODO Deal with altitude/doodad-based miss%
  // Each hit deals at least 0.5 damage
  auto shdmg = std::max(0.5 * numAttacks, computeShieldDamage(dest));
  auto hpdmg = std::max(0.5 * numAttacks, computeHPDamage(dest));

  return dest->unit.shield / shdmg + dest->unit.health / hpdmg;
}

Position Unit::getMovingTarget() const {
  if (moving()) {
    // TODO Make sure this is right for things like carriers with 2 orders
    return Position(unit.orders[0].targetX, unit.orders[0].targetY);
  }
  return Position(-1, -1);
}

UnitsInfo::UnitsInfo(State* state)
    : state_(state),
      rngEngine(common::Rand::makeRandEngine<std::minstd_rand>()) {
  if (FLAGS_inferEnemyPositions) {
    inferPositionsUnitAt.resize(TilesInfo::tilesWidth * TilesInfo::tilesHeight);
  }
}

UnitsInfo::~UnitsInfo() {}

Unit* UnitsInfo::getUnit(UnitId id) {
  auto i = unitsMap_.find(id);
  if (i != unitsMap_.end()) {
    return &i->second;
  }
  return nullptr;
}

const UnitsInfo::Units& UnitsInfo::myUnitsOfType(const BuildType* type) {
  return myUnitsOfType_[type->unit];
}

const UnitsInfo::Units& UnitsInfo::myCompletedUnitsOfType(
    const BuildType* type) {
  return myCompletedUnitsOfType_[type->unit];
}

const std::unordered_map<const BuildType*, int>&
UnitsInfo::inferredEnemyUnitTypes() {
  if (!memoizedEnemyUnitTypes_.empty()) {
    return memoizedEnemyUnitTypes_;
  }
  for (auto eu : enemyUnits_) {
    memoizedEnemyUnitTypes_[eu->type] += 1;
  }
  // max TT depth = 5, funrollloops! ;)
  // see http://wiki.teamliquid.net/starcraft/Technology_tree
  for (auto bt_n : memoizedEnemyUnitTypes_) {
    for (auto bt1 : bt_n.first->prerequisites) {
      if (memoizedEnemyUnitTypes_.find(bt1) == memoizedEnemyUnitTypes_.end()) {
        memoizedEnemyUnitTypes_[bt1] = 1;
      }
      for (auto bt2 : bt1->prerequisites) {
        if (memoizedEnemyUnitTypes_.find(bt2) ==
            memoizedEnemyUnitTypes_.end()) {
          memoizedEnemyUnitTypes_[bt2] = 1;
        }
        for (auto bt3 : bt2->prerequisites) {
          if (memoizedEnemyUnitTypes_.find(bt3) ==
              memoizedEnemyUnitTypes_.end()) {
            memoizedEnemyUnitTypes_[bt3] = 1;
          }
          for (auto bt4 : bt3->prerequisites) {
            if (memoizedEnemyUnitTypes_.find(bt4) ==
                memoizedEnemyUnitTypes_.end()) {
              memoizedEnemyUnitTypes_[bt4] = 1;
            }
            for (auto bt5 : bt4->prerequisites) {
              if (memoizedEnemyUnitTypes_.find(bt5) ==
                  memoizedEnemyUnitTypes_.end()) {
                memoizedEnemyUnitTypes_[bt5] = 1;
              }
            }
          }
        }
      }
    }
  }
  return memoizedEnemyUnitTypes_;
}

void UnitsInfo::update() {
  newUnits_.clear();
  startedMorphingUnits_.clear();
  completedOrMorphedUnits_.clear();
  showUnits_.clear();
  hideUnits_.clear();
  destroyUnits_.clear();
  memoizedEnemyUnitTypes_.clear();

  FrameNum frame = state_->currentFrame();

  bool updateMyGroups = false;

  if (state_->mapHack()) {
    for (auto& e : state_->tcstate()->frame->units) {
      for (auto& v : e.second) {
        auto ut = tc::BW::UnitType::_from_integral_nothrow(v.type);
        if (!ut) {
          continue;
        }
        Unit* u = &mapHackUnitsMap_[v.id];
        if (!u->type) {
          u->firstSeen = frame;
        }

        auto* prevType = u->type;

        updateUnit(u, v, state_->tcstate(), /*maphack = */ true);

        if (u->type != prevType) {
          u->firstSeen = frame;
        }
      }
    }

    for (int id : state_->tcstate()->deaths) {
      mapHackUnitsMap_.erase(id);
    }
    mapHackUnits_.clear();
    for (auto& v : mapHackUnitsMap_) {
      mapHackUnits_.emplace_back(&v.second);
    }
  }

  for (auto& v : state_->units()) {
    Unit* u = &unitsMap_[v.first];
    u->threateningEnemies.clear();
    u->beingAttackedByEnemies.clear();
    u->unitsInSightRange.clear();
    u->obstaclesInSightRange.clear();
    u->enemyUnitsInSightRange.clear();
    u->allyUnitsInSightRange.clear();
  }

  for (auto& v : state_->units()) {
    Unit* u = &unitsMap_[v.first];
    bool doUpdateGroups = false;
    if (!u->type) {
      u->firstSeen = frame;
      newUnits_.push_back(u);
      doUpdateGroups = true;
    }
    if (!u->visible) {
      showUnits_.push_back(u);
      doUpdateGroups = true;
    }

    bool wasActive = u->active();
    bool wasCompleted = u->completed();
    bool wasMorphing = u->morphing();
    int prevPlayerId = u->playerId;
    auto* prevType = u->type;

    updateUnit(u, *v.second, state_->tcstate());

    if (u->morphing() && (!wasMorphing || u->type != prevType)) {
      doUpdateGroups = true;
      startedMorphingUnits_.push_back(u);
    }

    if (u->type != prevType) {
      doUpdateGroups = true;
      u->firstSeen = frame;
    }

    if (u->completed() != wasCompleted || u->morphing() != wasMorphing) {
      doUpdateGroups = true;
      if ((!wasCompleted && u->completed()) ||
          (wasMorphing && !u->morphing())) {
        completedOrMorphedUnits_.push_back(u);
      }
    }

    if (u->active() != wasActive || u->playerId != prevPlayerId ||
        u->type != prevType) {
      doUpdateGroups = true;
    }

    if (doUpdateGroups) {
      updateGroups(u);

      if (!updateMyGroups) {
        if (prevPlayerId == state_->playerId() ||
            u->playerId == state_->playerId()) {
          updateMyGroups = true;
        }
      }
    }
  }

  for (int id : state_->tcstate()->deaths) {
    Unit* u = getUnit(id);
    if (u) {
      u->dead = true;
      u->goneFrame = frame;
      destroyUnits_.push_back(u);
      updateGroups(u);
      if (u->isMine) {
        updateMyGroups = true;
      }
    }
  }

  Units needUpdateGroups;
  for (Unit* u : visibleUnits()) {
    if (u->gone) {
      u->gone = false;
      needUpdateGroups.push_back(u);
    }
    if (u->lastSeen != frame) {
      u->visible = false;
      hideUnits_.push_back(u);
      needUpdateGroups.push_back(u);
    }
  }
  for (Unit* u : needUpdateGroups) {
    updateGroups(u);
  }

  for (Unit* u : hiddenUnits()) {
    if (!u->gone) {
      Tile* t = state_->tilesInfo().tryGetTile(u->x, u->y);
      if (!t || t->visible) {
        if (frame - u->goneFrame >= 40) {
          u->goneFrame = frame;
        } else if (frame - u->goneFrame >= 20) {
          if (FLAGS_inferEnemyPositions && !u->type->isNonUsable) {
            // Instead of immediately setting gone, assume the unit simply moved
            // a bit.
            Position newPos =
                inferMovePosition(u->lastSeenPos, u->flying(), 15 * 10);
            if (newPos != Position()) {
              inferMoveUnit(u, newPos);
            } else {
              u->gone = true;
            }
          } else {
            u->gone = true;
            updateGroups(u);
          }
        }
      }
    }
  }

  for (Unit* u : liveUnits_) {
    if (u->gone || u->type->isGas || u->type->isMinerals || u->playerId < 0) {
      continue;
    }
    u->threateningEnemies.clear();
    u->unitsInSightRange.clear();

    // should we really compute this for enemies ?
    for (Unit* o : liveUnits_) {
      if (o->gone || o->id == u->id) {
        continue;
      }

      auto oSize = std::max(
          std::abs(o->type->dimensionUp - o->type->dimensionDown),
          std::abs(o->type->dimensionLeft - o->type->dimensionRight));
      if (o->visible && utils::distance(o, u) <= u->sightRange + oSize / 8) {
        u->unitsInSightRange.push_back(o);
      }
      if (o->playerId < 0)
        continue;

      if (u->playerId != o->playerId && u->inRangeOf(o, DFOASG(12, 24))) {
        u->threateningEnemies.push_back(o);
      }
    }
    VLOG(4) << utils::unitString(u) << " has threatening enemies: "
            << utils::unitsString(u->threateningEnemies);
  }
  for (Unit* u : liveUnits_) {
    auto distanceComp = [&u](Unit const* a, Unit const* b) {
      return utils::distanceBB(a, u) < utils::distanceBB(b, u);
    };
    std::sort(
        u->threateningEnemies.begin(),
        u->threateningEnemies.end(),
        distanceComp);
    std::sort(
        u->beingAttackedByEnemies.begin(),
        u->beingAttackedByEnemies.end(),
        distanceComp);
    std::sort(
        u->unitsInSightRange.begin(), u->unitsInSightRange.end(), distanceComp);
    for (auto o : u->unitsInSightRange) {
      auto isObstacle =
          o->type->isBuilding || o->type->isGas || o->type->isMinerals;
      if (isObstacle) {
        u->obstaclesInSightRange.push_back(o);
      }
      if (o->playerId != u->playerId) {
        u->enemyUnitsInSightRange.push_back(o);
      } else {
        u->allyUnitsInSightRange.push_back(o);
      }
    }
  }

  if (updateMyGroups) {
    for (auto& v : myUnitsOfType_) {
      v.second.clear();
    }
    for (Unit* u : myUnits()) {
      myUnitsOfType_[u->type->unit].push_back(u);
    }
    for (auto& v : myCompletedUnitsOfType_) {
      v.second.clear();
    }
    for (Unit* u : myUnits()) {
      if (u->completed()) {
        myCompletedUnitsOfType_[u->type->unit].push_back(u);
      }
    }
  }

  if (FLAGS_inferEnemyPositions) {
    auto rng = std::bind(std::uniform_real_distribution<>(0.0, 1.0), rngEngine);
    for (Unit* u : getShowUnits()) {
      if (!u->isEnemy || u->type->isBuilding) {
        continue;
      }
      // We just spotted a new enemy. Maybe move some enemy units that were seen
      // near it (and are currently invisible) to some nearby invisible tile?
      for (Unit* u2 : u->inferNearbyUnitsToMove) {
        if (u2->visible) {
          continue;
        }
        float d = utils::distance(u, u2);
        if (d / u2->topSpeed > frame - u2->lastSeen) {
          continue;
        }
        if (rng() <= FLAGS_inferEnemyPositionsChance) {
          continue;
        }
        Position newPos =
            inferMovePosition(Position(u->x, u->y), u2->flying(), 4);
        if (newPos != Position()) {
          inferMoveUnit(u2, newPos);
        }
      }
    }
    if (frame - lastInferUpdateNearbyUnits >= 30) {
      lastInferUpdateNearbyUnits = frame;
      inferUpdateNearbyUnits();
    }
  }
}

static int unitSightRange(const Unit* u, tc::State* tcstate) {
  auto isMorphingBuilding = [&]() {
    if (u->type == buildtypes::Zerg_Hive) {
      return true;
    }
    if (u->type == buildtypes::Zerg_Lair) {
      return true;
    }
    if (u->type == buildtypes::Zerg_Greater_Spire) {
      return true;
    }
    if (u->type == buildtypes::Zerg_Spore_Colony) {
      return true;
    }
    if (u->type == buildtypes::Zerg_Sunken_Colony) {
      return true;
    }
    return false;
  };

  if (u->type->isBuilding && !u->lifted() && !u->completed() &&
      !isMorphingBuilding()) {
    return tc::BW::XYWalktilesPerBuildtile * 4;
  }
  if (u->blind()) {
    return tc::BW::XYWalktilesPerBuildtile * 2;
  }
  if (u->type == buildtypes::Terran_Ghost &&
      // TODO: Should only account for upgrade level of unit's owner!
      tcstate->getUpgradeLevel(tc::BW::UpgradeType::Ocular_Implants)) {
    return tc::BW::XYWalktilesPerBuildtile * 11;
  }
  if (u->type == buildtypes::Zerg_Overlord &&
      // TODO: Should only account for upgrade level of unit's owner!
      tcstate->getUpgradeLevel(tc::BW::UpgradeType::Antennae)) {
    return tc::BW::XYWalktilesPerBuildtile * 11;
  }
  if (u->type == buildtypes::Protoss_Observer &&
      // TODO: Should only account for upgrade level of unit's owner!
      tcstate->getUpgradeLevel(tc::BW::UpgradeType::Sensor_Array)) {
    return tc::BW::XYWalktilesPerBuildtile * 11;
  }
  if (u->type == buildtypes::Protoss_Scout &&
      // TODO: Should only account for upgrade level of unit's owner!
      tcstate->getUpgradeLevel(tc::BW::UpgradeType::Apial_Sensors)) {
    return tc::BW::XYWalktilesPerBuildtile * 11;
  }
  return u->type->sightRange;
}

void UnitsInfo::updateUnit(
    Unit* u,
    const tc::Unit& tcu,
    tc::State* tcstate,
    bool maphack) {
  u->id = tcu.id;
  if (FLAGS_inferEnemyPositions) {
    inferMoveUnit(u, Position(tcu.x, tcu.y));
  } else {
    u->x = tcu.x;
    u->y = tcu.y;
  }
  if (u->x < 0) {
    u->x = 0;
  }
  if (u->y < 0) {
    u->y = 0;
  }
  if (u->x >= state_->mapWidth()) {
    u->x = state_->mapWidth() - 1;
  }
  if (u->y >= state_->mapHeight()) {
    u->y = state_->mapHeight() - 1;
  }
  u->playerId = tcu.playerId;
  u->visible = true;
  u->lastSeen = state_->currentFrame();
  u->lastSeenPos = Position(tcu.x, tcu.y);
  if (!u->type || u->type->unit != tcu.type) {
    u->type = getUnitBuildType(tcu.type);
  }
  if (std::max(tcu.groundCD, tcu.airCD) >
      std::max(u->unit.groundCD, u->unit.airCD)) {
    u->lastAttacked = state_->currentFrame();
  }
  u->unit = tcu;
  u->isMine = u->playerId == state_->playerId();
  u->isEnemy =
      u->playerId != state_->playerId() && u->playerId != state_->neutralId();
  u->isNeutral = u->playerId == state_->neutralId();
  u->buildX = (u->unit.pixel_x - u->type->tileWidth * 16) / 8;
  u->buildY = (u->unit.pixel_y - u->type->tileHeight * 16) / 8;

  const BuildType* constructingType = nullptr;
  const BuildType* upgradingType = nullptr;
  const BuildType* researchingType = nullptr;
  if (u->isMine) {
    if (u->upgrading()) {
      auto ut =
          tc::BW::UpgradeType::_from_integral_nothrow(tcu.buildTechUpgradeType);
      if (ut) {
        upgradingType = getUpgradeBuildType(
            tcu.buildTechUpgradeType, tcstate->getUpgradeLevel(*ut) + 1);
      } else {
        LOG(WARNING) << "Unknown upgrade type: " << tcu.buildTechUpgradeType;
      }
    } else if (u->researching()) {
      researchingType = getTechBuildType(tcu.buildTechUpgradeType);
    } else {
      constructingType = getUnitBuildType(tcu.buildTechUpgradeType);
    }
  }
  u->constructingType = constructingType;
  u->upgradingType = upgradingType;
  u->researchingType = researchingType;
  u->remainingBuildTrainTime = tcu.remainingBuildTrainTime;
  u->remainingUpgradeResearchTime = tcu.remainingUpgradeResearchTime;
  u->associatedUnit = getUnit(tcu.associatedUnit);
  u->associatedCount = tcu.associatedCount;
  u->addon = u->type->canBuildAddon ? u->associatedUnit : nullptr;
  u->sightRange = unitSightRange(u, tcstate);
  // TODO Add checks for workers gathering gas/minerals
  u->hasCollision = !u->flying() && !u->burrowed();

  for (auto& order : tcu.orders) {
    if (maphack) {
      break;
    }
    if (utils::tcOrderIsAttack(order.type)) {
      auto unit = getUnit(order.targetId);
      if (unit != nullptr) {
        unit->beingAttackedByEnemies.push_back(u);
        u->attackingTarget = unit;
        unit->lastAttacked = state_->currentFrame();
        break;
      }
    }
  }

  // Top speed per player, to be tech aware
  if (speedMap_.find(u->playerId) == speedMap_.end()) {
    speedMap_.emplace(
        u->playerId, std::unordered_map<const BuildType*, double>());
  }
  auto playerSpeedMap = speedMap_[u->playerId];
  if (playerSpeedMap.find(u->type) == playerSpeedMap.end()) {
    playerSpeedMap[u->type] =
        tc::BW::data::TopSpeed[u->type->unit] / tc::BW::XYPixelsPerWalktile;
  }
  auto curSpeed =
      Vec2(tcu.velocityX, tcu.velocityY).length() / tc::BW::XYPixelsPerWalktile;
  playerSpeedMap[u->type] = std::max(playerSpeedMap[u->type], curSpeed);
  u->topSpeed = playerSpeedMap[u->type];

  if (u->firstSeen == state_->currentFrame() &&
      u->type == buildtypes::Zerg_Larva && u->associatedUnit) {
    auto lastSpawn = u->associatedUnit->lastLarvaSpawn;
    u->associatedUnit->lastLarvaSpawn = u->firstSeen;

    // Logging units on frame 0 segfaults
    if (state_->currentFrame() > 24) {
      if (lastSpawn == 0) {
        VLOG(3) << fmt::format(
            "{} spawned its first larva, {}",
            utils::unitString(u->associatedUnit),
            utils::unitString(u));
      } else {
        VLOG(3) << fmt::format(
            "{} spawned {} (last Larva spawn was {} frames ago)",
            utils::unitString(u->associatedUnit),
            utils::unitString(u),
            state_->currentFrame() - lastSpawn);
      }
    }
  }
}

void UnitsInfo::updateGroups(Unit* u) {
  auto updateUnitContainer = [&](auto& cont, bool contain) {
    size_t contIndex = &cont - unitContainers_.data();
    size_t& uIndex = u->containerIndices[contIndex];
    if (contain) {
      if (uIndex == Unit::invalidIndex) {
        uIndex = cont.size();
        cont.push_back(u);
      }
    } else {
      if (uIndex != Unit::invalidIndex) {
        if (uIndex != cont.size() - 1) {
          cont.back()->containerIndices[contIndex] = uIndex;
          std::swap(cont.back(), cont[uIndex]);
        }
        cont.pop_back();
        uIndex = Unit::invalidIndex;
      }
    }
  };

  updateUnitContainer(allUnitsEver_, true);
  updateUnitContainer(liveUnits_, !u->dead);
  updateUnitContainer(visibleUnits_, !u->dead && u->visible);
  updateUnitContainer(hiddenUnits_, !u->dead && !u->visible);
  updateUnitContainer(
      visibleBuildings_, !u->dead && u->visible && u->type->isBuilding);
  updateUnitContainer(
      resourceUnits_, !u->dead && !u->gone && u->type->isResourceContainer);

  updateUnitContainer(
      myUnits_,
      !u->dead && u->visible && u->playerId == state_->playerId() &&
          u->powered());
  updateUnitContainer(
      myWorkers_,
      !u->dead && u->visible && u->playerId == state_->playerId() &&
          u->type->isWorker && u->completed());
  updateUnitContainer(
      myBuildings_,
      !u->dead && u->visible && u->playerId == state_->playerId() &&
          u->type->isBuilding);
  updateUnitContainer(
      myResourceDepots_,
      !u->dead && u->visible && u->playerId == state_->playerId() &&
          u->type->isResourceDepot);

  updateUnitContainer(enemyUnits_, !u->dead && u->isEnemy);
  updateUnitContainer(visibleEnemyUnits_, !u->dead && u->visible && u->isEnemy);

  updateUnitContainer(neutralUnits_, !u->dead && u->isNeutral);
}

size_t UnitsInfo::inferPositionsUnitAtIndex(Position pos) {
  const Tile* tile = state_->tilesInfo().tryGetTile(pos.x, pos.y);
  return tile ? tile - state_->tilesInfo().tiles.data() : 0;
}

Position UnitsInfo::inferMovePosition(
    Position source,
    bool flying,
    int tileVisibiltyAge) {
  const float maxMoveDistance = 4 * 12;

  const int mapWidth = state_->mapWidth();
  const int mapHeight = state_->mapHeight();
  auto* tilesData = state_->tilesInfo().tiles.data();
  const int frame = state_->currentFrame();

  std::vector<uint8_t> visited(TilesInfo::tilesWidth * TilesInfo::tilesHeight);
  std::deque<const Tile*> open;
  open.push_back(&state_->tilesInfo().getTile(source.x, source.y));
  while (!open.empty()) {
    const Tile* tile = open.front();
    open.pop_front();

    if (frame - tile->lastSeen > tileVisibiltyAge &&
        inferPositionsUnitAt[tile - tilesData] < 2) {
      return Position(tile->x + 2, tile->y + 2);
    }

    auto add = [&](const Tile* ntile) {
      if ((!flying && !tile->entirelyWalkable)) {
        return;
      }
      auto& v = visited[ntile - tilesData];
      if (v) {
        return;
      }
      v = 1;
      if (utils::distance(ntile->x, ntile->y, source.x, source.y) <=
          maxMoveDistance) {
        open.push_back(ntile);
      }
    };

    if (tile->x > 0) {
      add(tile - 1);
    }
    if (tile->y > 0) {
      add(tile - TilesInfo::tilesWidth);
    }
    if (tile->x < mapWidth - tc::BW::XYWalktilesPerBuildtile) {
      add(tile + 1);
    }
    if (tile->y < mapHeight - tc::BW::XYWalktilesPerBuildtile) {
      add(tile + TilesInfo::tilesHeight);
    }
  }
  return Position();
}

void UnitsInfo::inferMoveUnit(Unit* u, Position newPos) {
  --inferPositionsUnitAt[inferPositionsUnitAtIndex(Position(u->x, u->y))];
  u->x = newPos.x;
  u->y = newPos.y;
  ++inferPositionsUnitAt[inferPositionsUnitAtIndex(Position(u->x, u->y))];
}

void UnitsInfo::inferUpdateNearbyUnits() {
  for (Unit* u : visibleEnemyUnits()) {
    if (u->type->isBuilding || u->type->isWorker) {
      continue;
    }
    auto& inferNearbyUnitsToMove = u->inferNearbyUnitsToMove;
    for (Unit* u2 : enemyUnits()) {
      if (u2->type->isBuilding || u2->type->isWorker || u2->gone) {
        continue;
      }
      if (utils::distance(u, u2) <= 4 * 6) {
        inferNearbyUnitsToMove.insert(u2);
      }
    }
  }
}

const UnitsInfo::Units& UnitsInfo::mapHacked() {
  if (!state_->mapHack()) {
    throw std::runtime_error(
        "Trying to get mapHacked units on a state that doesn't have mapHack "
        "on");
  }
  return mapHackUnits_;
}

UnitsInfo::Units UnitsInfo::enemyUnitsMapHacked() {
  auto units = mapHacked();
  Units enemyUnits;
  std::copy_if(
      units.begin(), units.end(), std::back_inserter(enemyUnits), [](Unit* u) {
        return u->isEnemy;
      });
  return enemyUnits;
}
} // namespace cherrypi
