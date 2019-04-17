/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "buildtype.h"
#include "cherrypi.h"
#include "module.h"

#include "models/buildingplacer.h"

namespace cherrypi {

/**
 * Determines positions for buildings.
 *
 * For buildings that require a worker to build them, BuilderModule requires the
 * UPC to specify a Dirac position. This module's job is to determine suitable
 * positions based on an existing distribution over positions and various
 * heuristics (via builderhelpers).
 *
 * Optionally, BuildingPlacerModel can be used for building placement. The model
 * location is specified via the -bp_model command-line flag; if a valid model
 * is found at the specified location, it will be loaded and used. If GPU
 * support is available, the model will be run on the GPU.
 *
 * ProxyTasks are used to track execution of the downstream production task. If
 * the production task will fail (e.g. because the location became unbuildable),
 * retries will be attempted until the ProxyTask is cancelled (e.g. from an
 * upstream module).
 *
 * This module will also reserve build tiles as unbuildable via TilesInfo.
 */
class BuildingPlacerModule : public Module {
 public:
  virtual ~BuildingPlacerModule() = default;

  virtual void step(State* s) override;
  virtual void onGameStart(State* state) override;

 private:
  std::shared_ptr<UPCTuple> upcWithPositionForBuilding(
      State* state,
      UPCTuple const& upc,
      BuildType const* type);

  std::shared_ptr<BuildingPlacerModel> model_;
  std::shared_ptr<BuildingPlacerSample::StaticData> staticData_;
  bool firstStep_ = true;
  std::unordered_set<Position> baseLocations_;
};

} // namespace cherrypi
