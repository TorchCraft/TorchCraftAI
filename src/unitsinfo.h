/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "basetypes.h"
#include "buildtype.h"
#include "cherrypi.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <list>
#include <random>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cherrypi {

class State;

typedef int32_t UnitId;

/**
 * Represents a unit in the game.
 *
 * Unit objects have game lifetime. Thus, you can hold pointers to them.
 */
struct Unit {
  UnitId id = -1;
  int x = 0;
  int y = 0;
  PlayerId playerId = -1;
  /// This unit is currently visible. If this field is set, all other fields
  /// are up-to-date, otherwise everything is from when we last saw it.
  bool visible = false;
  /// We've seen this unit die.
  bool dead = false;
  /// If the unit is not visible, but we've scouted the last location it was at,
  /// and we don't have a better clue about where it is, then this is set to
  /// true. The unit might still be in the game, or it might be dead.
  bool gone = false; // todo
  FrameNum firstSeen = 0;
  FrameNum lastSeen = 0;
  FrameNum goneFrame = 0;
  FrameNum lastLarvaSpawn = 0;
  bool isMine = false;
  bool isEnemy = false;
  bool isNeutral = false;
  const BuildType* type = nullptr;
  FrameNum busyUntil = 0;
  /// X coordinate of the *walktile* the build location (top left corner) of the
  /// unit
  int buildX = 0;
  /// Y coordinate of the *walktile* the build location (top left corner) of the
  /// unit
  int buildY = 0;
  const BuildType* constructingType = nullptr;
  const BuildType* upgradingType = nullptr;
  const BuildType* researchingType = nullptr;
  int remainingBuildTrainTime = 0;
  int remainingUpgradeResearchTime = 0;
  Unit* associatedUnit = nullptr;
  int associatedCount = 0;
  Unit* addon = nullptr;
  int sightRange = 0;
  Unit* attackingTarget = nullptr;
  int lastAttacked = 0;
  double topSpeed = 0;
  bool hasCollision = true;
  Position lastSeenPos;
  std::unordered_set<Unit*> inferNearbyUnitsToMove;

  /// A copy of the torchcraft unit data.
  tc::Unit unit = {};

  /// The last UPC that was used to send a TC command involving this unit
  UpcId lastUpcId = kInvalidUpcId;
  /// Bitwise combination of last UPC command and all its sources.
  uint32_t lastUpcCommands = Command::None;
  /// Commands with a probility higher than this will be considered for
  /// lastUpcCommands.
  static float constexpr kLastUpcCommandThreshold = 0.5f;

  // Both the below are guaranteed to be sorted by distance from me
  // All enemy units who are within range to attack you in the next second
  std::vector<Unit*> threateningEnemies;
  // All units that are attacking you
  std::vector<Unit*> beingAttackedByEnemies;
  // these helpers can be used for efficiency when computing moves
  // All units in sight range
  std::vector<Unit*> unitsInSightRange;
  // All buildings in sight range
  std::vector<Unit*> obstaclesInSightRange;
  // All enemies/allied units in sight range, without buildings
  std::vector<Unit*> enemyUnitsInSightRange;
  std::vector<Unit*> allyUnitsInSightRange;

  std::array<size_t, 16> containerIndices;
  static const size_t invalidIndex = (size_t)-1;

  Unit() {
    containerIndices.fill((size_t)invalidIndex);
  }

  bool flag(tc::Unit::Flags n) const {
    return (unit.flags & n) != 0;
  }

  bool attacking() const {
    return flag(tc::Unit::Flags::Attacking);
  }
  bool burrowed() const {
    return flag(tc::Unit::Flags::Burrowed);
  }
  bool cloaked() const {
    return flag(tc::Unit::Flags::Cloaked);
  }
  bool idle() const {
    return flag(tc::Unit::Flags::Idle);
  }
  bool completed() const {
    return flag(tc::Unit::Flags::Completed);
  }
  bool detected() const {
    return flag(tc::Unit::Flags::Detected);
  }
  bool morphing() const {
    return flag(tc::Unit::Flags::Morphing);
  }
  bool beingGathered() const {
    return flag(tc::Unit::Flags::BeingGathered);
  }
  bool active() const {
    if (dead) {
      return false;
    }
    auto constexpr flip = tc::Unit::Flags::Powered | tc::Unit::Flags::Completed;
    auto constexpr mask = tc::Unit::Flags::BeingConstructed |
        tc::Unit::Flags::Completed | tc::Unit::Flags::Loaded |
        tc::Unit::Flags::LockedDown | tc::Unit::Flags::Maelstrommed |
        tc::Unit::Flags::Powered | tc::Unit::Flags::Stasised |
        tc::Unit::Flags::Stuck;
    return ((unit.flags ^ flip) & mask) == 0;
  }
  bool powered() const {
    return flag(tc::Unit::Flags::Powered);
  }
  bool lifted() const {
    return flag(tc::Unit::Flags::Lifted);
  }
  bool carryingMinerals() const {
    return flag(tc::Unit::Flags::CarryingMinerals);
  }
  bool carryingGas() const {
    return flag(tc::Unit::Flags::CarryingGas);
  }
  bool carryingResources() const {
    return carryingMinerals() || carryingGas();
  }
  bool moving() const {
    return flag(tc::Unit::Flags::Moving);
  }
  bool upgrading() const {
    return flag(tc::Unit::Flags::Upgrading);
  }
  bool researching() const {
    return flag(tc::Unit::Flags::Researching);
  }
  bool blind() const {
    return flag(tc::Unit::Flags::Blind);
  }
  bool beingConstructed() const {
    return flag(tc::Unit::Flags::BeingConstructed);
  }
  bool flying() const {
    return flag(tc::Unit::Flags::Flying);
  }
  bool invincible() const {
    return flag(tc::Unit::Flags::Invincible);
  }
  bool irradiated() const {
    return flag(tc::Unit::Flags::Irradiated);
  }
  bool plagued() const {
    return flag(tc::Unit::Flags::Plagued);
  }
  bool underDarkSwarm() const {
    return flag(tc::Unit::Flags::UnderDarkSwarm);
  }
  bool gatheringGas() const {
    return flag(tc::Unit::Flags::GatheringGas);
  }
  bool gatheringMinerals() const {
    return flag(tc::Unit::Flags::GatheringMinerals);
  }
  bool gathering() const {
    return gatheringGas() || gatheringMinerals();
  }
  bool constructing() const {
    return flag(tc::Unit::Flags::Constructing);
  }
  bool repairing() const {
    return flag(tc::Unit::Flags::Repairing);
  }
  bool stimmed() const {
    return flag(tc::Unit::Flags::Stimmed);
  }
  bool ensnared() const {
    return flag(tc::Unit::Flags::Ensnared);
  }
  double atTopSpeed() const {
    return tc::BW::data::Acceleration[type->unit] <= 1
        ? true
        : unit.velocityX * unit.velocityX + unit.velocityY * unit.velocityY >
            0.90 * topSpeed * topSpeed;
  }

  bool canKite(Unit const* dest) const;
  inline bool canAttack(Unit const* dest) const {
    return dest->detected() && !dest->invincible() &&
        (dest->flying() ? type->hasAirWeapon : type->hasGroundWeapon);
  }

 private:
  double cdMultiplier() const;

 public:
  double cd() const;
  double maxCdAir() const;
  double maxCdGround() const;
  Vec2 velocity() const;
  Vec2 pxVelocity() const;
  double pxTopSpeed() const;
  double maxCdAgainst(Unit const* target) const;
  double rangeAgainst(Unit const* target) const;
  double pxRangeAgainst(Unit const* target) const;

  Position pos() const {
    return Position(x, y);
  }
  Vec2 posf() const;

  // In range of source, assuming source has had [frames] to move towards us
  bool inRangeOf(Unit const* source, double frames = 0U) const;

  /// Compute HP and shield damage to dest when attacking now.
  void computeDamageTo(Unit const* dest, int* hpDamage, int* shieldDamage)
      const;
  /// Compute HP and shield damage to dest when attacking now and dest has
  /// `destShield` shield points left.
  void computeDamageTo(
      Unit const* dest,
      int* hpDamage,
      int* shieldDamage,
      int destShield) const;

  // Computes number of hits to kill target (effective health points)
  double computeEHP(Unit const* dest) const;

  inline double computeHPDamage(Unit const* dest, double dmg) const {
    auto air = dest->flying();
    auto dmgType = air ? unit.airDmgType : unit.groundDmgType;
    auto numAttacks = air ? type->numAirAttacks : type->numGroundAttacks;
    return damageMultiplier(dmgType, dest->unit.size) * dmg -
        numAttacks * dest->unit.armor;
  }
  inline double computeHPDamage(Unit const* dest) const {
    return computeHPDamage(dest, dest->flying() ? unit.airATK : unit.groundATK);
  }
  inline double computeShieldDamage(Unit const* dest, double dmg) const {
    auto numAttacks =
        dest->flying() ? type->numAirAttacks : type->numGroundAttacks;
    return dmg - numAttacks * dest->unit.shieldArmor;
  }
  inline double computeShieldDamage(Unit const* dest) const {
    return computeShieldDamage(
        dest, dest->flying() ? unit.airATK : unit.groundATK);
  }
  Position getMovingTarget() const;
  double damageMultiplier(Unit const* dest) const;
  double damageMultiplier(int damageType, int unitSize) const;
};

/**
 * Updates and organizes information about all the units in the game.
 */
class UnitsInfo {
 public:
  typedef std::vector<Unit*> Units;

  UnitsInfo(State* state_);
  ~UnitsInfo();
  UnitsInfo(UnitsInfo const&) = delete;
  UnitsInfo& operator=(UnitsInfo const&) = delete;

  Unit* getUnit(UnitId id);

  /// Our units of a particular type (does not include dead units).
  const Units& myUnitsOfType(const BuildType* type);
  /// Our completed units of a particular type (does not include dead units).
  const Units& myCompletedUnitsOfType(const BuildType* type);

  /// All units we've ever seen
  const Units& allUnitsEver() {
    return allUnitsEver_;
  }
  /// All units that are not dead. This includes gone units.
  const Units& liveUnits() {
    return liveUnits_;
  }
  /// All units that are currently visible.
  const Units& visibleUnits() {
    return visibleUnits_;
  }
  /// All units that are currently not visible.
  const Units& hiddenUnits() {
    return hiddenUnits_;
  }
  /// All buildings that are currently visible.
  const Units& visibleBuildings() {
    return visibleBuildings_;
  }
  /// All resources (mineral fields, gas geysers + refineries)
  const Units& resourceUnits() {
    return resourceUnits_;
  }

  /// All our (live) units
  const Units& myUnits() {
    return myUnits_;
  }
  const Units& myWorkers() {
    return myWorkers_;
  }
  const Units& myBuildings() {
    return myBuildings_;
  }
  const Units& myResourceDepots() {
    return myResourceDepots_;
  }

  /// All enemy units that are not dead (includes gone units).
  const Units& enemyUnits() {
    return enemyUnits_;
  }
  /// All enemy units that we can see right now.
  const Units& visibleEnemyUnits() {
    return visibleEnemyUnits_;
  }

  /// All neutral units that are not dead
  const Units& neutralUnits() {
    return neutralUnits_;
  }

  /// All units that were discovered since the previous update/step.
  const Units& getNewUnits() {
    return newUnits_;
  }
  /// All units that has started morphing to a different unit type (was
  /// not morphing last update/step).
  const Units& getStartedMorphingUnits() {
    return startedMorphingUnits_;
  }
  /// All units that finished being built or finished morphing since the
  /// previous update/step.
  const Units& getCompletedOrMorphedUnits() {
    return completedOrMorphedUnits_;
  }
  /// All units that went from hidden to visible since the previous
  /// update/step.
  const Units& getShowUnits() {
    return showUnits_;
  }
  /// All units that went from visible to hidden since the previous
  /// update/step.
  const Units& getHideUnits() {
    return hideUnits_;
  }
  /// All units that died since the previous update/step.
  const Units& getDestroyUnits() {
    return destroyUnits_;
  }

  /// Parse the units directly from the tcstate, this is only reasonable
  /// when we have mapHack on. Keep in mind not all things that we precompute,
  /// like beingAttackedByEnemies, etc, are filled out in the mapHacked units.
  const Units& mapHacked();

  // Returns enemyUnits, or enemy units from mapHack if available
  Units enemyUnitsMapHacked();

  const std::unordered_map<const BuildType*, int>& inferredEnemyUnitTypes();

  void update();

 protected:
  State* state_ = nullptr;
  void updateUnit(Unit*, const tc::Unit&, tc::State*, bool maphack = false);
  void updateGroups(Unit* i);
  size_t inferPositionsUnitAtIndex(Position pos);
  Position
  inferMovePosition(Position source, bool flying, int tileVisibiltyAge);
  void inferMoveUnit(Unit* u, Position newPos);
  void inferUpdateNearbyUnits();

  std::array<Units, 16> unitContainers_;
  std::unordered_map<UnitId, Unit> unitsMap_;
  std::unordered_map<int, std::unordered_map<const BuildType*, double>>
      speedMap_;

  Units& allUnitsEver_ = unitContainers_[0];
  Units& liveUnits_ = unitContainers_[1];
  Units& visibleUnits_ = unitContainers_[2];
  Units& hiddenUnits_ = unitContainers_[3];
  Units& visibleBuildings_ = unitContainers_[4];
  Units& resourceUnits_ = unitContainers_[5];

  Units& myUnits_ = unitContainers_[6];
  Units& myWorkers_ = unitContainers_[7];
  Units& myBuildings_ = unitContainers_[8];
  Units& myResourceDepots_ = unitContainers_[9];

  Units& enemyUnits_ = unitContainers_[10];
  Units& visibleEnemyUnits_ = unitContainers_[11];

  Units& neutralUnits_ = unitContainers_[12];

  std::unordered_map<int, Units> myUnitsOfType_;
  std::unordered_map<int, Units> myCompletedUnitsOfType_;

  Units newUnits_;
  Units startedMorphingUnits_;
  Units completedOrMorphedUnits_;
  Units showUnits_;
  Units hideUnits_;
  Units destroyUnits_;
  std::unordered_map<const BuildType*, int> memoizedEnemyUnitTypes_;

  std::vector<uint8_t> inferPositionsUnitAt;
  FrameNum lastInferUpdateNearbyUnits;

  std::minstd_rand rngEngine;

  std::unordered_map<UnitId, Unit> mapHackUnitsMap_;
  Units& mapHackUnits_ = unitContainers_[15];
};

} // namespace cherrypi

namespace std {
inline ostream& operator<<(ostream& oss, cherrypi::Unit const* unit) {
  if (!unit) {
    oss << "i (nullptr)";
  } else {
    // Log units with 'i' prefix so that we'll be able to use 'u' for UPC tuples
    oss << "i" << unit->id << " (" << unit->type->name << ")";
  }
  return oss;
}
} // namespace std
