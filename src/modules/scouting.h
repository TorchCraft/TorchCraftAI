/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "buildtype.h"
#include "module.h"
#include "task.h"

namespace cherrypi {

/**
 * Scout management
 */
enum class ScoutingGoal {
  FindEnemyBase = 0,
  ExploreEnemyBase,
  FindEnemyExpand,
  SneakyOverlord,
  Automatic // decide depending on context
};

class ScoutingModule : public Module {
 public:
  virtual ~ScoutingModule() = default;

  virtual void step(State* s) override;

  // this should be accessible to the higher-level module
  // or decided in context
  void setScoutingGoal(ScoutingGoal);
  ScoutingGoal goal(State* state) const; // used for automatic decisions

 protected:
  Unit* findUnit(
      State* state,
      std::unordered_map<Unit*, float> const&,
      Position const& pos);
  bool postTask(
      State* state,
      UpcId baseUpcId,
      Unit* unit,
      Position loc,
      ScoutingGoal goal);
  bool postMoveUPC(
      State* state,
      UpcId baseUpcId,
      Unit* unit,
      const Position& loc,
      bool useSafeMove = true);
  Position nextScoutingLocation(
      State* state,
      Unit* unit,
      std::unordered_map<Position, int> const&);
  void updateLocations(
      State* state,
      std::unordered_map<Position, int>&,
      std::vector<Position> const&);

  std::unordered_map<Position, int> startingLocations_;
  ScoutingGoal scoutingGoal_ = ScoutingGoal::Automatic;
};

} // namespace cherrypi
