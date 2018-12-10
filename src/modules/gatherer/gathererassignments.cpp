/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "modules/gatherer/gathererassignments.h"
#include "blackboard.h"
#include "movefilters.h"
#include "utils.h"

#include <bwem/map.h>
#include <common/rand.h>
#include <fmt/format.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

DEFINE_double(
    gatherer_threat_cost,
    DFOASG(50, 50),
    "How much does an unsafe worker transfer impact its perceived cost?");

DEFINE_double(
    gatherer_lookahead,
    DFOASG(24 * 60, 24 * 30),
    "How far in the future to measure gatherer returns");

DEFINE_double(
    gatherer_mining2,
    DFOASG(0.8, 0.2),
    "How effective we think the second worker on a mineral patch will be.");

DEFINE_double(
    gatherer_mining3,
    DFOASG(0.1, 0.1),
    "How effective we think the third worker on a mineral patch will be.");

DEFINE_double(
    gatherer_sticky_distance,
    DFOASG(12, 12),
    "Bonus proximity workers assume to their previously targeted resource");

DEFINE_double(
    gatherer_sticky_multiplier,
    DFOASG(2, 2),
    "Incentive given to workers to stay on their patch when mining");

DEFINE_double(
    gatherer_cooldown,
    DFOASG(72, 48),
    "After reassigning a worker, wait this many frames before re-reassigning "
    "them.");

DEFINE_double(
    gatherer_cooldown_noise,
    DFOASG(48, 48),
    "Random range applied to gatherer_cooldown");

DEFINE_int32(
    gatherer_worker_updates,
    int(DFOASG(15, 15)),
    "Number of workers to update per iteration");

DEFINE_double(
    gatherer_gas_ratio,
    DFOASG(0.4, 0.2),
    "Ideal ratio of gas gatherers");

DEFINE_double(
    gatherer_speed_bias,
    DFOASG(3.0, 2.0),
    "Bias used in measuring mining speed from a resource");

DEFINE_int32(
    gatherer_remove_blocks_at,
    20,
    "Minimum number of workers for removing minerals blocking our bases");

DEFINE_int32(
    gatherer_distance_mineral_threshold,
    7,
    "Allow distance mining if we have fewer than this many mineral patches");

namespace cherrypi {

/// How far a resource can be from our base before it's considered long-distance
/// mining
constexpr double kDistanceMining = 4 * 12;

void GathererAssignments::addUnit(Unit* unit) {
  workers[unit];
}

void GathererAssignments::removeUnit(Unit* unit) {
  workers.erase(unit);
  resourceWorkers.unassignWorker(unit);
}

/// Should we be allowed to gather from this resource?
bool isValidResource(Unit* u) {
  if (!u) {
    return false;
  }
  if (u->dead) {
    return false;
  }
  if (!u->type->isResourceContainer) {
    return false;
  }
  if (u->type->isMinerals) {
    return !u->gone;
  } else {
    return u->isMine && (u->completed() || u->remainingBuildTrainTime < 24 * 2);
  }
}

void GathererAssignments::step(State* state) {
  constexpr double lightYear = 1e10;
  auto& areaInfo = state->areaInfo();

  // Add/remove resources.
  std::vector<Unit*> resourcesToRemove;
  for (auto& pair : resourceWorkers) {
    if (!isValidResource(pair.first)) {
      resourcesToRemove.push_back(pair.first);
    }
  }
  for (Unit* resource : resourcesToRemove) {
    resourceWorkers.excludeResource(resource);
  }
  std::vector<Unit*> newResources;
  for (Unit* u : state->unitsInfo().liveUnits()) {
    if (isValidResource(u)) {
      if (!resourceWorkers.containsResource(u)) {
        newResources.push_back(u);
      }
      resourceWorkers.includeResource(u);
    }
  }

  // Count gas workers.
  int gasWorkersNow = 0;
  for (auto& pair : resourceWorkers) {
    gasWorkersNow += pair.first->type->isGas ? pair.second.size() : 0;
  }

  // Identify base-to-base transfer costs, considering distance and threats.
  using Base = AreaInfo::BaseInfo;
  std::unordered_map<Base const*, std::unordered_map<Base const*, double>>
      baseCosts;
  auto& myBases = areaInfo.myBases();
  for (Base const& a : myBases) {
    for (Base const& b : myBases) {
      if (a.resourceDepot && b.resourceDepot &&
          baseCosts[&a].find(&b) == baseCosts[&a].end()) {
        float cost = 0.0;
        if (&a != &b) {
          auto pathAreas = areaInfo.walkPathAreas(
              a.resourceDepot->pos(), b.resourceDepot->pos(), &cost);
          for (auto* area : pathAreas) {
            if (area->enemyGndStrength > area->myGndStrength) {
              cost *= FLAGS_gatherer_threat_cost;
              break;
            }
          }
        }

        baseCosts[&a][&b] = baseCosts[&b][&a] = cost;
      }
    }
  }

  // Map resources to bases and measure distance.
  std::unordered_map<Unit*, double> depotToResourceDistances;
  std::unordered_map<Unit*, Base const*> resourceBases;
  std::unordered_map<Base const*, std::vector<Unit*>> baseResources;
  for (auto& pair : resourceWorkers) {
    Unit* resource = pair.first;
    Area* area = areaInfo.tryGetArea(Position(resource));
    depotToResourceDistances[resource] = lightYear;
    for (Base const& base : myBases) {
      if (base.resourceDepot && base.area == area) {
        double distance = utils::distanceBB(resource, base.resourceDepot);
        if (distance < depotToResourceDistances[resource]) {
          depotToResourceDistances[resource] = distance;
          resourceBases[resource] = &base;
          baseResources[&base].push_back(resource);
        }
      }
    }
  }

  int totalMineralPatches = 0;
  for (auto& base : myBases) {
    totalMineralPatches += base.area->minerals.size();
  }
  bool canDistanceMine =
      totalMineralPatches < FLAGS_gatherer_distance_mineral_threshold;
  VLOG_IF(1, canDistanceMine) << "Distance mining enabled";

  // Find mineral blockers to remove.
  std::unordered_set<Unit*> mineralBlockersToRemove;
  if (int(workers.size()) > FLAGS_gatherer_remove_blocks_at) {
    for (auto& base : areaInfo.myBases()) {
      for (auto* chokepoint : base.area->area->ChokePoints()) {
        auto* neutralBwem = chokepoint->BlockingNeutral();
        if (neutralBwem) {
          Unit* neutral =
              state->unitsInfo().getUnit(neutralBwem->Unit()->getID());
          if (neutral && neutral->type->isMinerals && !neutral->gone) {
            mineralBlockersToRemove.insert(neutral);
          }
        }
      }
    }
  }

  // Set limits on gas workers
  int gasWorkersMax =
      int(std::round(FLAGS_gatherer_gas_ratio * workers.size()));
  VLOG(1) << "Gas worker target: " << gasWorkersMax;
  const auto* keyMin = Blackboard::kGathererMinGasWorkers;
  const auto* keyMax = Blackboard::kGathererMaxGasWorkers;
  if (state->board()->hasKey(keyMax)) {
    const int boardValue = state->board()->get<int>(keyMax);
    VLOG(4) << keyMax << ": " << boardValue;
    gasWorkersMax = std::min(gasWorkersMax, boardValue);
    VLOG(1) << "Gas workers capped at: " << gasWorkersMax;
  }
  if (state->board()->hasKey(keyMin)) {
    const int boardValue = state->board()->get<int>(keyMin);
    VLOG(4) << keyMin << ": " << boardValue;
    gasWorkersMax = std::max(gasWorkersMax, boardValue);
    VLOG(1) << "Gas workers floored at: " << gasWorkersMax;
  }
  int gasWorkersAbsoluteMax = 0;
  for (auto& pair : resourceWorkers) {
    gasWorkersAbsoluteMax += pair.first->type->isRefinery ? 3 : 0;
  }
  gasWorkersMax = std::min(gasWorkersMax, gasWorkersAbsoluteMax);
  VLOG(1) << "Gas workers FINAL: " << gasWorkersMax;

  VLOG(4) << fmt::format(
      "Gatherer sees {} bases and {} resources for {} gatherers with {}/{} on "
      "gas.",
      baseCosts.size(),
      resourceWorkers.size(),
      workers.size(),
      gasWorkersNow,
      gasWorkersMax);

  // Update workers in priority order.
  //
  // When a new resource is available:
  // Prioritize workers closest to the new resource.
  //
  // Otherwise:
  // Sort workers by frames since update, descending.
  bool respectCooldown = true;
  auto newGasDistance = [&](Unit* worker) {
    float minDistance = 256 * 256 * 1000;
    for (Unit* resource : newResources) {
      minDistance = std::min(minDistance, utils::distanceBB(worker, resource));
    }
    return minDistance;
  };
  // Determine if we need to prioritize updating gas workers.
  std::unordered_map<Unit*, float> minGasDistance;
  if (gasWorkersNow < gasWorkersMax) {
    for (auto& workerPair : workers) {
      Unit* worker = workerPair.first;
      minGasDistance[worker] = 256 * 256 * 1000;
      for (auto resourceWorker : resourceWorkers) {
        Unit* gas = resourceWorker.first;
        if (gas->type->isGas && resourceWorkers.countWorkers(gas) < 3) {
          minGasDistance[worker] =
              std::min(minGasDistance[worker], utils::distanceBB(worker, gas));
        }
      }
    }
  }
  auto workerSorter = [&](Unit* a, Unit* b) {
    if (gasWorkersNow < gasWorkersMax) {
      respectCooldown = false;
      if (newResources.empty()) {
        return minGasDistance[a] < minGasDistance[b];
      }
      return newGasDistance(a) < newGasDistance(b);
    }
    return workers[a].framesSinceUpdate > workers[b].framesSinceUpdate;

  };
  std::vector<Unit*> workersToUpdate;
  for (auto& workerPair : workers) {
    workersToUpdate.push_back(workerPair.first);
  }
  std::sort(workersToUpdate.begin(), workersToUpdate.end(), workerSorter);

  // Update workers in priority order.
  int workerUpdates = 0;
  const int currentFrame = state->currentFrame();
  for (Unit* worker : workersToUpdate) {
    // Cap the number of worker updates (for performance reasons).
    ++workerUpdates;
    if (workerUpdates >= FLAGS_gatherer_worker_updates) {
      ++workers[worker].framesSinceUpdate;
      continue;
      VLOG(5) << fmt::format(
          "Skipping update for {}", utils::unitString(worker));
    }
    if (respectCooldown && workers[worker].cooldownUntil > currentFrame) {
      continue;
    }

    // Don't interrupt workers who are about to reach minerals.
    Unit* resourceBefore = resourceWorkers.getResource(worker);
    float workerToResourceBefore =
        resourceBefore ? utils::distanceBB(worker, resourceBefore) : 0.0;
    if (resourceBefore && resourceBefore->type->isMinerals &&
        workerToResourceBefore < 4 &&
        resourceWorkers.countWorkers(resourceBefore) < 4) {
      continue;
    }

    // Update this worker.
    // Remove it from its current resource,
    // then assign it to the best possible resource.
    workers[worker].framesSinceUpdate = 0;
    resourceWorkers.unassignWorker(worker);
    if (resourceBefore && resourceBefore->type->isGas) {
      --gasWorkersNow;
    }
    const double gasWorkerDesire = gasWorkersNow < gasWorkersMax ? 1.0 : 0.1;

    // Evaluate the marginal efficacy of assigning this worker to a resource.
    constexpr double kInvalid = 1e100;
    auto scoreResource = [&](auto& pair) {
      Unit* resource = pair.first;

      // How many workers are already mining this patch?
      // Count only close-or-closer workers so new miners don't scare us off.
      const int workersBefore = pair.second.size();

      // How effective will the next worker be on this resource?
      double throughput = 0.001;
      if (mineralBlockersToRemove.find(resource) !=
          mineralBlockersToRemove.end()) {
        if (workersBefore < 1) {
          throughput = 1000.0;
        }
      } else if (resource->type->isGas) {
        // Depends on distance, but generally a geyser only supports three
        // workers
        if (workersBefore == 3) {
          throughput = 0.01;
        } else if (workersBefore < 3) {
          throughput = 1.0;
        }
        // Account for geyser depletion
        if (resource->unit.resources < 8) {
          throughput *= 0.25;
        }
      } else {
        if (workersBefore == 0) {
          throughput = 1.0;
        } else if (workersBefore == 1) {
          throughput = FLAGS_gatherer_mining2;
        } else if (workersBefore == 2) {
          throughput = FLAGS_gatherer_mining3;
        }
      }

      // How fast is mining from this resource?
      const double depotToResource = depotToResourceDistances[resource];
      const double speed =
          FLAGS_gatherer_speed_bias + 12.0 / std::max(12.0, depotToResource);

      if (depotToResource > kDistanceMining) {
        if (!canDistanceMine) {
          return kInvalid;
        }
      }

      // When deciding whether to travel to another base to mine, there's a
      // tradeoff between mining efficiency and the time-discounted value of
      // resources.
      // There's no obvious way to measure the tradeoff, so it's left as a
      // hyperparameter.
      const bool stick = resourceBefore == resource &&
          !worker->carryingResources() &&
          (resourceBefore == nullptr || workerToResourceBefore > 1);
      const auto baseFrom = utils::contains(resourceBases, resourceBefore)
          ? resourceBases[resourceBefore]
          : nullptr;
      const auto baseTo = utils::contains(resourceBases, resource)
          ? resourceBases[resource]
          : nullptr;
      const double workerToResource = (baseFrom && baseTo && baseFrom != baseTo)
          ? baseCosts[baseFrom][baseTo]
          : utils::distanceBB(worker, resource);
      const double framesToResource =
          (workerToResource + (stick ? 0.0 : FLAGS_gatherer_sticky_distance)) /
          std::max(.01, worker->topSpeed);
      const double framesGathering =
          std::max(24.0, FLAGS_gatherer_lookahead - framesToResource);
      const double preference = resource->type->isGas ? gasWorkerDesire : 1.0;
      const double stickiness = stick ? FLAGS_gatherer_sticky_multiplier : 1.0;

      VLOG(6) << fmt::format(
          "Eval of {} for {}: {} {} {} {} {}",
          utils::unitString(resource),
          utils::unitString(worker),
          throughput,
          speed,
          int(framesGathering),
          preference,
          stickiness);
      return -throughput * speed * framesGathering * preference * stickiness;
    };

    // Assign the worker to the best resource.
    auto resourceAfterIter =
        utils::getBestScore(resourceWorkers, scoreResource, kInvalid);
    if (resourceAfterIter != resourceWorkers.end()) {
      Unit* resourceAfter = resourceAfterIter->first;
      if (resourceBefore == resourceAfter) {
        VLOG(4) << fmt::format(
            "{} continues gathering {} ({})",
            utils::unitString(worker),
            utils::unitString(resourceAfter),
            scoreResource(*resourceAfterIter));
      } else {
        gasWorkersNow -= resourceBefore && resourceBefore->type->isGas ? 1 : 0;
        gasWorkersNow += resourceAfter->type->isGas ? 1 : 0;
        VLOG(4) << fmt::format(
            "{} switches from gathering {} ({}) to {} ({})",
            utils::unitString(worker),
            utils::unitString(resourceBefore),
            workers[worker].lastResourceScore,
            utils::unitString(resourceAfter),
            scoreResource(*resourceAfterIter));
        const double depotDistance = depotToResourceDistances[resourceAfter];
        if (depotDistance > kDistanceMining) {
          VLOG(1) << fmt::format(
              "{} is distance mining from {} ({})",
              utils::unitString(worker),
              utils::unitString(resourceAfter),
              depotDistance);
        }
      }
      workers[worker].lastResourceScore = scoreResource(*resourceAfterIter);
      resourceWorkers.assignWorker(worker, resourceAfter);
      if (resourceBefore != resourceAfter) {
        workers[worker].cooldownUntil =
            int(currentFrame + FLAGS_gatherer_cooldown +
                FLAGS_gatherer_cooldown_noise * (common::Rand::rand() % 2 - 1));
      }
    }
  }

  // Update saturation of bases
  for (auto& pair : baseResources) {
    auto* base = pair.first;
    double minerals = 0;
    double gas = 0;
    double workers = 0;
    for (auto* resource : pair.second) {
      gas += resource->type->isGas ? 1 : 0;
      minerals += resource->type->isMinerals ? 1 : 0;
      workers += resourceWorkers.countWorkers(resource);
    }
    double denominator = 3 * gas + 2 * minerals;

    // For efficiency and expediency, GathererController is responsible for
    // updating base saturation. Hence, the const_cast.
    // (No, this isn't a good design)
    const_cast<Base*>(base)->saturation =
        denominator >= 0 ? workers / denominator : 1.0;
  }

  // Draw saturation, in order to detect overmicroed workers
  if (VLOG_IS_ON(1)) {
    for (auto& pair : resourceWorkers) {
      utils::drawCircle(
          state,
          Position(pair.first),
          16,
          pair.first->beingGathered() ? tc::BW::Color::Cyan
                                      : tc::BW::Color::Red);
    }
  }
}

} // namespace cherrypi
