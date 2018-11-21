/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "blackboard.h"
#include "modules/gatherer/gathererc.h"
#include "movefilters.h"
#include "utils.h"

#include <bwem/map.h>
#include <common/rand.h>
#include <fmt/format.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

DEFINE_double(
    gatherer_bastion_distance,
    DFOASG(40, 20),
    "Distance from a base position to look for defensive bastions");

DEFINE_double(
    gatherer_max_pull_distance,
    DFOASG(200, 50),
    "Maximum distance to pull workers against proxies");

DEFINE_double(
    gatherer_max_invader_fight_distance,
    DFOASG(16, 8),
    "Maximum distance to pull workers against invaders");

DEFINE_double(
    gatherer_invader_scan_distance,
    DFOASG(60, 20),
    "Distance from a base position to look for invaders");

DEFINE_double(
    gatherer_invader_flee_distance,
    DFOASG(12, 4),
    "Distance from an invader under which we flee while on cooldown");

DEFINE_double(
    gatherer_proxy_window,
    DFOASG(10080, 3600), // 10080 = 24 * 60 * 7
    "Number of frames into the game to consider pulling workers for a proxy");

DEFINE_double(
    gatherer_proxy_distance,
    DFOASG(150, 50),
    "Distance from a base position to look for proxies");

DEFINE_double(
    gatherer_proxier_distance,
    DFOASG(200, 50),
    "Distance from a base position to look for proxy builders");

DEFINE_double(
    gatherer_cannon_leash,
    DFOASG(24, 12),
    "Once an attacking proxy is complete, don't pull workers unless within "
    "this range");

DEFINE_double(
    gatherer_fearless_move,
    DFOASG(120, 60),
    "Distance within with workers ignore enemies en route to their resource");

DEFINE_double(
    gatherer_avoid_range,
    DFOASG(48.0, 24.0),
    "Distance from enemies for gatherers to maintain while transferring");

DEFINE_double(
    gatherer_burrow_hp,
    DFOASG(0, 0.0),
    "Burrow threatened workers below this HP");

namespace cherrypi {

constexpr double kInvalid = std::numeric_limits<double>::infinity();

inline auto order(Unit* worker) {
  return worker->unit.orders.front();
}

void vlog(int level, Unit* worker, Unit* resource, std::string&& logTemplate) {
  VLOG(level) << fmt::format(
      "{} (O {} to i{} @ ({}, {}) since f{}) {} {}",
      utils::unitString(worker),
      order(worker).type,
      order(worker).targetId,
      order(worker).targetX,
      order(worker).targetY,
      order(worker).first_frame,
      logTemplate,
      utils::unitString(resource));
}

auto comparableProxy(Unit* unit) {
  return std::make_tuple(
      !unit->type->hasGroundWeapon,
      !unit->completed(),
      unit->unit.health + unit->unit.shield,
      unit->firstSeen);
};

void GathererController::step(State* state) {
  // Update state
  assignments.step(state);
  proxyBuilders.clear();
  proxies.clear();
  invaders.clear();
  bastions.clear();

  // Find base positions
  std::vector<Position> basePositions;
  for (auto& base : state->areaInfo().myBases()) {
    if (base.resourceDepot) {
      basePositions.push_back(base.resourceDepot->pos());
    }
  }
  // Finds incomplete bases too
  for (auto* unit : state->unitsInfo().myBuildings()) {
    if (!unit->completed() && unit->type->isResourceDepot) {
      [&]() {
        for (auto& area : state->areaInfo().areas()) {
          for (auto& base : area.baseLocations) {
            // 2 taken from AreaInfo definition
            if (utils::distance(base, {unit}) <= 2) {
              basePositions.push_back(base);
              return;
            }
          }
        }
      }();
    }
  }
  
  // Track our defensive bastions, which we might want to protect with workers
  for (Unit* unit : state->unitsInfo().myUnits()) {
    if (unit->type->hasGroundWeapon && !unit->type->isWorker &&
        !unit->flying()) {
      for (auto& position : basePositions) {
        if (utils::distance({unit}, position) <
            FLAGS_gatherer_bastion_distance) {
          bastions.push_back(unit);
        }
      }
    }
  }

  // A major goal for our gatherers is to prevent the enemy from constructing
  // buildings -- "proxies" -- in our base.
  //
  // We want to tear down proxied enemy buildings, and make sure they aren't
  // hiding any out of view.
   
  // Track proxies and proxy builders  
  auto inOurBase = [&](Unit* unit, float distance) {
    for (auto& basePosition : basePositions) {
      if (utils::distance(basePosition, unit) < distance &&
          state->areaInfo().walkPathLength(basePosition, unit->pos()) <
              distance) {
        return true;
      }
    }
    return false;
  };
  bool enemyHasCombatUnits = false;
  bool enemyHasCompletedScaryProxies = false;
  for (Unit* enemy : state->unitsInfo().enemyUnits()) {
    auto eType = enemy->type;
    if (eType->hasGroundWeapon && !eType->isWorker) {
      if (!eType->isBuilding) {
        enemyHasCombatUnits = true;
      } else if (enemy->completed()) {
        enemyHasCompletedScaryProxies = true;
      }
    }
    if (enemy->gone && state->currentFrame() - enemy->lastSeen >= 24 * 2) {
      continue;
    }
    if (state->currentFrame() < FLAGS_gatherer_proxy_window) {
      if (enemy->type->isBuilding &&
          inOurBase(enemy, FLAGS_gatherer_proxy_distance)) {
        proxies.push_back(enemy);
        wasProxied = true;
      }
      if (wasProxied && enemy->type->isWorker &&
          inOurBase(enemy, FLAGS_gatherer_proxier_distance)) {
        proxyBuilders.push_back(enemy);
      }
    }
    if (enemy->type->hasGroundWeapon && !enemy->type->isBuilding &&
        inOurBase(enemy, FLAGS_gatherer_invader_scan_distance)) {
      invaders.push_back(enemy);
    }
  }

  VLOG_IF(2, basePositions.size() > 5) << "Bases: " << basePositions.size();
  VLOG_IF(1, !invaders.empty()) << "Invaders: " << invaders.size();
  VLOG_IF(1, !proxies.empty()) << "Proxies: " << proxies.size();
  VLOG_IF(1, !proxyBuilders.empty()) << "Proxy builders: "
                                     << proxyBuilders.size();
  
  // Track workers who aren't yet assigned to defense or gathering
  std::unordered_set<Unit*> freeWorkers;
  for (auto& pair : assignments.workers) {
    freeWorkers.insert(pair.first);
  }

  // Prioritize proxies in ascending order of importance
  std::sort(proxies.begin(), proxies.end(), [&](Unit* a, Unit* b) {
    return comparableProxy(a) < comparableProxy(b);
  });
  // Raze the proxy with an appropriate number of workers
  auto canSafelyApproach = [&](Unit* worker, Unit* enemy) {
    // Don't enter proxy attacker range if we're not already in it
    return proxies.end() ==
        std::find_if(proxies.begin(), proxies.end(), [&](Unit* proxy) {

             if (enemyHasCombatUnits &&
                 proxy->type != buildtypes::Zerg_Creep_Colony &&
                 proxy->type != buildtypes::Zerg_Sunken_Colony) {
               return false;
             }
             if (!proxy->type->hasGroundWeapon || !proxy->completed()) {
               return false;
             }
             if (utils::distance({proxy}, {enemy}) >
                 FLAGS_gatherer_cannon_leash) {
               return false;
             }
             return true;
           });
  };
  for (Unit* proxy : proxies) {
    int workersRequired = 0;
    if (proxy->type == buildtypes::Protoss_Photon_Cannon) {
      workersRequired = 4;
    } else if (proxy->type == buildtypes::Zerg_Creep_Colony) {
      workersRequired = 3;
    } else if (proxy->type == buildtypes::Zerg_Sunken_Colony) {
      workersRequired = 3;
    } else if (proxy->type == buildtypes::Protoss_Pylon) {
      // Keep vision on the Pylon, just in case they try to add any Cannons later
      workersRequired = 1;
    }
    while (workersRequired > 0) {
      auto* razer = utils::getBestScoreCopy(
          freeWorkers,
          [&](auto* razer) {
            double distance = utils::distanceBB(razer, proxy);
            if (!canSafelyApproach(razer, proxy)) {
              return kInvalid;
            }
            if (distance > FLAGS_gatherer_max_pull_distance) {
              return kInvalid;
            }
            return distance;
          },
          kInvalid);
      if (razer) {
        VLOG(2) << fmt::format(
            "{} razes {}", utils::unitString(razer), utils::unitString(proxy));
        --workersRequired;
        freeWorkers.erase(razer);
        attack(state, razer, proxy);
      } else {
        break;
      }
    }
  }

  // Chase proxy builders to prevent proxy placement and to keep an eye on them
  // so they don't hide any proxies in fog of war
  if (!enemyHasCompletedScaryProxies) {
    for (Unit* proxyBuilder : proxyBuilders) {
      auto* chaser = utils::getBestScoreCopy(
          freeWorkers,
          [&](auto* chaser) {
            if (!canSafelyApproach(chaser, proxyBuilder)) {
              return kInvalid;
            }
            double distance = utils::distanceBB(chaser, proxyBuilder);
            return distance > FLAGS_gatherer_max_pull_distance ? kInvalid
                                                               : distance;
          },
          kInvalid);
      if (chaser) {
        VLOG(2) << fmt::format(
            "{} chases {}",
            utils::unitString(chaser),
            utils::unitString(proxyBuilder));
        freeWorkers.erase(chaser);
        chase(state, chaser, proxyBuilder);
      }
    }
  }

  // Micro the remaining workers individually
  for (auto& resourcePair : assignments.resourceWorkers) {
    Unit* resource = resourcePair.first;
    for (Unit* worker : resourcePair.second) {
      if (utils::contains(freeWorkers, worker)) {
        micro(state, worker, resource);
      }
    }
  }
  SharedController::postUpcs(state);
}

void GathererController::micro(State* state, Unit* worker, Unit* resource) {
  
  // Should this worker run away?
  double distance = utils::distance({worker}, {resource});
  bool shouldFlee = distance > FLAGS_gatherer_fearless_move &&
      worker->enemyUnitsInSightRange.end() !=
          std::find_if(
              worker->enemyUnitsInSightRange.begin(),
              worker->enemyUnitsInSightRange.end(),
              [&](Unit* enemy) {
                return enemy->type->hasGroundWeapon && !enemy->type->isWorker;
              });
  if (shouldFlee) {
    flee(state, worker, resource);
    return;
  }

  // Are we under attack and have researched Burrow?
  // Duck for cover!
  if (state->hasResearched(buildtypes::Burrowing) &&
      worker->unit.health < FLAGS_gatherer_burrow_hp) {
    bool hideUnderBed = state->areaInfo().myBases().size() > 1 &&
        invaders.end() != std::find_if(
                              invaders.begin(),
                              invaders.end(),
                              [&](Unit* invader) {
                                return worker->inRangeOf(invader, 24);
                              }) &&
        invaders.end() ==
            std::find_if(invaders.begin(), invaders.end(), [](Unit* invader) {
              return invader->type->isDetector;
            });
    if (hideUnderBed) {
      VLOG(1) << "Burrowing " << utils::unitString(worker);
      state->board()->postCommand(
          tc::Client::Command(
              tc::BW::Command::CommandUnit,
              worker->id,
              tc::BW::UnitCommandType::Burrow),
          kRootUpcId);
      return;
    } else if (worker->burrowed()) {
      VLOG(1) << "Unburrowing " << utils::unitString(worker);
      state->board()->postCommand(
          tc::Client::Command(
              tc::BW::Command::CommandUnit,
              worker->id,
              tc::BW::UnitCommandType::Unburrow),
          kRootUpcId);
      return;
    }
  }

  // Fight invaders if:
  // * They're in our face, or
  // * They're threatening a nearby bastion
  Unit* invader = utils::getBestScoreCopy(
      invaders,
      [&](Unit* invader) {
        double output = std::max(3.f, utils::distanceBB(worker, invader));
        return output > FLAGS_gatherer_max_invader_fight_distance
            ? kInvalid
            : (output + 0.001 * (invader->unit.health + invader->unit.shield));
      },
      kInvalid);
  if (invader) {
    int lf = state->latencyFrames();
    double invaderDistance = utils::distanceBB(worker, invader);
    if (worker->canAttack(invader)) {
      // If we're on cooldown, shoot any invaders in range

      if (worker->cd() < 4 + lf && invader->inRangeOf(worker, 4 + lf)) {
        VLOG(2) << fmt::format(
            "{} pokes {}",
            utils::unitString(worker),
            utils::unitString(invader));
        attack(state, worker, invader);
        return;
      }

      // If we're near a defensive bastion (like a Sunken Colony) protect it
      if (worker->unit.health > 16) {
        double multiplier = invader->type->isWorker ? 3 : 1;
        Unit* bastion = utils::getBestScoreCopy(
            bastions,
            [&](Unit* bastion) {
              double distance =
                  multiplier * utils::distanceBB(bastion, invader);
              return distance > FLAGS_gatherer_max_invader_fight_distance
                  ? kInvalid
                  : distance;
            },
            kInvalid);
        if (bastion) {
          double bastionDistance = utils::distanceBB(bastion, invader);
          if (bastionDistance <= 4 + multiplier * invaderDistance) {
            VLOG(2) << fmt::format(
                "{} defends {} against {}",
                utils::unitString(worker),
                utils::unitString(bastion),
                utils::unitString(invader));
            attack(state, worker, invader);
            return;
          }
        }
      }
    }

    // If we're being attacked by a Zealot or Zergling, let's flee and maybe
    // glitch them out
    bool invaderIsScary = invader->type == buildtypes::Zerg_Zergling ||
        invader->type == buildtypes::Protoss_Zealot;
    if (invaderIsScary &&
        invaderDistance < FLAGS_gatherer_invader_flee_distance) {
      // Find a resource to mineral walk to
      auto* area = state->areaInfo().tryGetArea(worker->pos());
      if (area) {
        Unit* mineral =
            utils::getBestScoreCopy(area->minerals, [&](Unit* mineral) {
              return utils::distanceBB(worker, mineral) -
                  1.2 * utils::distanceBB(invader, mineral);
            });
        if (mineral) {
          VLOG(2) << fmt::format(
              "{} mineral walks to {} from {}",
              utils::unitString(worker),
              utils::unitString(mineral),
              utils::unitString(invader));
          gather(state, worker, mineral, true);
          return;
        }
      }
    }
  }

  gather(state, worker, resource);
}

/// Issue a UPC to command a worker to gather a resource.
void GathererController::gather(
    State* state,
    Unit* worker,
    Unit* resource,
    bool dropResources) {
  
  // Should this worker return cargo?
  bool shouldReturn = dropResources
      ? (resource->type->isGas ? worker->carryingGas()
                               : worker->carryingMinerals())
      : worker->carryingResources();
  if (shouldReturn) {
    if (order(worker).type != tc::BW::Order::ReturnMinerals &&
        order(worker).type != tc::BW::Order::ReturnGas) {
      vlog(5, worker, resource, "returns cargo en route to ");
      addUpc(worker, resource, Command::ReturnCargo);
    } else {
      vlog(5, worker, resource, "is already returning cargo en route to ");
    }
    return;
  }
  
  // If the resource is far away or invisible, move to it instead of trying to gather.
  auto moveTo = movefilters::pathMoveTo(state, worker, resource->pos());
  bool resourceFar = utils::distance({resource}, moveTo) >= 30;
  if (resourceFar || !resource->visible || !resource->completed()) {
    addUpc(worker, moveTo, Command::Move);
    vlog(4, worker, resource, "moves to resource");
    return;
  }
  
  // Do we need to issue a new gather command?
  bool shouldCommand = [&]() {
    if (order(worker).targetId != resource->id) {
      return true;
    } else if (utils::distanceBB(worker, resource) > 4) {
      return true;
    }
    return false;
  }();

  if (shouldCommand) {
    addUpc(worker, resource, Command::Gather);
    vlog(4, worker, resource, "gathers from");
  } else {
    vlog(5, worker, resource, "is already gathering");
  }
}


/// Issue a UPC to command a worker to flee.
void GathererController::flee(State* state, Unit* worker, Unit* resource) {
  auto filter = movefilters::PositionFilters(
      {movefilters::makePositionFilter(
          movefilters::getCloserTo(resource),
          {movefilters::avoidAttackers(),
           movefilters::avoidThreatening(),
           movefilters::avoidEnemyUnitsInRange(FLAGS_gatherer_avoid_range)})});
  addUpc(worker, movefilters::smartMove(state, worker, filter), Command::Move);
}

/// Issue a UPC to command a worker to chase an enemy proxy builder, to ensure
/// that they don't do anything sneaky out of our vision.
void GathererController::chase(State* state, Unit* worker, Unit* target) {
  if (target->inRangeOf(worker)) {
    attack(state, worker, target);
  } else {
    addUpc(worker, utils::predictPosition(target, 24), Command::Move);
  }
}

/// Issue a UPC to command a worker to attack a unit.
void GathererController::attack(State* state, Unit* worker, Unit* target) {
  if (target->visible) {
    if (order(worker).targetId != target->id) {
      addUpc(worker, target, Command::Delete);
    }
  } else {
    addUpc(worker, target->pos(), Command::Move);
  }
}

} // namespace cherrypi
