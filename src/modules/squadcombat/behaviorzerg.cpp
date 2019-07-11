/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "agent.h"
#include "behavior.h"
#include "common/rand.h"
#include "fmt/format.h"
#include "modules/cherryvisdumper.h"
#include "squadtask.h"
#include <autogradpp/autograd.h>

namespace cherrypi {

DEFINE_int32(
    consume_energy_cap,
    150,
    "Cast Consume only if energy is below this cap");

MicroAction BehaviorAsZergling::onPerform(Agent& agent) {
  auto& task = agent.task;
  State* state = agent.state;
  Unit* unit = agent.unit;
  Unit* target = agent.target;

  if (unit->type != buildtypes::Zerg_Zergling) {
    return pass;
  }

  for (auto u : unit->beingAttackedByEnemies) {
    if (u->type == buildtypes::Terran_Vulture_Spider_Mine) {
      // Suicide into enemy units
      Unit* target = utils::getBestScoreCopy(
          task->targets_,
          [&](Unit* target) {
            if (target->type == buildtypes::Terran_Vulture_Spider_Mine ||
                target->flying()) {
              return kdInfty;
            }
            return utils::distance(unit, target) -
                (target->type->mineralCost + target->type->gasCost) / 16;
          },
          kdInfty);
      if (target) {
        auto pos = Vec2(unit) + (Vec2(target) - Vec2(unit)).normalize() * 10;
        return doAction(agent.moveTo(pos));
      }
    }
  }

  if (target && agent.targetInRange) {
    return doAction(agent.attack(target));
  }

  if (target == nullptr || agent.targetInRange) {
    return pass;
  }

  for (Unit* u : unit->unitsInSightRange) {
    if (u->type == buildtypes::Terran_Vulture_Spider_Mine) {
      if (u->attackingTarget) {
        if (utils::distance(unit, u) > 16) {
          continue;
        }
        if (unit != u->attackingTarget) {
          auto pos = Vec2(unit) +
              (Vec2(unit) - Vec2(u->attackingTarget)).normalize() * 10;
          return doAction(agent.moveTo(pos));
        }
      }
    }
  }

  Unit* u = unit;

  // Consider running away from Vultures.
  if (target->visible && target->type == buildtypes::Terran_Vulture &&
      utils::distance(u, target) > DFOASG(8, 4)) {
    int lings = 0;
    int nonLings = 0;
    bool anyInRange = false;
    for (Unit* ally : unit->allyUnitsInSightRange) {
      if (ally->type == buildtypes::Zerg_Zergling) {
        ++lings;
      } else {
        ++nonLings;
        if (target->inRangeOf(ally)) {
          anyInRange = true;
          break;
        }
      }
    }
    if (!anyInRange && lings < DFOASG(8, 4) &&
        nonLings >= lings / DFOASG(2, 1)) {
      Unit* moveToAlly = utils::getBestScoreCopy(
          unit->allyUnitsInSightRange,
          [&](Unit* ally) {
            float d = utils::distance(u, ally);
            if (ally->unit.groundRange <= 12) {
              return kfInfty;
            }
            return d;
          },
          kfInfty);
      if (moveToAlly) {
        if (utils::distance(u, moveToAlly) <= 8) {
          auto pos = Vec2(unit) + (Vec2(unit) - Vec2(target)).normalize() * 10;
          return doAction(agent.moveTo(pos));
        }
        return doAction(agent.moveTo(Position(moveToAlly)));
      }
    }
  }

  auto targetDistanceBB = utils::distanceBB(unit, target);
  auto canMoveInDirection = [&](Vec2 dir,
                                float distance = DFOASG(4.0f * 2, 4.0f)) {
    dir = dir.normalize();
    for (float d = 4.0f; d <= distance; d += 4.0f) {
      Position pos = Position(unit->posf() + dir * d);
      auto* tile = state->tilesInfo().tryGetTile(pos.x, pos.y);
      if (!tile || !tile->entirelyWalkable || tile->building) {
        return false;
      }
    }
    return true;
  };

  if (target->type != buildtypes::Zerg_Zergling &&
      canMoveInDirection(target->posf() - unit->posf(), targetDistanceBB)) {
    // Try to surround the target when we have multiple zerglings -
    // The 2 zerglings nearest to the target can attack directly.
    // The other zerglings will move in the direction from the nearest
    // zergling to the target, until/unless they are on the other side
    // of the target.
    std::array<std::pair<Unit*, float>, std::max((int)DFOASG(2, 1), 1)>
        nearestN;
    nearestN.fill({nullptr, kfInfty});
    for (Unit* u : agent.task->squadUnits()) {
      if (u->type == buildtypes::Zerg_Zergling) {
        float d = utils::distanceBB(u, target);
        for (size_t i = 0; i != nearestN.size(); ++i) {
          if (d < nearestN[i].second) {
            for (size_t i2 = nearestN.size() - 1; i2 != i; --i2) {
              nearestN[i2] = nearestN[i2 - 1];
            }
            nearestN[i] = {u, d};
            break;
          }
        }
      }
    }
    Position moveToPos = kInvalidPosition;
    if (targetDistanceBB > nearestN.back().second) {
      Unit* nearest = nearestN.front().first;
      if (nearest &&
          utils::distance(unit, nearest) <= DFOASG(4.0f * 10, 4.0f * 4)) {
        Vec2 targetpos =
            target->posf() + target->velocity() * state->latencyFrames();
        Vec2 myrel = targetpos - unit->posf();
        Vec2 nrel = targetpos - nearest->posf();

        if (myrel.dot(nrel) > 0) {
          float moveDist = std::min(
              targetDistanceBB + DFOASG(4.0f, 4.0f), DFOASG(12.0f, 8.0f));
          if (canMoveInDirection(nrel, moveDist)) {
            moveToPos = Position(unit) + Position(nrel.normalize() * moveDist);
          }
        }
      }
    }

    if (moveToPos != kInvalidPosition) {
      return doAction(agent.moveTo(moveToPos, false));
    }
  }

  if (target->visible &&
      (target->type->isWorker || target->type == buildtypes::Terran_Vulture)) {
    auto shouldMoveTo = [&](Vec2& newPos) {
      int n = (int)(utils::distance(Position(target), Position(newPos)) / 4.0f);
      Vec2 step = (newPos - target->posf()).normalize() * 4;
      Vec2 pos = target->posf();
      for (int i = 0; i != n; ++i) {
        if (utils::distance(Position(pos), Position(u)) < DFOASG(8, 4)) {
          return false;
        }
        const Tile* tile =
            state->tilesInfo().tryGetTile((int)pos.x, (int)pos.y);
        if (!tile || !tile->entirelyWalkable) {
          return false;
        }
        pos += step;
      }
      return true;
    };

    if (target->topSpeed >= u->topSpeed * 0.66f && target->moving() &&
        !target->inRangeOf(u, DFOASG(4, 2))) {
      const float latency = state->latencyFrames() + DFOASG(0.0f, 2.0f);
      float weaponRange =
          target->flying() ? u->unit.groundRange : u->unit.airRange;
      auto targetVelocity = target->velocity();
      auto targetNextPos = target->posf() + targetVelocity * latency;
      auto myNextPos = u->posf() + u->velocity() * latency;
      float distance = std::min(
          utils::distanceBB(u, myNextPos, target, targetNextPos),
          utils::distanceBB(u, u->posf(), target, targetNextPos));
      if (distance > weaponRange) {
        float distance = utils::distance(u->x, u->y, target->x, target->y);
        if (utils::distance(u->x, u->y, targetNextPos.x, targetNextPos.y) >
            distance) {
          auto np =
              u->posf() + targetVelocity.normalize() * DFOASG(16.0f, 8.0f);
          if (shouldMoveTo(np)) {
            return doAction(agent.moveTo(np));
          }
        } else {
          auto np = target->posf() +
              targetVelocity.normalize() *
                  std::min(
                      std::max(
                          distance - DFOASG(4.0f, 4.0f), DFOASG(4.0f, 4.0f)),
                      DFOASG(20.0f, 8.0f));
          if (shouldMoveTo(np)) {
            return doAction(agent.moveTo(np));
          } else {
            auto np = target->posf() +
                targetVelocity.normalize() *
                    std::min(
                        std::max(
                            distance - DFOASG(4.0f, 4.0f), DFOASG(4.0f, 4.0f)),
                        DFOASG(12.0f, 8.0f));
            if (shouldMoveTo(np)) {
              return doAction(agent.moveTo(np));
            }
          }
        }
      }
    }
  }

  return pass;
}

MicroAction BehaviorAsMutaliskMicro::onPerform(Agent& agent) {
  State* state = agent.state;
  Unit* unit = agent.unit;
  Unit* target = agent.target;
  auto* task = agent.task;

  if (unit->type != buildtypes::Zerg_Mutalisk) {
    return pass;
  }

  if (!target) {
    return pass;
  }

  int latency = state->latencyFrames();

  Vec2 myPos = unit->posf() + unit->velocity() * latency;
  Vec2 targetPos = target->posf() + target->velocity() * latency;
  float range = (float)unit->rangeAgainst(target);

  double cd = unit->cd();
  if (state->currentFrame() - agent.lastAttack < latency) {
    cd += 30;
  }
  float distance = utils::distanceBB(unit, myPos, target, targetPos);
  if (cd <= latency && agent.targetInRange) {
    return pass;
  }

  bool dodgeSplash = false;
  bool anyThreats = false;
  for (Unit* u : agent.legalTargets) {
    if (u->canAttack(unit) &&
        utils::distanceBB(unit, u) <=
            u->rangeAgainst(unit) + DFOASG(4 * 5, 8)) {
      anyThreats = true;
    }
    if (u->type == buildtypes::Terran_Valkyrie ||
        u->type == buildtypes::Protoss_Corsair ||
        u->type == buildtypes::Protoss_Archon ||
        u->type == buildtypes::Protoss_High_Templar) {
      if (utils::distance(unit, u) <= 4 * 8) {
        dodgeSplash = true;
        break;
      }
    }
  }

  auto attackVector = [&]() {
    if (!dodgeSplash) {
      return targetPos;
    }
    Vec2 adjustment;
    for (Unit* u : task->squadUnits()) {
      if (u != unit && u->flying()) {
        float distance =
            std::max(utils::distanceBB(unit, u), DFOASG(0.125f, 1.0f));
        float maxDistance = DFOASG(4 * 3, 6);
        if (distance <= maxDistance) {
          adjustment += (myPos - u->posf()) * (maxDistance / distance);
        }
      }
    }
    Vec2 moveDir = (targetPos - myPos).normalize();
    if (adjustment == Vec2()) {
      adjustment = moveDir;
    }
    return unit->posf() +
        (moveDir + moveDir + adjustment.normalize()).normalize() * 12.0f;
  };

  auto kiteVector = [&]() {
    Vec2 adjustment;
    for (Unit* u : agent.legalTargets) {
      if (u->canAttack(unit)) {
        float distance =
            std::max(utils::distanceBB(unit, u), DFOASG(0.125f, 1.0f));
        float maxDistance = DFOASG(4 * 10, 4 * 3);
        if (distance <= maxDistance) {
          adjustment += (myPos - u->posf()) * (maxDistance / distance);
        }
      }
    }
    Vec2 moveDir = (myPos - targetPos).normalize();
    return unit->posf() + (moveDir + adjustment.normalize()).normalize() * 12.0;
  };

  auto willMoveIntoDanger = [&]() {
    if (!anyThreats) {
      return false;
    }
    Vec2 attackPos = targetPos + (myPos - targetPos).normalize() * range;
    for (Unit* u : agent.legalTargets) {
      if (u != target && u->canAttack(unit) &&
          utils::distance(u->pos(), Position(attackPos)) <=
              u->rangeAgainst(unit) + 6.0f) {
        if (u->velocity().length() < DFOASG(0.125, 0.25) ||
            u->velocity().dot(u->posf() - unit->posf()) <= 0) {
          return true;
        }
      }
    }
    return false;
  };

  if ((!anyThreats && !dodgeSplash) ||
      (target->velocity().length() >= DFOASG(0.125, 0.25) &&
       target->velocity().dot(target->posf() - unit->posf()) > 0)) {
    if (!willMoveIntoDanger()) {
      if (cd <= latency) {
        return pass;
      }
      if (distance > DFOASG(2, 2)) {
        return doAction(agent.moveTo(attackVector()));
      } else {
        return pass;
      }
    }
  } else if (target->velocity().length() < DFOASG(0.25, 0.25)) {
    if (target->topSpeed >= unit->topSpeed * DFOASG(0.66, 0.33)) {
      if (!willMoveIntoDanger()) {
        range /= DFOASG(4, 3);
      }
    }
  }
  float tr = 128.0f / tc::BW::data::TurnRadius[unit->type->unit];
  if ((distance - range) / unit->topSpeed + tr < cd) {
    return doAction(agent.moveTo(kiteVector()));
  }

  return doAction(agent.moveTo(attackVector()));
}

MicroAction BehaviorAsMutaliskVsScourge::onPerform(Agent& agent) {
  auto state = agent.state;
  auto unit = agent.unit;

  if (unit->type != buildtypes::Zerg_Mutalisk) {
    return pass;
  }

  if (agent.target == nullptr ||
      agent.target->type != buildtypes::Zerg_Scourge || !agent.targetInRange) {
    return pass;
  }

  auto u = agent.target;
  auto cd = unit->cd();
  auto scourgeVelo = u->velocity();
  auto myVelo = unit->velocity();
  auto dirToScourge = (Vec2(u) - Vec2(unit)).normalize();
  if (VLOG_IS_ON(2)) {
    utils::drawCircle(
        state, unit, unit->unit.airRange * tc::BW::XYPixelsPerWalktile);
  }

  scourgeVelo.normalize();
  myVelo.normalize();

  auto distBB = utils::distanceBB(u, unit);
  if (agent.mutaliskTurning ||
      (cd < 3 && distBB > 3 && myVelo.dot(dirToScourge) > 0)) {
    VLOG(3) << utils::unitString(unit) << " is launching a scourge attack ";
    utils::drawCircle(state, u, 25, tc::BW::Color::Red);
    agent.mutaliskTurning = false;
    return doAction(agent.attack(u));
  } else if (cd < 6 && distBB > 8) {
    VLOG(3) << utils::unitString(unit) << " is turning to face unit";
    utils::drawCircle(state, u, 25, tc::BW::Color::Red);
    agent.mutaliskTurning = true;
    return doAction(agent.moveTo(Vec2(unit) + dirToScourge * 20));
  } else if (myVelo.dot(scourgeVelo) < 0.1 || !u->atTopSpeed()) {
    VLOG(3) << utils::unitString(unit) << " is moving away from the scourge";
    return doAction(agent.moveTo(Vec2(unit) + dirToScourge * -20));
  } else {
    // http://liquipedia.net/starcraft/Mutalisk_vs._Scourge_Control#Method_2
    auto pos1 = Vec2(unit) + scourgeVelo.rotateDegrees(100) * 20;
    auto pos2 = Vec2(unit) + scourgeVelo.rotateDegrees(-200) * 20;
    auto pos = pos1.distanceTo(u) < pos2.distanceTo(u) ? pos2 : pos1;
    utils::drawCircle(state, unit, 25, tc::BW::Color::Blue);
    VLOG(3) << utils::unitString(unit)
            << " is using the triangle technique and moving to dir "
            << scourgeVelo;
    return doAction(agent.moveTo(pos));
  }
  return pass;
}

MicroAction BehaviorAsScourge::onPerform(Agent& agent) {
  auto& task = agent.task;
  auto unit = agent.unit;

  if (unit->type != buildtypes::Zerg_Scourge) {
    return pass;
  }

  if (agent.target == nullptr) {
    if (!unit->threateningEnemies.empty()) {
      auto centroid = utils::centerOfUnits(unit->threateningEnemies);
      auto pos = Vec2(unit) + (Vec2(unit) - Vec2(centroid)).normalize() * 10;
      return doAction(agent.moveTo(pos));
    } else {
      return doAction(agent.moveTo(task->center_));
    }
  }
  // Scourges wants to click past the target so to move at full speed, and
  // issue an attack command when they are right on top of the target.
  auto invalidUnit = [&](Unit const* u) {
    if (u->type == buildtypes::Protoss_Interceptor ||
        u->type == buildtypes::Zerg_Overlord || u->type->isBuilding) {
      return true;
    }
    auto it = task->enemyStates_->find(u);
    if (it == task->enemyStates_->end()) {
      return true;
    }
    if (u != agent.target &&
        it->second.damages > u->unit.health + u->unit.shield - 15) {
      return true;
    }
    return false;
  };
  if (invalidUnit(agent.target)) {
    agent.target = nullptr;
    for (auto u : unit->enemyUnitsInSightRange) {
      if (!invalidUnit(u)) {
        agent.target = u;
        break;
      }
    }
  }
  if (agent.target == nullptr) {
    return doNothing;
  }
  if (agent.target->inRangeOf(unit, 3)) {
    return doAction(agent.attack(agent.target));
  }
  auto dir = Vec2(agent.target) - Vec2(unit);
  dir.normalize();
  return doAction(agent.moveTo(Vec2(unit) + dir * 25));
}

constexpr int kLurkerBurrowFrames = 24; // Magic: 0+
constexpr int kLurkerUnburrowFrames = 12; // Magic: 0+
MicroAction BehaviorAsLurker::onPerform(Agent& agent) {
  if (agent.unit->type != buildtypes::Zerg_Lurker) {
    return pass;
  }

  // Adapted from:
  // https://github.com/dgant/PurpleWave/blob/master/src/Micro/Actions/Combat/Decisionmaking/Root.scala

  constexpr int attackRange = 6 * tc::BW::XYWalktilesPerBuildtile;
  constexpr int forever = 24 * 60;

  auto* lurker = agent.unit;
  auto* target = agent.target;
  auto* state = agent.state;
  auto& task = agent.task;
  auto& threats = task->threats_;
  auto& targets = task->targets_;
  bool burrowed = lurker->burrowed();

  auto framesToCloseGap = [&](double distance, double speed) {
    if (distance < 0) {
      return 0;
    }
    if (speed <= 0) {
      return forever;
    }
    return static_cast<int>(ceil(distance / speed));
  };

  auto shouldPredict = [&](Unit* unit) {
    return burrowed || unit->type != buildtypes::Terran_Vulture;
  };

  int framesBeforeBeingDetected = [&]() {
    int output = forever;
    for (auto& threat : threats) {
      if (threat->type->isDetector) {
        double radius = (threat->type->isBuilding ? 7 : 11) *
            tc::BW::XYWalktilesPerBuildtile;
        double distance =
            utils::distance(Position(lurker), Position(threat)) - radius;
        double speed =
            lurker->topSpeed + (shouldPredict(threat) ? threat->topSpeed : 0.0);
        output = std::min(output, framesToCloseGap(distance, speed));
      }
    }
    return output;
  }();

  bool inTankRange = false;
  int framesBeforeThreatIsInRange = [&]() {
    int output = forever;
    for (auto& threat : threats) {
      bool canAttackUs = threat->type->hasGroundWeapon ||
          threat->type == buildtypes::Protoss_Reaver ||
          threat->type == buildtypes::Terran_Bunker;
      if (canAttackUs) {
        double range = threat->rangeAgainst(lurker);
        double distance = utils::distanceBB(lurker, threat) - range;
        double speed = threat->topSpeed;
        double frames = framesToCloseGap(distance, speed);
        output = std::min(output, int(frames));
        inTankRange = inTankRange ||
            (frames <= 0 &&
             (threat->type == buildtypes::Terran_Siege_Tank_Siege_Mode ||
              threat->type == buildtypes::Terran_Siege_Tank_Tank_Mode));
      }
    }
    return output;
  }();

  int framesBeforeTargetIsInRange = [&]() {
    if (target == nullptr) {
      return forever;
    }
    auto predictedPosition = utils::predictPosition(
        target, shouldPredict(target) ? kLurkerBurrowFrames : 0);
    auto distanceNow =
        utils::distanceBB(lurker, Position(lurker), target, predictedPosition);
    auto distanceFromRange = distanceNow - attackRange -
        3; // Let the target come in a little so we don't just barely miss
    return framesToCloseGap(distanceFromRange, target->topSpeed);
  }();
  auto targetsInRange =
      std::count_if(targets.begin(), targets.end(), [&](auto maybeTarget) {
        return !maybeTarget->flying() && task->isImportantTarget(maybeTarget) &&
            utils::distanceBB(lurker, maybeTarget) <= attackRange;
      });

  bool protectingBase = [&]() {
    auto& lurkerArea = state->areaInfo().getArea(Position(lurker));
    auto& buildings = state->unitsInfo().myBuildings();
    return buildings.end() !=
        std::find_if(
               buildings.begin(), buildings.end(), [&](Unit* const neighbor) {
                 return utils::distance(Position(neighbor), Position(lurker)) <
                     attackRange ||
                     std::addressof(state->areaInfo().getArea(
                         Position(neighbor))) == std::addressof(lurkerArea);
               });
  }();

  int framesToMove =
      kLurkerBurrowFrames + (burrowed ? kLurkerUnburrowFrames : 0);
  bool detected = framesBeforeBeingDetected < framesToMove;
  bool threatened = framesBeforeThreatIsInRange <= framesToMove;
  bool outOfRange = framesBeforeTargetIsInRange > kLurkerBurrowFrames;
  bool wantsToFight = agent.wantsToFight || !detected || lurker->irradiated();
  double distanceFromTarget = target ? utils::distanceBB(lurker, target) : 1024;
  bool nearingTarget = target && distanceFromTarget < 12;
  bool farFromTarget = !target || distanceFromTarget > 16;

  bool mustBeUnburrowed = detected && threatened && outOfRange;
  bool mustNotUnburrow =
      targetsInRange || (inTankRange && !detected && !wantsToFight);
  bool mustNotBurrow = !wantsToFight && !protectingBase;
  bool wantToBurrow =
      nearingTarget || (threatened && targetsInRange > 0 && !detected);
  bool wantToUnburrow =
      // Prepared to face consequences of unburrowing
      (!threatened || agent.wantsToFight)
      // Has motivation to unburrow
      && (!targetsInRange ||
          (detected && lurker->cd() > lurker->maxCdGround() / 2)) &&
      (framesBeforeTargetIsInRange >
           kLurkerBurrowFrames + kLurkerUnburrowFrames ||
       farFromTarget);

  bool shouldBurrow =
      !burrowed && !mustNotBurrow && !mustBeUnburrowed && wantToBurrow;
  bool shouldUnburrow = burrowed && !mustNotUnburrow &&
      (mustBeUnburrowed || (wantToUnburrow && !wantToBurrow));

  auto logState = [&]() {
    VLOG(3) << fmt::format(
        "Lurker: detected:{0} threatened:{1} outOfRange:{2} "
        "agent.wantsToFight{3} wantsToFight:{4} nearingTarget:{5} "
        "farFromTarget:{6} shouldBurrow:{7}, shouldUnburrow:{8}",
        detected,
        threatened,
        outOfRange,
        agent.wantsToFight,
        wantsToFight,
        nearingTarget,
        farFromTarget,
        shouldBurrow,
        shouldUnburrow);
  };

  if (shouldBurrow) {
    agent.postCommand(tc::BW::UnitCommandType::Burrow);
    logState();
    return doNothing;
  }
  if (shouldUnburrow) {
    agent.postCommand(tc::BW::UnitCommandType::Unburrow);
    logState();
    return doNothing;
  }
  if (wantsToFight && target != nullptr) {
    return doAction(agent.moveTo(Position(target)));
  }

  return pass;
}

MicroAction BehaviorAsHydralisk::onPerform(Agent& agent) {
  State* state = agent.state;
  Unit* unit = agent.unit;
  Unit* target = agent.target;

  if (unit->type != buildtypes::Zerg_Hydralisk) {
    return pass;
  }

  if (!target) {
    return pass;
  }

  if (agent.prevTargetInRange && !agent.targetInRange &&
      unit->velocity() == Vec2()) {
    agent.postCommand(tc::BW::UnitCommandType::Stop);
    return doNothing;
  }

  int latency = state->latencyFrames();

  Vec2 myPos = unit->posf() + unit->velocity() * latency;
  Vec2 targetPos = target->posf() + target->velocity() * latency;
  float range = (float)unit->rangeAgainst(target);
  float distance = utils::distanceBB(unit, myPos, target, targetPos);

  double cd = unit->cd();
  if (state->currentFrame() - agent.lastAttack < latency) {
    cd += 15;
  }

  auto willMoveIntoDanger = [&]() {
    Vec2 attackPos = targetPos + (myPos - targetPos).normalize() * range;
    for (Unit* u : agent.legalTargets) {
      if (u != target && u->canAttack(unit) &&
          utils::distance(u->pos(), Position(attackPos)) <=
              u->rangeAgainst(unit) + 6.0f) {
        if (u->velocity().length() < DFOASG(0.125, 0.25) ||
            u->velocity().dot(u->posf() - unit->posf()) <= 0) {
          return true;
        }
      }
    }
    return false;
  };

  auto canMoveInDirection = [&](Vec2 dir,
                                float distance = DFOASG(4.0f * 2, 4.0f)) {
    dir = dir.normalize();
    for (float d = 4.0f; d <= distance; d += 4.0f) {
      Position pos = Position(unit->posf() + dir * d);
      auto* tile = state->tilesInfo().tryGetTile(pos.x, pos.y);
      if (!tile || !tile->entirelyWalkable || tile->building) {
        return false;
      }
    }
    return true;
  };

  if (target->velocity().length() >= DFOASG(0.125, 0.25) &&
      target->velocity().dot(target->posf() - unit->posf()) > 0) {
    if (!willMoveIntoDanger()) {
      if (unit->topSpeed > target->topSpeed &&
          distance > std::max(range - 6.0f, 4.0f) &&
          canMoveInDirection(targetPos - myPos)) {
        return doAction(agent.moveTo(targetPos));
      }
      if (cd <= latency) {
        return pass;
      }
      if (distance > DFOASG(6, 3)) {
        return doAction(agent.moveTo(targetPos));
      } else {
        return pass;
      }
    }
  }

  float targetRange = target->rangeAgainst(unit);

  if (agent.targetInRange && targetRange < range &&
      (distance <= targetRange + 12.0f || target->topSpeed >= unit->topSpeed)) {
    float tr = 128.0f / tc::BW::data::TurnRadius[unit->type->unit];
    if (cd <= latency + tr) {
      return doAction(agent.attack(target));
    }
    auto kiteVector = [&]() {
      Vec2 adjustment;
      for (Unit* u : agent.legalTargets) {
        if (u->canAttack(unit)) {
          float distance =
              std::max(utils::distanceBB(unit, u), DFOASG(0.125f, 1.0f));
          float maxDistance = DFOASG(4 * 10, 4 * 3);
          if (distance <= maxDistance) {
            adjustment += (myPos - u->posf()) * (maxDistance / distance);
          }
        }
      }
      Vec2 moveDir = (myPos - targetPos).normalize();
      return unit->posf() +
          (moveDir + adjustment.normalize()).normalize() * 6.0;
    };
    return doAction(agent.moveTo(kiteVector()));
  } else if (
      distance <= range + 4.0f && distance > range - 4.0f &&
      targetRange > range) {
    if (cd <= latency) {
      return pass;
    }
    return doAction(agent.moveTo(targetPos));
  }
  return pass;
}

MicroAction BehaviorAsDefilerConsumeOnly::onPerform(Agent& agent) {
  auto* defiler = agent.unit;

  if (defiler->type != buildtypes::Zerg_Defiler) {
    return pass;
  }
  auto key = fmt::format("defiler_{}_consume", agent.unit->id);
  bool canConsume = agent.state->board()->get<bool>(key, true);
  if (!canConsume) {
    return pass;
  }

  std::unordered_map<Unit*, float> unitValues;
  auto consumeValue = [&](Unit* const target) {
    if (!target->isMine) {
      return -1.;
    }
    static const std::unordered_map<const BuildType*, double> consumeScore = {
        {buildtypes::Zerg_Zergling, 1.0}};
    auto it = consumeScore.find(target->type);
    double value = 0.0;
    if (it == consumeScore.end()) {
      value = -1.;
    } else {
      value = it->second / std::max(1.f, utils::distance(target, agent.unit));
    }
    return value;
  };
  auto energy = agent.unit->unit.energy;

  if (energy < FLAGS_consume_energy_cap) {
    auto upc = agent.tryCastSpellOnUnit(buildtypes::Consume, consumeValue, 0.0);
    if (upc) {
      auto [unit, pos] = upc->positionUArgMax();
      CVIS_LOG_UNIT(agent.state, defiler)
          << "Energy: " << energy << " - trying to consume " << unit
          << unitValues;
      return doAction(upc);
    } else {
      CVIS_LOG_UNIT(agent.state, defiler)
          << "No target to consume - task has "
          << agent.task->squadUnits().size() << " units" << unitValues;
    }
  }

  return pass;
}

MicroAction BehaviorAsDefilerMoveToBattle::onPerform(Agent& agent) {
  auto* unit = agent.unit;
  auto& task = agent.task;

  if (unit->type != buildtypes::Zerg_Defiler) {
    return pass;
  }

  Unit* nearestThreat = utils::getBestScoreCopy(
      agent.legalTargets,
      [&](Unit* u) {
        if (!u->canAttack(unit) || u->flying()) {
          return kfInfty;
        }
        return utils::distanceBB(unit, u) - (float)u->rangeAgainst(unit);
      },
      kfInfty);

  Unit* nearestTarget = utils::getBestScoreCopy(
      agent.legalTargets,
      [&](Unit* u) {
        if (u->flying() || u->unit.groundRange < 4.0f) {
          return kfInfty;
        }
        return utils::distance(unit->pos(), u->pos());
      },
      kfInfty);

  Unit* nearestAlly = utils::getBestScoreCopy(
      task->squadUnits(),
      [&](Unit* u) {
        if (u->flying() || !u->type->hasGroundWeapon) {
          return kfInfty;
        }
        return utils::distance(unit->pos(), u->pos());
      },
      kfInfty);

  float maxSafeRange = 4 * 7.0f;
  if (nearestThreat) {
    auto nearbyCloserAllyUnits = 0;
    auto distToNearestThreat = utils::distanceBB(unit, nearestThreat);
    for (auto u : task->squadUnits()) {
      if (u->flying() || !u->type->hasGroundWeapon) {
        continue;
      }
      if (utils::distanceBB(u, nearestThreat) < distToNearestThreat) {
        ++nearbyCloserAllyUnits;
      }
    }
    if (nearbyCloserAllyUnits > 5) {
      maxSafeRange -= 4;
    }
    if (nearbyCloserAllyUnits > 10) {
      maxSafeRange -= 4;
    }

    if (distToNearestThreat <=
        std::min(
            maxSafeRange, float(nearestThreat->rangeAgainst(unit)) + 8.0f)) {
      auto kiteVector = [&]() {
        Vec2 adjustment;
        for (Unit* u : agent.legalTargets) {
          if (u->canAttack(unit)) {
            float distance =
                std::max(utils::distanceBB(unit, u), DFOASG(0.125f, 1.0f));
            float maxDistance = DFOASG(4 * 14, 4 * 3);
            if (distance <= maxDistance) {
              adjustment +=
                  (unit->posf() - u->posf()) * (maxDistance / distance);
            }
          }
        }
        Vec2 targetVec;
        if (nearestAlly) {
          targetVec += (nearestAlly->posf() - unit->posf()).normalize();
        }
        if (nearestTarget) {
          targetVec += (nearestTarget->posf() - unit->posf()).normalize();
        }
        if (targetVec != Vec2()) {
          adjustment = adjustment.normalize() + targetVec.normalize() * 0.5f;
        }
        return unit->posf() + adjustment.normalize() * 12.0;
      };
      auto dest = Position(kiteVector());
      CVIS_LOG_UNIT(agent.state, unit)
          << "nearestThreat=" << nearestThreat
          << " is close (distanceBB=" << utils::distanceBB(unit, nearestThreat)
          << ", maxSafeRange=" << maxSafeRange
          << ", nearbyCloserAllyUnits=" << nearbyCloserAllyUnits
          << ") - fleeing to " << dest;
      return doAction(agent.moveTo(dest));
    }
  }
  if (nearestAlly && utils::distanceBB(unit, nearestAlly) > 4.0f * 6) {
    CVIS_LOG_UNIT(agent.state, unit)
        << "nearestAlly=" << nearestAlly
        << " is too far away - helping (maxSafeRange=" << maxSafeRange
        << ", nearestThreat=" << nearestThreat << ")";
    return doAction(agent.moveTo(nearestAlly->pos()));
  } else if (nearestTarget) {
    CVIS_LOG_UNIT(agent.state, unit)
        << "nearestTarget=" << nearestTarget
        << " is close - engaging (maxSafeRange=" << maxSafeRange
        << ", nearestThreat=" << nearestThreat << ")";
    return doAction(agent.moveTo(nearestTarget->pos()));
  }

  return pass;
}

MicroAction BehaviorAsDefiler::onPerform(Agent& agent) {
  auto* defiler = agent.unit;

  if (defiler->type != buildtypes::Zerg_Defiler) {
    return pass;
  }

  // The cast range is 9, but we don't necessarily want to dive
  double range = 4 * (agent.wantsToFight ? 7 : 5);
  auto plagueValue = [&](Unit* const target) {
    if (target->plagued())
      return 0.0;
    if (target->unit.max_health <= 0)
      return 0.0;
    return target->type->subjectiveValue * target->unit.health /
        (target->unit.max_health + target->unit.max_shield) *
        (target->isEnemy ? 1.0 : -1.0) * range /
        std::max(range, double(utils::distance(defiler, target)));
  };
  auto swarmValue = [&](Unit* const target) {
    if (target->underDarkSwarm())
      return 0.0;
    if (!target->type->restrictedByDarkSwarm)
      return 0.0;

    return target->type->subjectiveValue * (target->isEnemy ? 1.0 : -1.0) *
        range / std::max(range, double(utils::distance(defiler, target)));
  };

  auto energy = agent.unit->unit.energy;
  if (energy >= 150) {
    auto upc = agent.tryCastSpellOnArea(
        buildtypes::Plague,
        16,
        16,
        plagueValue,
        3 * buildtypes::Zerg_Zergling->subjectiveValue);
    if (upc)
      return doAction(upc);
  }
  if (energy >= 100) {
    auto upc = agent.tryCastSpellOnArea(
        buildtypes::Dark_Swarm,
        24,
        24,
        swarmValue,
        3 * buildtypes::Zerg_Zergling->subjectiveValue,
        [&](auto p) {
          return p.project(defiler->pos(), agent.wantsToFight ? 16 : 8);
        });
    if (upc)
      return doAction(upc);
  }
  if (energy < FLAGS_consume_energy_cap) {
    return BehaviorAsDefilerConsumeOnly::onPerform(agent);
  }

  return pass;
}

MicroAction BehaviorAsOverlord::onPerform(Agent& agent) {
  auto& task = agent.task;
  auto unit = agent.unit;

  if (unit->type != buildtypes::Zerg_Overlord) {
    return pass;
  }

  // Stay away from other Overlords when there are no threats nearby.
  // This prevents Corsairs from murdering all our Overlords at once.
  Unit* repellant = nullptr;
  {
    auto enemies = unit->threateningEnemies;
    repellant = enemies.empty() ? nullptr : enemies[0];
    if (repellant == nullptr) {
      auto allies = unit->allyUnitsInSightRange;
      // Corsair max splash range = 100 pixels = 12.5 walktiles, then add a
      // bit of margin
      auto closestOverlordDistance = 15.0;
      std::for_each(allies.begin(), allies.end(), [&](Unit* ally) {
        if (ally != unit && ally->type == unit->type) {
          auto distance = utils::distance(unit, ally);
          if (distance < closestOverlordDistance) {
            repellant = ally;
            closestOverlordDistance = distance;
          }
        }
      });
    }
  }
  if (repellant != nullptr) {
    auto dir = Vec2(unit) - Vec2(repellant);
    dir.normalize();
    VLOG(3) << unit << " spreads away from " << repellant;
    return doAction(agent.smartMove(Position(Vec2(unit) + dir * 25)));
  }

  VLOG(3) << unit << " has no purpose in life, following the group";
  return doAction(agent.smartMove(task->center_));
}

} // namespace cherrypi
