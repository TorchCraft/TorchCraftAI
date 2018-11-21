/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "basetypes.h"
#include "tracker.h"

namespace cherrypi {

struct BuildType;
struct Unit;

/**
 * Tracks movement of a set of units to a target location.
 *
 * Ongoing is defined as at least one unit being in movement. The tracker might
 * switch back to Pending if no unit is moving.
 * Success is defined in terms of units reaching the target location with the
 * center of units being within `mind` distance.
 */
class MovementTracker : public Tracker {
 public:
  MovementTracker(
      std::unordered_set<Unit*> units,
      int targetX,
      int targetY,
      float mind = 4.0f * 2,
      int timeout = 15);

 protected:
  virtual bool updateNotTracking(State* s) override {
    return false;
  }
  virtual bool updatePending(State* s) override;
  virtual bool updateOngoing(State* s) override;

 private:
  std::unordered_set<Unit*> units_;
  Position target_;
  float mind_;
};

/**
 * Tracks construction of a building or training of a unit.
 */
class BuildTracker : public Tracker {
 public:
  BuildTracker(Unit* unit, BuildType const* type, int timeout = 15 * 4);

 protected:
  virtual bool updateNotTracking(State* s) override {
    return false;
  }
  virtual bool updatePending(State* s) override;
  virtual bool updateOngoing(State* s) override;

 private:
  void findTarget(State* s);
  void findTargetForDrone(State* s);

  static constexpr FrameNum kMorphTimeout = 4;
  static constexpr float kMorphDistanceThreshold = 4.0f;
  static constexpr FrameNum kNotBuildingTimeout = 4;
  Unit* unit_;
  Unit* target_ = nullptr;
  BuildType const* type_;
  FrameNum startedPendingFrame_ = -1;
};

/**
 * Tracks upgrade development.
 */
class UpgradeTracker : public Tracker {
 public:
  UpgradeTracker(Unit* unit, BuildType const* type, int timeout = 15 * 4);

 protected:
  virtual bool updateNotTracking(State* s) override {
    return false;
  }
  virtual bool updatePending(State* s) override;
  virtual bool updateOngoing(State* s) override;

 private:
  Unit* unit_;
  BuildType const* type_;
};

/**
 * Tracks research progress
 */
class ResearchTracker : public Tracker {
 public:
  ResearchTracker(Unit* unit, BuildType const* type, int timeout = 15 * 4);

 protected:
  virtual bool updateNotTracking(State* s) override {
    return false;
  }
  virtual bool updatePending(State* s) override;
  virtual bool updateOngoing(State* s) override;

 private:
  Unit* unit_;
  BuildType const* type_;
};

/**
 * Tracks a set of units attacking enemy units.
 *
 * Ongoing is defined as at least one unit firing bullets. The tracker might
 * switch back to Pending if no unit is firing.
 * Success is defined in terms of all enemy units being dead.
 */
class AttackTracker : public Tracker {
 public:
  AttackTracker(
      std::unordered_set<Unit*> units,
      std::unordered_set<Unit*> enemies,
      int timeout = 30);
  virtual ~AttackTracker() {}

  void setUnits(std::unordered_set<Unit*> units) {
    units_ = std::move(units);
  }

 protected:
  virtual bool updateNotTracking(State* s);
  virtual bool updatePending(State* s);
  virtual bool updateOngoing(State* s);

  void updateEnemies();

  std::unordered_set<Unit*> units_;
  std::unordered_set<Unit*> enemies_;
};

} // namespace cherrypi
