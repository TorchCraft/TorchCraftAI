/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <memory>
#include <set>
#include <vector>

#include "../squadcombat.h"
#include "basetypes.h"
#include "task.h"

namespace cherrypi {

class Agent;
struct EnemyState;

/**
 * SquadTask controls a "squad" of units (units which share the same Delete
 * or Flee UPC)
 *
 * After doing some group-level coordination, SquadTask delegates individual
 * unit controllers ("Agents") to emit sharp UPCs for translation into game
 * commands (presumably via the UpcToCommand module)
 *
 * How UPCs are interpreted, loosely:
 *  - Flee = 1.0: Run away if able or otherwise evade the enemy
 *    (perhaps by Burrowing)
 *  - Delete = 1.0: Engage the UPC-specified targets recklessly
 *  - Delete < 1: Engage the UPC-specified targets or nearby enemies
 */
class SquadTask : public Task {
 public:
  /// Enemies this Squad should target
  std::vector<Unit*> targets;

  /// Location this Squad should defend or attack
  int targetX = -1;
  int targetY = -1;

  /// Whether to consider targets or targetX/targetY
  bool targetingLocation;

  /// Does this Squad have any air units?
  bool hasAirUnits = false;

  /// Does this Squad have any ground units?
  bool hasGroundUnits = false;

  /// Does this Squad have any cloaked units that can kill things?
  bool hasCloakedFighters = false;

  /// What is the probability -- presumably coming from combat simulation via
  /// Tactics -- that we will win this fight?
  double delProb;

  /// What is the probability that we should flee?
  double fleeProb;

  /// What UPC -- presumably from Tactics -- is directing this squad?
  std::shared_ptr<UPCTuple> sourceUpc;

  /// Centroid of the Squad units
  Position center_;

  /// Known locations of Psionic Storms (so we can dodge them)
  std::vector<Position> storms_;

  /// Stateful information about enemy units
  std::unordered_map<Unit const*, EnemyState>* enemyStates_;

  /// Stateful information about our units
  std::unordered_map<Unit const*, Agent>* agents_;

  /// Models to solicit for UPCs
  std::vector<std::shared_ptr<MicroModel>>* models;

  /// Enemies this Squad should target
  std::vector<Unit*> targets_;

  /// Threatening enemies this Squad should be aware of
  std::vector<Unit*> threats_;

  /// All units relevant to this squad.
  std::unordered_set<Unit*> relevantUnits_;

  const std::unordered_set<Unit*>& squadUnits() const {
    return units();
  }

  const std::unordered_set<Unit*>& relevantUnits() const {
    return relevantUnits_;
  }

  SquadTask(
      int upcId,
      std::shared_ptr<UPCTuple> upc,
      std::unordered_set<Unit*> units,
      std::vector<Unit*> targets,
      std::unordered_map<Unit const*, EnemyState>* enemyStates,
      std::unordered_map<Unit const*, Agent>* agents,
      std::vector<std::shared_ptr<MicroModel>>* models)
      : Task(upcId, units),
        targets(std::move(targets)),
        targetingLocation(false),
        delProb(upc->commandProb(Command::Delete)),
        fleeProb(upc->commandProb(Command::Flee)),
        sourceUpc(upc),
        enemyStates_(enemyStates),
        agents_(agents),
        models(models) {}

  SquadTask(
      int upcId,
      std::shared_ptr<UPCTuple> upc,
      std::unordered_set<Unit*> units,
      int x,
      int y,
      std::unordered_map<Unit const*, EnemyState>* enemyStates,
      std::unordered_map<Unit const*, Agent>* agents,
      std::vector<std::shared_ptr<MicroModel>>* models)
      : Task(upcId, units),
        targetX(x),
        targetY(y),
        targetingLocation(true),
        delProb(upc->commandProb(Command::Delete)),
        fleeProb(upc->commandProb(Command::Flee)),
        sourceUpc(upc),
        enemyStates_(enemyStates),
        agents_(agents),
        models(models) {}

  void update(State* state) override;

  std::vector<Unit*> getGroupTargets(State* state) const;
  std::vector<Unit*> getGroupThreats(State* state) const;

  void pickTargets(State* state);
  void formation(State* state);

  std::vector<std::shared_ptr<UPCTuple>> makeUPCs(State* state);

  bool isIrrelevantTarget(Unit const* u) const;
  bool isImportantTarget(Unit const* u) const;
  bool isRelevantDetector(Unit const* u) const;
  bool isThreat(Unit const* u) const;

  virtual const char* getName() const override {
    return "Squad";
  };
};

} // namespace cherrypi
