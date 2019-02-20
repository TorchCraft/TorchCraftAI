/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "features.h"

#include "state.h"
#include "tilesinfo.h"

#include <glog/logging.h>

namespace cherrypi {
namespace featureimpl {

namespace {

auto const cast = [](auto const& val) -> float {
  return static_cast<float>(val);
};

template <typename T, typename F = decltype(cast)>
void extractTilesInfoHelper(
    torch::Tensor t,
    Rect const& boundingBox,
    State* state,
    T Tile::*field,
    F castfn = cast) {
  t.fill_(-1);

  // This is the region we are able to fill
  auto ir = boundingBox.intersected(state->mapRect());
  if (ir.empty()) {
    return;
  }
  // Index bounds on tensor
  int axmin = ir.x - boundingBox.x;
  int axmax = axmin + ir.w;
  int aymin = ir.y - boundingBox.y;
  int aymax = aymin + ir.h;

  auto a = t.accessor<float, 3>()[0];
  auto& tilesInfo = state->tilesInfo();

  // TODO: This could probably be optimized by leveraging the lower resolution
  // of TilesInfo, e.g. by manually filling the tensor in 4x4 steps.
  for (int ay = aymin, wy = ir.y; ay < aymax; ay++, wy++) {
    auto row = a[ay];
    unsigned tileOff = TilesInfo::tilesWidth *
        (wy / (unsigned)tc::BW::XYWalktilesPerBuildtile);
    for (int ax = axmin, wx = ir.x; ax < axmax; ax++, wx++) {
      unsigned tileX = wx / (unsigned)tc::BW::XYWalktilesPerBuildtile;
      auto& tile = tilesInfo.tiles[tileOff + tileX];
      row[ax] = castfn(tile.*field);
    }
  }
}

} // namespace

/**
 * 2D tensor representation of whether a tile is visible to the current player
 * (as opposed to being in the fog of war).
 */
void extractFogOfWar(torch::Tensor t, State* state, Rect const& r) {
  extractTilesInfoHelper(t, r, state, &Tile::visible, [](bool visible) {
    return visible ? 0.0f : 1.0f;
  });
}

/**
 * 2D tensor representation of whether a tile has creep
 */
void extractCreep(torch::Tensor t, State* state, Rect const& r) {
  extractTilesInfoHelper(t, r, state, &Tile::hasCreep);
}

/**
 * 2D tensor representation of whether we have reserved this area for placing
 * a building, rendering it unavailable for further buildings.
 */
void extractReservedAsUnbuildable(
    torch::Tensor t,
    State* state,
    Rect const& r) {
  extractTilesInfoHelper(t, r, state, &Tile::reservedAsUnbuildable);
}

} // namespace featureimpl
} // namespace cherrypi
