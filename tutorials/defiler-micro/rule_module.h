/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <unordered_map>

#include "flags.h"
#include "module.h"
#include "modules/creategatherattack.h"
#include "modules/dummytactics.h"
#include "modules/squadcombat.h"
#include "modules/tactics.h"
#include "state.h"
#include "unitsinfo.h"
#include "utils.h"

namespace cherrypi {

CPI_DEFINE_BEHAVIOR(TargetWeakest)
CPI_DEFINE_BEHAVIOR(TargetClosest)
CPI_DEFINE_BEHAVIOR(SimpleAttackTarget)
CPI_DEFINE_BEHAVIOR(SimpleAttackMove)

MicroAction BehaviorTargetWeakest::onPerform(Agent& agent) {
  auto* unit = agent.unit;
  auto& enemies = agent.state->unitsInfo().enemyUnits();
  agent.target = utils::getBestScoreCopy(enemies, [&](Unit* target) {
    return target->unit.health + target->unit.shield +
        utils::distance(unit, target) / 1024;
  });
  return pass;
}

MicroAction BehaviorTargetClosest::onPerform(Agent& agent) {
  auto* unit = agent.unit;
  auto& enemies = agent.state->unitsInfo().enemyUnits();
  agent.target = utils::getBestScoreCopy(
      enemies, [&](Unit* target) { return utils::distanceBB(unit, target); });
  return pass;
}

MicroAction BehaviorSimpleAttackTarget::onPerform(Agent& agent) {
  auto* target = agent.target;
  if (target) {
    if (agent.unit->idle() || agent.attacking != target) {
      return doAction(agent.attack(target));
    }
    return doNothing;
  }
  return pass;
}

MicroAction BehaviorSimpleAttackMove::onPerform(Agent& agent) {
  auto& enemies = agent.state->unitsInfo().enemyUnits();
  if (enemies.size() > 0) {
    if (agent.unit->idle()) {
      return doAction(agent.attack(Position{enemies[0]}));
    }
    return doNothing;
  }
  return pass;
}

} // namespace cherrypi

class SquadCombatAttackWeakest : public cherrypi::SquadCombatModule {
  virtual cherrypi::BehaviorList makeDeleteBehaviors() override {
    return cherrypi::BehaviorList{
        std::make_shared<cherrypi::BehaviorAsDefiler>(),
        std::make_shared<cherrypi::BehaviorTargetWeakest>(),
        std::make_shared<cherrypi::BehaviorSimpleAttackTarget>()};
  }
  virtual cherrypi::BehaviorList makeFleeBehaviors() override {
    return makeDeleteBehaviors();
  }
};

class SquadCombatAttackClosest : public cherrypi::SquadCombatModule {
  virtual cherrypi::BehaviorList makeDeleteBehaviors() override {
    return cherrypi::BehaviorList{
        std::make_shared<cherrypi::BehaviorAsDefiler>(),
        std::make_shared<cherrypi::BehaviorTargetClosest>(),
        std::make_shared<cherrypi::BehaviorSimpleAttackTarget>()};
  }
  virtual cherrypi::BehaviorList makeFleeBehaviors() override {
    return makeDeleteBehaviors();
  }
};

class SquadCombatAttackMove : public cherrypi::SquadCombatModule {
  virtual cherrypi::BehaviorList makeDeleteBehaviors() override {
    return cherrypi::BehaviorList{
        std::make_shared<cherrypi::BehaviorAsDefiler>(),
        std::make_shared<cherrypi::BehaviorSimpleAttackMove>()};
  }
  virtual cherrypi::BehaviorList makeFleeBehaviors() override {
    return makeDeleteBehaviors();
  }
};

template <class T>
void addModule(std::vector<std::shared_ptr<cherrypi::Module>>& modules) {
  modules.push_back(cherrypi::Module::make<T>());
}

auto getCombatModules(std::string const& behavior) {
  std::vector<std::shared_ptr<cherrypi::Module>> output;
  addModule<cherrypi::DummyTacticsModule>(output);
  if (behavior == "attack_move") {
    addModule<SquadCombatAttackMove>(output);
  } else if (behavior == "closest") {
    addModule<SquadCombatAttackClosest>(output);
  } else if (behavior == "weakest") {
    addModule<SquadCombatAttackWeakest>(output);
  } else if (behavior == "squad") {
    addModule<cherrypi::SquadCombatModule>(output);
  } else {
    throw std::runtime_error("Unexpected behavior: " + behavior);
  }
  return output;
}
