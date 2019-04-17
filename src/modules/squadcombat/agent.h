/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "basetypes.h"
#include "behavior.h"
#include "buildtype.h"
#include "movefilters.h"
#include "state.h"

namespace cherrypi {

class SquadTask;

/**
 * An Agent represents the micromanagement state of one of our units.
 *
 * Agents belong to a SquadTask, which invokes microFlee() or microDelete().
 *
 * From there, the Agent, will forward control to a series of Behaviors,
 * each of which is given the opportunity to do one of the following:
 * * Post a sharp UPC (for consumption as an in-game command by UPCToCommand)
 * * Do nothing (issue no commands to the unit this frame)
 * * Defer control: give the next Behavior an opportunity to control the unit
 */
class Agent {
 public:
  /// To what squad does this unit belong?
  SquadTask* task;

  /// What unit is this Agent controlling?
  Unit* unit;

  /// The current game state
  State* state;

  /// Behaviors to perform when receiving a Delete UPC
  std::shared_ptr<Behavior> behaviorDelete;

  /// Behaviors to perform when receiving a Flee UPC
  std::shared_ptr<Behavior> behaviorFlee;

  /// What action has been selected for this unit by a Behavior?
  MicroAction currentAction;

  /// Who is this unit intended to fight?
  std::vector<Unit*> legalTargets;

  /// Who has this unit decided to kill?
  Unit* target = nullptr;

  /// Is the target in range right now?
  /// This accounts for latency and unit/target velocities.
  bool targetInRange = false;
  bool prevTargetInRange = false;

  /// Tracks the last target this unit was commanded to attack.
  Unit* attacking = nullptr;

  /// Has this unit joined the vanguard of its squad? Or is it on the way?
  bool wantsToFight = false;

  /// On what frame was this unit last micromanaged?
  int lastMicroFrame = -1;

  /// On what frame did this unit last choose a target?
  int lastTarget = -1;

  /// On what frame did this unit start moving?
  /// -1 when the unit is attacking, rather than moving.
  int lastMove = -1;

  /// On what frame did this unit start attacking?
  /// -1 when the unit is not attacking.
  int lastAttack = -1;

  /// If we attempted to move the unit, the last position to which we attempted
  /// to move it.
  Position lastMoveTo;

  /// The unit's position last time we micromanaged it.
  Position lastPosition;

  /// How many consecutive frames has this unit been inadvertently idle?
  int stuckFrames = 0;

  /// Is this unit a Mutalisk turning to face a Scourge?
  /// Used to apply the triangle method for kiting Scourge with Mutalisks.
  int mutaliskTurning = 0;

  /// SquadTask organizes its units into a formation for attacking.
  /// This is the Agent's assigned formation position, which it may use before
  /// fighting.
  Position formationPosition = kInvalidPosition;

  /// Used by SquadTask in calculating formations
  int formationCounter = 0;

  /// How many frames of being stuck before we attempt to un-stick a unit.
  static constexpr int unstickTriggerFrames = 9;

 public:
  /// Hand control of the unit over to the Agent for fighting.
  std::shared_ptr<UPCTuple> microDelete();

  /// Hand control of the unit over to the Agent for fleeing.
  std::shared_ptr<UPCTuple> microFlee();

  /// Issues a command to the Agent's unit by posting it to the Blackboard.
  void postCommand(tc::BW::UnitCommandType command);

  std::shared_ptr<UPCTuple> attack(Position const& pos);
  std::shared_ptr<UPCTuple> attack(Unit* u);
  std::shared_ptr<UPCTuple> moveTo(Position pos, bool protect = true);
  std::shared_ptr<UPCTuple> moveTo(Vec2 pos, bool protect = true);
  std::shared_ptr<UPCTuple> filterMove(const movefilters::PositionFilters& pfs);
  std::shared_ptr<UPCTuple> smartMove(const Position& tgt);
  std::shared_ptr<UPCTuple> smartMove(Unit* tgt);

  /// Attempt to cast a spell targeting a unit.
  /// Returns a UPC if an acceptable target was found; null otherwise.
  std::shared_ptr<UPCTuple> tryCastSpellOnUnit(
      const BuildType* spell,
      std::function<double(Unit* const)> scoring,
      double minimumScore);

  /// Attempt to cast a spell targeting an area.
  /// Returns a UPC if an acceptable target was found; null otherwise.
  std::shared_ptr<UPCTuple> tryCastSpellOnArea(
      const BuildType* spell,
      double areaWidth,
      double areaHeight,
      std::function<double(Unit* const)> scoring,
      double minimumScore,
      std::function<Position(Position input)> positionTransform = [](auto p) {
        return p;
      });

 protected:
  /// Prepare the unit for micro
  void preMicro();
};

} // namespace cherrypi
