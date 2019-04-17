/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "models/micromodel.h"
#include "module.h"
#include "squadcombat/agent.h"
#include "squadcombat/behavior.h"
#include "upc.h"

namespace cherrypi {

struct EnemyState {
  int damages = 0;
  int lastRepairing = 0;
};

/**
 * Module which controls ("micromanages") units into and out of combat.
 *
 * SquadCombat reads diffuse Delete ("Fight") or Flee UPCs from the Blackboard
 * and
 * reposts them as sharp UPCs for commands like Delete or Move.
 *
 * SquadCombatModule is a thin orchestrator for micromanagement. Most of the
 * micromanagement logic lives in the supporting classes of
 * src/modules/squadcombat/
 *
 * * SquadTask: Controls squads (groups of unit with the same UPC)
 * * Agent: Controls individual units using Behaviors
 */
class SquadCombatModule : public Module {
 public:
  virtual void step(State* s) override;
  /// Adds a MicroModel to the end of the list of models which will be updated
  /// and solicited for UPCs
  void enqueueModel(std::shared_ptr<MicroModel> model, std::string name);

  std::shared_ptr<MicroModel> getModel(std::string);
  virtual void onGameEnd(State* state) override;
  virtual void onGameStart(State* state) override;

 protected:
  State* state;

  /// Micromanagement state of our units.
  std::unordered_map<Unit const*, Agent> agents_;

  /// Micromanagement state of enemy units.
  std::unordered_map<Unit const*, EnemyState> enemyStates_;

  /// Models for SquadCombat to solicit for unit UPCs
  std::unordered_map<std::string, std::shared_ptr<MicroModel>> models_;

  /// Takes incoming UPCs (usually from the Tactics module) and forms
  /// clusters of units that fight collaboratively.
  bool formNewSquad(std::shared_ptr<UPCTuple> sourceUpc, int sourceUpcId);

  void updateTask(std::shared_ptr<Task>);

  /// Produces new fight Behaviors for an Agent.
  /// Intended for override by subclasses which insert baseline or ML-powered
  /// behaviors.
  virtual BehaviorList makeDeleteBehaviors();

  /// Produces new flee Behaviors for an Agent.
  /// Intended for override by subclasses which insert baseline or ML-powered
  /// behaviors.
  virtual BehaviorList makeFleeBehaviors();
};

} // namespace cherrypi
