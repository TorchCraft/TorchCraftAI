/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "controller.h"
#include "modules/gatherer/gathererassignments.h"

namespace cherrypi {

/**
 * Controls gathering workers for GathererModule.
 * - Bookkeeping for SharedController lives in gathererc.cpp
 * - Micromanagement and worker defense lives in gatherermicro.cpp
 */
class GathererController : public SharedController {
 public:
  using SharedController::SharedController;

  virtual void addUnit(State*, Unit* worker, UpcId) override;
  virtual void removeUnit(State*, Unit* worker, UpcId) override;
  virtual bool keepUnit(State*, Unit* worker) const override;
  virtual void step(State*) override;
  virtual const char* getName() const override {
    return "Gatherer";
  };

 protected:
  GathererAssignments assignments;
  std::vector<Unit*> proxyBuilders;
  std::vector<Unit*> proxies;
  std::vector<Unit*> invaders;
  std::vector<Unit*> bastions;
  
  /// True if we have ever been proxied (an enemy attempted to build structures
  /// in or near our base
  bool wasProxied = false;
  
  /// Decide what to do with a worker this frame.
  void micro(State*, Unit* worker, Unit* resource);
  
  void gather(State*, Unit* worker, Unit* resource, bool dropResources=false);  
  void flee(State*, Unit* worker, Unit* resource);
  void chase(State*, Unit* worker, Unit* target);
  void attack(State*, Unit* worker, Unit* target);
};

} // namespace cherrypi
