/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "modules/gatherer/resourceworkers.h"
#include "utils.h"

#include <glog/logging.h>

namespace cherrypi {

/// Assign a worker to gathering a specific resource.
void ResourceWorkers::assignWorker(Unit* worker, Unit* resource) {
  unassignWorker(worker);
  if (worker && resource && utils::contains(workersByResource, resource)) {
    resourceByWorker[worker] = resource;
    workersByResource[resource].insert(worker);
  } else {
    VLOG(0) << fmt::format(
        "Failed to assign {} to {}",
        utils::unitString(worker),
        utils::unitString(resource));
  }
}

/// Remove a worker from gathering and any resources it might be assigned to.
void ResourceWorkers::unassignWorker(Unit* worker) {
  if (worker) {
    Unit* resource = getResource(worker);
    if (resource && utils::contains(workersByResource, resource)) {
      workersByResource[resource].erase(worker);
    }
  }
  resourceByWorker.erase(worker);
}

/// Allow gathering from a resource.
void ResourceWorkers::includeResource(Unit* resource) {
  if (resource) {
    workersByResource[resource];
  }
}

/// Is this resource currently included in gathering?
bool ResourceWorkers::containsResource(Unit* resource) {
  return utils::contains(workersByResource, resource);
}

/// Disallow gathering from a resource.
bool ResourceWorkers::excludeResource(Unit* resource) {
  VLOG(1) << "Excluding " << utils::unitString(resource);
  if (utils::contains(workersByResource, resource)) {
    for (auto* worker : workersByResource[resource]) {
      resourceByWorker.erase(worker);
    }
    workersByResource.erase(resource);
    return true;
  }
  return false;
}

/// To which resource (if any) is this worker assigned?
Unit* ResourceWorkers::getResource(Unit* worker) const {
  return utils::contains(resourceByWorker, worker) ? resourceByWorker.at(worker)
                                                   : nullptr;
}
  
/// How many workers are assigned to this resource?
size_t ResourceWorkers::countWorkers(Unit* resource) const {
  return utils::contains(workersByResource, resource)
      ? workersByResource.at(resource).size()
      : 0U;
}

} // namespace cherrypi
