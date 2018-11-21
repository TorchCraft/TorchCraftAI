/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "features.h"

#include "state.h"

#include <cassert>
#include <glog/logging.h>

namespace cherrypi {
namespace featureimpl {

namespace {

int constexpr kNumTerrainValues = 3;

void getIndexBounds(
    Rect& ir,
    int& axmin,
    int& axmax,
    int& aymin,
    int& aymax,
    Rect const& boundingBox,
    Rect const& mapRect) {
  // This is the region we are able to fill
  ir = boundingBox.intersected(mapRect);
  if (ir.empty()) {
    axmin = 1;
    axmax = 0;
    aymin = 1;
    aymax = 0;
    return;
  }
  // Index bounds on tensor
  axmin = ir.x - boundingBox.x;
  axmax = axmin + ir.w;
  aymin = ir.y - boundingBox.y;
  aymax = aymin + ir.h;
}

void extractStaticMapDataHelper(
    torch::Tensor t,
    Rect const& boundingBox,
    std::vector<uint8_t> const& data,
    Rect const& mapRect) {
  Rect ir;
  int axmin, axmax;
  int aymin, aymax;
  getIndexBounds(ir, axmin, axmax, aymin, aymax, boundingBox, mapRect);

  t.fill_(-1);
  auto a = t.accessor<float, 3>()[0];
  for (int ay = aymin, wy = ir.y; ay < aymax; ay++, wy++) {
    auto row = a[ay];
    auto woff = wy * mapRect.w;
    for (int ax = axmin, wx = ir.x; ax < axmax; ax++, wx++) {
      row[ax] = data[woff + wx];
    }
  }
}

} // namespace

/**
 * Extracts a 2D tensor of ground height, which impacts vision and the
 * probability that a bullet attack will miss. Ignores the presence of doodads.
 *
 * * 0: Low ground
 * * 1: High ground
 * * 2: Very high ground
 *
 * See
 * https://bwapi.github.io/class_b_w_a_p_i_1_1_game.html#a94eb3e3fe7850078c2086638a46214be
 */
void extractGroundHeight(torch::Tensor t, State* state, Rect const& r) {
  Rect ir;
  int axmin, axmax;
  int aymin, aymax;
  auto mapRect = state->mapRect();
  getIndexBounds(ir, axmin, axmax, aymin, aymax, r, mapRect);

  auto const& data = state->tcstate()->ground_height_data;
  t.fill_(-1);
  auto a = t.accessor<float, 3>()[0];
  for (int ay = aymin, wy = ir.y; ay < aymax; ay++, wy++) {
    auto row = a[ay];
    auto woff = wy * mapRect.w;
    for (int ax = axmin, wx = ir.x; ax < axmax; ax++, wx++) {
      row[ax] = data[woff + wx] / 2;
    }
  }
}

/**
 * Extracts a 2D tensor of the presence of tall doodads, which impact vision
 * and the probability that a bullet attack will miss.
 *
 * * 0: No tall doodad
 * * 1: Tall doodad
 *
 * See
 * https://bwapi.github.io/class_b_w_a_p_i_1_1_game.html#a94eb3e3fe7850078c2086638a46214be
 */
void extractTallDoodad(torch::Tensor t, State* state, Rect const& r) {
  Rect ir;
  int axmin, axmax;
  int aymin, aymax;
  auto mapRect = state->mapRect();
  getIndexBounds(ir, axmin, axmax, aymin, aymax, r, mapRect);

  auto const& data = state->tcstate()->ground_height_data;
  t.fill_(0);
  auto a = t.accessor<float, 3>()[0];
  for (int ay = aymin, wy = ir.y; ay < aymax; ay++, wy++) {
    auto row = a[ay];
    auto woff = wy * mapRect.w;
    for (int ax = axmin, wx = ir.x; ax < axmax; ax++, wx++) {
      row[ax] = data[woff + wx] % 2;
    }
  }
}

/**
 * Extracts a 2D tensor of whether the terrain on a walktile is walkable by
 * ground units.
 *
 * See
 * https://bwapi.github.io/class_b_w_a_p_i_1_1_game.html#a91153ca71797617ce225adf28d508510
 */
void extractWalkability(torch::Tensor t, State* state, Rect const& r) {
  extractStaticMapDataHelper(
      t, r, state->tcstate()->walkable_data, state->mapRect());
}

void extractBuildability(torch::Tensor t, State* state, Rect const& r) {
  extractStaticMapDataHelper(
      t, r, state->tcstate()->buildable_data, state->mapRect());
}

/**
 * Extracts a 3D tensor of ground height, where each of the 3 ground heights
 * (plus "not on the map") is a one-hot dimension. Ignores the presence of
 * doodads.
 *
 * See
 * https://bwapi.github.io/class_b_w_a_p_i_1_1_game.html#a94eb3e3fe7850078c2086638a46214be
 */
void extractOneHotGroundHeight(torch::Tensor t, State* state, Rect const& r) {
  Rect ir;
  int axmin, axmax;
  int aymin, aymax;
  auto mapRect = state->mapRect();
  getIndexBounds(ir, axmin, axmax, aymin, aymax, r, mapRect);

  auto const& data = state->tcstate()->ground_height_data;
  // One channel for each possible values and one for off of map
  t.resize_({kNumTerrainValues + 1, t.size(1), t.size(2)});
  t.fill_(0);
  // Fill off-of-map channel with 1's and update as we fill in map
  t[kNumTerrainValues].fill_(1);

  auto a = t.accessor<float, 3>();
  for (int ay = aymin, wy = ir.y; ay < aymax; ay++, wy++) {
    auto woff = wy * mapRect.w;
    for (int ax = axmin, wx = ir.x; ax < axmax; ax++, wx++) {
      auto channel = data[woff + wx] / 2;
      assert(channel <= 2 && channel >= 0);
      a[channel][ay][ax] = 1;
      a[kNumTerrainValues][ay][ax] = 0;
    }
  }
}

/**
 * Set tensor to '1' for every start location reported by TorchCraft.
 */
void extractStartLocations(torch::Tensor t, State* state, Rect const& r) {
  FeaturePositionMapper mapper(r, state->mapRect());
  auto a = t.accessor<float, 3>()[0];
  for (auto& pos : state->tcstate()->start_locations) {
    auto mpos = mapper({pos.x, pos.y});
    if (mpos.x >= 0) {
      a[mpos.y][mpos.x] = 1;
    }
  }
}

} // namespace featureimpl
} // namespace cherrypi
