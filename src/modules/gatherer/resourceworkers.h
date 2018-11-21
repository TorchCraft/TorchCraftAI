/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "unitsinfo.h"

#include <map>
#include <set>

namespace cherrypi {

/**
 * Data structure for storing the state of our gathering assignments.
 * Enforces the bidirectional mapping of (Worker -> Resource)
 * and (Resource -> Set<Workers>)
 *
 * Iterable as a C++ Range over (Resource -> Set<Workers>)
 */
struct ResourceWorkers {
 public:
  void assignWorker(Unit* worker, Unit* resource);
  void unassignWorker(Unit* worker);
  void includeResource(Unit* resource);  
  bool excludeResource(Unit* resource);  
  bool containsResource(Unit* resource);
  Unit* getResource(Unit* worker) const;
  size_t countWorkers(Unit* resource) const;
  
  /// Start iterator for worker assignments; allows treatment as a C++ Range
  auto begin() {
    return workersByResource.begin();
  }
  /// End iterator for worker assignments; allows treatment as a C++ Range
  auto end() {
    return workersByResource.end();
  }
  /// For how many resources is gathering enabled?
  size_t size() {
    return workersByResource.size();
  }

 protected:
  std::unordered_map<Unit*, Unit*> resourceByWorker;
  std::unordered_map<Unit*, std::unordered_set<Unit*>> workersByResource;
};
} // namespace
