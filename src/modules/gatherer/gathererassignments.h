/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "modules/gatherer/resourceworkers.h"
#include "state.h"

namespace cherrypi {

/**
 * Assigns workers to resources for optimal gathering.
 */
struct GathererAssignments {
  
  /// Used internally to track worker statefulness
  struct WorkerState {
    /// Number of frames since this worker has had a gathering update.
    int framesSinceUpdate = kForever;
    
    /// Next frame we're allowed to reassign this worker to a new resource.
    /// Serves to avoid excessive churning of confused workers.
    int cooldownUntil = -kForever;
    
    /// Tracks the gathering value of this worker's last resource.
    /// Used for diagnostic purposes only.
    double lastResourceScore = 0.0;
  };
  
  /// Mapping of workers assigned to resources.
  ResourceWorkers resourceWorkers;
  
  /// Mapping of workers to their gathering state.
  std::unordered_map<Unit*, WorkerState> workers;
  
  /// Include a worker in gathering.
  void addUnit(Unit*);
  
  /// Remove a worker from gathering.
  void removeUnit(Unit*);
  
  /// Update gathering assignments for this frame.
  void step(State*);
};

} // namespace cherrypi
