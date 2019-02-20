/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "behavior.h"
#include "agent.h"
#include "common/rand.h"
#include "squadtask.h"

namespace cherrypi {

DEFINE_bool(behavior_chase, false, "Toggles chasing behaviors");
DEFINE_bool(behavior_kite, false, "Toggles kiting behaviors");
void Behavior::perform(Agent& agent) {
  if (!agent.currentAction.isFinal) {
    agent.currentAction = onPerform(agent);
  }
}

MicroAction BehaviorSeries::onPerform(Agent& agent) {
  for (auto& behavior : behaviors_) {
    behavior->perform(agent);
  }
  return agent.currentAction;
}

MicroAction BehaviorML::onPerform(Agent& agent) {
  for (auto& model : *(agent.task->models)) {
    auto action = model->decode(agent.unit);
    if (action.isFinal) {
      return action;
    }
  }
  return pass;
}

MicroAction BehaviorUnstick::onPerform(Agent& agent) {
  if (agent.stuckFrames < agent.unstickTriggerFrames || agent.unit->flying()) {
    return pass;
  }
  VLOG(3) << utils::unitString(agent.unit) << " is unsticking";
  agent.postCommand(tc::BW::UnitCommandType::Stop);
  return doNothing;
}

MicroAction BehaviorIfIrradiated::onPerform(Agent& agent) {
  auto* unit = agent.unit;

  if (unit->irradiated()) {
    std::vector<Unit*> units;
    for (auto u : unit->allyUnitsInSightRange) {
      if (utils::distance(u, unit) < 16) {
        units.push_back(u);
      }
    }
    if (!units.empty()) {
      auto centroid = Vec2(utils::centerOfUnits(units));
      auto pos = Vec2(unit) + (Vec2(unit) - Vec2(centroid)).normalize() * 10;
      return doAction(agent.moveTo(pos));
    }
  }
  return pass;
}

MicroAction BehaviorIfStormed::onPerform(Agent& agent) {
  auto& task = agent.task;
  auto* unit = agent.unit;

  for (auto stormLoc : task->storms_) {
    if (utils::distance(unit, stormLoc) > 16)
      continue;
    auto pos = Vec2(unit) + (Vec2(unit) - Vec2(stormLoc)).normalize() * 10;
    return doAction(agent.moveTo(pos));
  }
  return pass;
}

MicroAction BehaviorVsScarab::onPerform(Agent& agent) {
  auto* unit = agent.unit;
  for (auto u : unit->beingAttackedByEnemies) {
    if (u->type == buildtypes::Protoss_Scarab) {
      auto pos = Vec2(unit) + (Vec2(unit) - Vec2(u)).normalize() * 10;
      return doAction(agent.moveTo(pos));
    }
  }
  return pass;
}

MicroAction BehaviorTravel::onPerform(Agent& agent) {
  auto& task = agent.task;
  auto* unit = agent.unit;

  if (task->targetingLocation) {
    return doAction(agent.moveTo(Position(task->targetX, task->targetY)));
  } else if (unit->threateningEnemies.empty()) {
    return doAction(agent.smartMove(task->center_));
  }
  return pass;
}

MicroAction BehaviorLeave::onPerform(Agent& agent) {
  auto* unit = agent.unit;

  if (unit->threateningEnemies.empty()) {
    return pass;
  }

  auto enemyCenter = utils::centerOfUnits(unit->threateningEnemies);
  auto fleePosition = (Vec2(unit) - Vec2(enemyCenter)) * 15;
  return doAction(agent.smartMove(Position(fleePosition)));
}

constexpr int kChaseHpThreshold = 20; // Magic: 0+
constexpr double kChaseDelProbThreshold = 1.0; // Magic: 0-1
constexpr double kChaseOvershoot = 4.0; // Magic: 0+
constexpr double kChaseLookahead = 4.0; // Magic: 0+
MicroAction BehaviorChase::onPerform(Agent& agent) {
  auto& task = agent.task;
  State* state = agent.state;
  Unit* unit = agent.unit;
  Unit* target = agent.target;

  if (!FLAGS_behavior_chase) {
    return pass;
  }
  if (unit->type == buildtypes::Zerg_Lurker) {
    return pass;
  }
  if (target == nullptr || target->gone) {
    return pass;
  }
  if (unit->unit.health < kChaseHpThreshold) {
    return pass;
  }
  if (task->delProb < kChaseDelProbThreshold) {
    return pass;
  }
  if (target->topSpeed <= 0 && !unit->threateningEnemies.empty()) {
    return pass;
  }

  VLOG(2) << utils::unitString(unit) << " chases " << utils::unitString(target);

  bool readyToShoot = unit->cd() <= state->latencyFrames();
  bool inRange = target->inRangeOf(unit);
  if (readyToShoot && inRange) {
    return doAction(agent.attack(target));
  } else {
    double overshoot = target->topSpeed > 0 ? kChaseOvershoot : 0;
    auto destination = Vec2(target) +
        (Vec2(target) - Vec2(unit)).normalize() * overshoot +
        target->velocity() * kChaseLookahead;
    return doAction(agent.moveTo(Position(destination)));
  }
}

constexpr int kKiteHpThreshold = 20; // Magic: 0+
constexpr double kKiteDelProbThreshold = 0.85; // Magic: 0-1
constexpr double kKiteRatioDefault = 0.25; // Magic: 0+
constexpr double kKiteRatioPunish = 1.0; // Magic: 0+
constexpr double kKiteRatioFallback = 0.5; // Magic: 0+
constexpr double kKiteRatioBreathe = 0.5; // Magic: 0+
constexpr int kKiteFrameMargin = 3; // Magic: 0+
constexpr int kKiteRangeMargin = 4; // Magic: 0+
constexpr float kKiteFleeDistance = 60; // Magic: 48+
MicroAction BehaviorKite::onPerform(Agent& agent) {
  auto& task = agent.task;
  State* state = agent.state;
  Unit* unit = agent.unit;
  Unit* target = agent.target;

  if (!FLAGS_behavior_kite) {
    return pass;
  }
  if (unit->type == buildtypes::Zerg_Lurker) {
    return pass;
  }
  if (unit->threateningEnemies.empty()) {
    return pass;
  }
  if (unit->unit.health > kKiteHpThreshold &&
      task->delProb > kKiteDelProbThreshold) {
    return pass;
  }
  if (std::max(unit->unit.groundRange, unit->unit.airRange) < 12) {
    return pass;
  }

  if (target == nullptr || target->gone) {
    return pass;
  }
  double kiteRange = unit->rangeAgainst(target);
  double kiteCd = unit->maxCdAgainst(target);

  // Punish:
  // * Hover at the edge of our range.
  // * Use if we outrange and outspeed them.
  // * Use even if retreating.
  //
  // Fallback:
  // * Shoot while retreating.
  // * Use if we outrange but don't outspeed them.
  // * Use only if retreating.
  //
  // Breathe:
  // * Back out while on cooldown (catching our breath)
  // * Use if this helps us trade more effectively
  // * Use only if fighting.
  //
  int countThreats = 0;
  int countCanPunish = 0;
  int countCanFallback = 0;
  int countCanBreathe = 0;
  for (auto* enemy : unit->threateningEnemies) {
    if (enemy->type == buildtypes::Terran_Siege_Tank_Siege_Mode ||
        enemy->type == buildtypes::Protoss_Reaver) {
      // Don't get cute while eating splash damage.
      return pass;
    }

    double rangeAgainstUs = enemy->rangeAgainst(unit);
    double maxCdAgainstUs = enemy->maxCdAgainst(unit);

    bool canPunish = true;
    bool canFallback = !agent.wantsToFight && target != nullptr;
    bool canBreathe = agent.wantsToFight;
    if (rangeAgainstUs >= kiteRange || enemy->topSpeed >= unit->topSpeed) {
      canPunish = false;
    }
    if (rangeAgainstUs >= kiteRange) {
      canFallback = false;
    }
    if (unit->topSpeed > enemy->topSpeed) {
      canFallback = false;
    }
    if (rangeAgainstUs * maxCdAgainstUs > kiteCd * task->delProb) {
      canBreathe = false;
    }
    countThreats += 1;
    countCanPunish += canPunish ? 1 : (agent.wantsToFight ? 0 : -1);
    countCanFallback += canFallback ? 1 : 0;
    countCanBreathe += canBreathe ? 1 : 0;
  }
  if (countThreats <= 0) {
    return pass;
  }
  double valueDefault = countThreats * kKiteRatioDefault;
  double valuePunish = countCanPunish * kKiteRatioPunish;
  double valueFallback = countCanFallback * kKiteRatioFallback;
  double valueBreathe = countCanBreathe * kKiteRatioBreathe;
  double valueBest = std::max(
      valueDefault,
      std::max(valuePunish, std::max(valueFallback, valueBreathe)));

  if (valueDefault >= valueBest) {
    return pass;
  }

  bool readyToShoot = unit->cd() < state->latencyFrames();
  double cdEffective = std::max(unit->cd(), (double)state->latencyFrames());
  double targetDistanceNow = utils::pxDistanceBB(unit, target);

  // Project distance at time we fire
  // Allow some time to turn
  double targetDistanceProjected =
      targetDistanceNow + target->topSpeed * (cdEffective + kKiteFrameMargin);
  bool targetEscaped = targetDistanceProjected > kiteRange;
  bool targetEscaping = targetDistanceProjected > kiteRange - kKiteRangeMargin;

  auto attack = [&]() { return doAction(agent.attack(target)); };
  auto runAway = [&]() {
    return doAction(agent.filterMove({movefilters::avoidThreatening()}));
  };

  if (valueFallback >= valueBest) {
    VLOG(2) << utils::unitString(unit) << " falls back from "
            << utils::unitString(target);

    if (readyToShoot) {
      return attack();
    }
    // The default behavior will have us flee; do so.
    return pass;
  } else if (valuePunish >= valueBest) {
    VLOG(2) << utils::unitString(unit) << " punishes "
            << utils::unitString(target);

    if ((readyToShoot && targetEscaping) || targetEscaped) {
      return attack();
    }
    return runAway();
  } else if (valueBreathe >= valueBest) {
    VLOG(2) << utils::unitString(unit) << " catches breath against "
            << utils::unitString(target);

    if (readyToShoot || targetEscaped) {
      return attack();
    }
    return runAway();
  }

  return pass;
}

MicroAction BehaviorFormation::onPerform(Agent& agent) {
  if (!agent.targetInRange && agent.formationPosition != kInvalidPosition) {
    return doAction(agent.moveTo(agent.formationPosition));
  }

  return pass;
}

MicroAction BehaviorEngageCooperatively::onPerform(Agent& agent) {
  Unit* unit = agent.unit;
  Unit* target = agent.target;
  State* state = agent.state;
  auto* task = agent.task;

  if (target == nullptr || unit->flying() || unit->burrowed()) {
    return pass;
  }

  // Avoid situations where some unit is stuck behind some other unit
  // and unable to attack the target.
  // Do this by simply moving the unit in front forwards between attacks.
  // This is most noticable with hydralisks attacking a static target
  // like cannons.

  if (unit->rangeAgainst(target) >= 8 &&
      target->inRangeOf(agent.unit, state->latencyFrames() + DFOASG(6, 3)) &&
      unit->cd() <= state->latencyFrames()) {
    Vec2 myPos = unit->posf();
    Vec2 targetPos = target->posf();
    Vec2 targetVector = targetPos - myPos;

    // TODO: check that we actually *can* move forward
    for (Unit* u : task->squadUnits()) {
      if ((myPos - u->posf()).dot(targetVector) >= 0 &&
          utils::distanceBB(u, unit) <= DFOASG(3.0f, 1.5f) &&
          !target->inRangeOf(u) &&
          u->rangeAgainst(target) >= unit->rangeAgainst(target)) {
        Vec2 moveTo = myPos + targetVector.normalize() * DFOASG(6, 3);
        return doAction(agent.moveTo(moveTo, false));
      }
    }
  }

  // Dodge splash!

  bool dodgeSplash = false;
  for (Unit* u : agent.legalTargets) {
    if (u->type == buildtypes::Terran_Valkyrie ||
        u->type == buildtypes::Protoss_Corsair ||
        u->type == buildtypes::Protoss_Archon ||
        u->type == buildtypes::Protoss_High_Templar ||
        u->type == buildtypes::Terran_Firebat ||
        u->type == buildtypes::Terran_Siege_Tank_Siege_Mode ||
        u->type == buildtypes::Zerg_Lurker ||
        u->type == buildtypes::Protoss_Reaver ||
        u->type == buildtypes::Terran_Vulture_Spider_Mine) {
      float range = u->rangeAgainst(unit);
      if (u->type == buildtypes::Protoss_High_Templar ||
          u->type == buildtypes::Protoss_Reaver) {
        range = 4.0f * 9;
      }
      if (u->type == buildtypes::Terran_Vulture_Spider_Mine) {
        range = 4.0f * 2;
      }
      if (u->canAttack(unit) &&
          utils::distanceBB(unit, u) <= range + DFOASG(6, 3)) {
        dodgeSplash = true;
        break;
      }
    }
  }
  if (dodgeSplash &&
      (unit->rangeAgainst(target) >= 8 || !agent.targetInRange) &&
      (!target->inRangeOf(unit, state->latencyFrames() + DFOASG(6, 3)) ||
       unit->cd() > state->latencyFrames())) {
    int latency = state->latencyFrames();

    Vec2 myPos = unit->posf() + unit->velocity() * latency;
    Vec2 targetPos = target->posf() + target->velocity() * latency;

    auto canMoveInDirection = [&](Vec2 dir,
                                  float distance = DFOASG(4.0f * 2, 4.0f)) {
      dir = dir.normalize();
      for (float d = 4.0f; d <= distance; d += 4.0f) {
        Position pos{myPos + dir * d};
        auto* tile = state->tilesInfo().tryGetTile(pos.x, pos.y);
        if (!tile || !tile->entirelyWalkable || tile->building) {
          return false;
        }
      }
      return true;
    };

    if (canMoveInDirection(
            targetPos - myPos,
            utils::distance(Position(myPos), Position(targetPos)))) {
      auto attackVector = [&]() {
        Vec2 adjustment;
        for (Unit* u : task->squadUnits()) {
          if (u != unit && u->flying() == unit->flying()) {
            float distance =
                std::max(utils::distanceBB(unit, u), DFOASG(0.125f, 1.0f));
            float maxDistance = DFOASG(4 * 3, 6);
            if (distance <= maxDistance) {
              adjustment += (myPos - u->posf()) * (maxDistance / distance);
            }
          }
        }
        if (adjustment == Vec2()) {
          return Vec2(kInvalidPosition);
        }
        Vec2 moveDir = (targetPos - myPos).normalize();
        return unit->posf() +
            (moveDir + moveDir + adjustment.normalize()).normalize() * 12.0f;
      };

      auto moveTo = attackVector();
      if (moveTo != Vec2(kInvalidPosition) &&
          canMoveInDirection(Vec2(moveTo) - myPos)) {
        utils::drawLine(state, unit, Position(moveTo));
        return doAction(agent.moveTo(Position(moveTo)));
      }
    }
  }

  if (!agent.targetInRange && agent.formationPosition != kInvalidPosition) {
    return doAction(agent.moveTo(agent.formationPosition));
  }

  return pass;
}

MicroAction BehaviorEngage::onPerform(Agent& agent) {
  State* state = agent.state;
  Unit* target = agent.target;
  Unit* unit = agent.unit;

  if (target == nullptr) {
    return pass;
  }

  VLOG(3) << utils::unitString(agent.unit) << " engages "
          << utils::unitString(target);

  bool issueAttack =
      target->inRangeOf(unit, state->latencyFrames() + DFOASG(6, 3));

  Vec2 myPos = unit->posf() + unit->velocity() * state->latencyFrames();
  Vec2 targetPos = target->posf() + target->velocity() * state->latencyFrames();
  if ((targetPos - myPos).dot(target->posf() - unit->posf()) < 0) {
    issueAttack = true;
  }

  if (issueAttack) {
    // Send attack command if we're in range or aren't already attacking
    // the target
    if (agent.lastMove > 0 || agent.attacking != target) {
      return doAction(agent.attack(target));
    } else {
      return doNothing;
    }
  } else {
    return doAction(agent.smartMove(Position(targetPos)));
  }
  return pass;
}

} // namespace cherrypi
