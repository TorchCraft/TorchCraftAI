/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "module.h"
#include "task.h"
#include "unitsinfo.h"

#include <memory>
#include <vector>

namespace cherrypi {

/**
 * Manages worker units for resource gathering.
 *
 * GathererModule is a thin orchestrator of GatherControllers.
 * Most gathering logic happens in gathererassignments.cpp or 
 * gatherermicro.cpp
 */
class GathererModule : public Module {
 public:
  GathererModule() {}
  virtual ~GathererModule() = default;

  virtual void step(State* s) override;
};

} // namespace cherrypi
