/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "module.h"
#include "squadcombat/agent.h"
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

 protected:
  State* state;

  /// Micromanagement state of our units.
  std::unordered_map<Unit const*, Agent> agents_;

  /// Micromanagement state of enemy units.
  std::unordered_map<Unit const*, EnemyState> enemyStates_;

  /// Takes incoming UPCs (usually from the Tactics module) and forms
  /// clusters of units that fight collaboratively.
  bool formNewSquad(std::shared_ptr<UPCTuple> sourceUpc, int sourceUpcId);

  void updateTask(std::shared_ptr<Task> task);
};

} // namespace cherrypi
