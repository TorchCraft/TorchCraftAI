/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "features.h"

#include "state.h"

namespace cherrypi {
namespace featureimpl {

/**
 * 2D tensor representation of whether a tile is a start location.
 */
void extractCandidateEnemyStartLocations(
    torch::Tensor t,
    State* state,
    Rect const& r) {
  FeaturePositionMapper mapper(r, state->mapRect());
  auto a = t.accessor<float, 3>()[0];
  for (auto& pos : state->areaInfo().candidateEnemyStartLocations()) {
    auto mpos = mapper(pos);
    if (mpos.x >= 0) {
      a[mpos.y][mpos.x] = 1;
    }
  }
}

/**
 * 2D tensor representation of whether a tile is a start location.
 */
void extractCandidateEnemyStartLocationsBT(
    torch::Tensor t,
    State* state,
    Rect const& rBT) {
  auto mapRectBT = state->mapRect();
  mapRectBT.w /= tc::BW::XYWalktilesPerBuildtile;
  mapRectBT.h /= tc::BW::XYWalktilesPerBuildtile;
  FeaturePositionMapper mapper(rBT, mapRectBT);
  auto a = t.accessor<float, 3>()[0];
  for (auto& pos : state->areaInfo().candidateEnemyStartLocations()) {
    auto mpos = mapper(pos / tc::BW::XYWalktilesPerBuildtile);
    if (mpos.x >= 0) {
      a[mpos.y][mpos.x] = 1;
    }
  }
}

} // namespace featureimpl
} // namespace cherrypi
