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
CPI_DEFINE_BEHAVIOR(TargetClosestStationary)
CPI_DEFINE_BEHAVIOR(TargetClosestStationaryBuffer5)
CPI_DEFINE_BEHAVIOR(TargetClosestStationaryBuffer7)
CPI_DEFINE_BEHAVIOR(TargetClosestStationaryBuffer10)
CPI_DEFINE_BEHAVIOR(TargetClosestStationaryUntilOpponent)
CPI_DEFINE_BEHAVIOR(SimpleAttackTarget)
CPI_DEFINE_BEHAVIOR(SimpleAttackTargetStationary)
CPI_DEFINE_BEHAVIOR(SimpleAttackTargetStationaryUntilOpponent)
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

MicroAction BehaviorTargetClosestStationaryBuffer5::onPerform(Agent& agent) {
  auto* unit = agent.unit;
  auto& enemies = agent.state->unitsInfo().enemyUnits();
  agent.target = utils::getBestScoreCopy(
      enemies, [&](Unit* target) { return utils::distanceBB(unit, target); });
  auto dist = utils::distanceBB(agent.unit, agent.target);
  auto range =
      agent.unit->type->isFlyer ? agent.unit->unit.airRange : agent.unit->unit.groundRange;

  if (dist < range + 10.0) {
    return pass;
  }
  return doNothing;
}

MicroAction BehaviorTargetClosestStationaryBuffer7::onPerform(Agent& agent) {
  auto* unit = agent.unit;
  auto& enemies = agent.state->unitsInfo().enemyUnits();
  agent.target = utils::getBestScoreCopy(
      enemies, [&](Unit* target) { return utils::distanceBB(unit, target); });
  auto dist = utils::distanceBB(agent.unit, agent.target);
  auto range =
      agent.unit->type->isFlyer ? agent.unit->unit.airRange : agent.unit->unit.groundRange;

  if (dist < range + 7.5) {
    return pass;
  }
  return doNothing;
}

MicroAction BehaviorTargetClosestStationaryUntilOpponent::onPerform(Agent& agent) {
  auto* unit = agent.unit;
  auto& enemies = agent.state->unitsInfo().enemyUnits();
  agent.target = utils::getBestScoreCopy(
      enemies, [&](Unit* target) { return utils::distanceBB(unit, target); });
  auto& allies = agent.state->unitsInfo().myUnits();
  for (auto& ally : allies) {
    if (ally->attacking()) {
      return pass;
    }
  }
  return doNothing;
}

MicroAction BehaviorTargetClosestStationaryBuffer10::onPerform(Agent& agent) {
  auto* unit = agent.unit;
  auto& enemies = agent.state->unitsInfo().enemyUnits();
  agent.target = utils::getBestScoreCopy(
      enemies, [&](Unit* target) { return utils::distanceBB(unit, target); });
  auto dist = utils::distanceBB(agent.unit, agent.target);
  auto range =
      agent.unit->type->isFlyer ? agent.unit->unit.airRange : agent.unit->unit.groundRange;

  if (dist < range + 10.0) {
    return pass;
  }
  return doNothing;
}

MicroAction BehaviorTargetClosestStationary::onPerform(Agent& agent) {
  auto* unit = agent.unit;
  auto& enemies = agent.state->unitsInfo().enemyUnits();
  agent.target = utils::getBestScoreCopy(
      enemies, [&](Unit* target) { return utils::distanceBB(unit, target); });
  return doNothing;
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
MicroAction BehaviorSimpleAttackTargetStationary::onPerform(Agent& agent) {
  auto* target = agent.target;
  if (target) {
    if (agent.unit->idle() || agent.attacking != target) {
      return doAction(agent.attack(target));
    }
  }
  return doNothing;
}

MicroAction BehaviorSimpleAttackTargetStationaryUntilOpponent::onPerform(Agent& agent) {
  auto* target = agent.target;
  if (target) {
    if (agent.unit->idle() || agent.attacking != target) {
      return doAction(agent.attack(target));
    }
  }
  auto& allies = agent.state->unitsInfo().myUnits();
  for (auto& ally : allies) {
    if (ally->attacking()) {
      return pass;
    }
  }
  return doNothing;
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
        std::make_shared<cherrypi::BehaviorTargetClosest>(),
        std::make_shared<cherrypi::BehaviorSimpleAttackTarget>()};
  }
  virtual cherrypi::BehaviorList makeFleeBehaviors() override {
    return makeDeleteBehaviors();
  }
};

class SquadCombatAttackClosestStationaryBuffer10 : public cherrypi::SquadCombatModule {
  virtual cherrypi::BehaviorList makeDeleteBehaviors() override {
    return cherrypi::BehaviorList{
        std::make_shared<cherrypi::BehaviorTargetClosestStationaryBuffer10>(),
        std::make_shared<cherrypi::BehaviorSimpleAttackTargetStationary>()};
  }
  virtual cherrypi::BehaviorList makeFleeBehaviors() override {
    return makeDeleteBehaviors();
  }
};

class SquadCombatAttackClosestStationaryBuffer5 : public cherrypi::SquadCombatModule {
  virtual cherrypi::BehaviorList makeDeleteBehaviors() override {
    return cherrypi::BehaviorList{
        std::make_shared<cherrypi::BehaviorTargetClosestStationaryBuffer5>(),
        std::make_shared<cherrypi::BehaviorSimpleAttackTargetStationary>()};
  }
  virtual cherrypi::BehaviorList makeFleeBehaviors() override {
    return makeDeleteBehaviors();
  }
};

class SquadCombatAttackClosestStationaryBuffer7 : public cherrypi::SquadCombatModule {
  virtual cherrypi::BehaviorList makeDeleteBehaviors() override {
    return cherrypi::BehaviorList{
        std::make_shared<cherrypi::BehaviorTargetClosestStationaryBuffer7>(),
        std::make_shared<cherrypi::BehaviorSimpleAttackTargetStationary>()};
  }
  virtual cherrypi::BehaviorList makeFleeBehaviors() override {
    return makeDeleteBehaviors();
  }
};

class SquadCombatAttackClosestStationaryUntilOpponent : public cherrypi::SquadCombatModule {
  virtual cherrypi::BehaviorList makeDeleteBehaviors() override {
    return cherrypi::BehaviorList{
        std::make_shared<cherrypi::BehaviorTargetClosestStationaryUntilOpponent>(),
        std::make_shared<cherrypi::BehaviorSimpleAttackTargetStationaryUntilOpponent>()};
  }
  virtual cherrypi::BehaviorList makeFleeBehaviors() override {
    return makeDeleteBehaviors();
  }
};

class SquadCombatAttackClosestStationary : public cherrypi::SquadCombatModule {
  virtual cherrypi::BehaviorList makeDeleteBehaviors() override {
    return cherrypi::BehaviorList{
        std::make_shared<cherrypi::BehaviorTargetClosestStationary>(),
        std::make_shared<cherrypi::BehaviorSimpleAttackTargetStationary>()};
  }
  virtual cherrypi::BehaviorList makeFleeBehaviors() override {
    return makeDeleteBehaviors();
  }
};

class SquadCombatAttackMove : public cherrypi::SquadCombatModule {
  virtual cherrypi::BehaviorList makeDeleteBehaviors() override {
    return cherrypi::BehaviorList{
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

auto getCombatModules(std::string const& name) {
  std::vector<std::shared_ptr<cherrypi::Module>> output;
  addModule<cherrypi::DummyTacticsModule>(output);
  if (name == "attack_move") {
    addModule<SquadCombatAttackMove>(output);
  } else if (name == "closest") {
    addModule<SquadCombatAttackClosest>(output);
  } else if (name == "weakest") {
    addModule<SquadCombatAttackWeakest>(output);
  } else if (name == "squad") {
    addModule<cherrypi::SquadCombatModule>(output);
  } else if (name == "hold_stationary") {
    addModule<SquadCombatAttackClosestStationary>(output);
  } else if (name == "stationary_buffer5") {
    addModule<SquadCombatAttackClosestStationaryBuffer5>(output);
  } else if (name == "stationary_buffer7.5") {
    addModule<SquadCombatAttackClosestStationaryBuffer7>(output);
  } else if (name == "stationary_buffer10") {
    addModule<SquadCombatAttackClosestStationaryBuffer10>(output);
  } else if (name == "stationary") {
    addModule<SquadCombatAttackClosestStationaryUntilOpponent>(output);
  } else {
    throw std::runtime_error("Unexpected rule: " + name);
  }
  return output;
}
