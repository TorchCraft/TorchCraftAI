/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace cherrypi {

class TilesInfo;

/**
 * Calculates which tiles should be revealed by a unit's vision.
 * BWAPI gives you your own vision, but this can be used to understand
 * the opponent's vision.
 */
class FogOfWar {
  struct sight_values_t {
    struct maskdat_node_t {
      size_t prev;
      size_t prev2;
      int relative_tile_index;
      int x;
      int y;
    };
    int max_width, max_height;
    int min_width, min_height;
    int min_mask_size;
    int ext_masked_count;
    std::vector<maskdat_node_t> maskdat;
  };

  std::array<sight_values_t, 12> sight_values;

  void generateSightValues();

 public:
  FogOfWar();
  void revealSightAt(
      TilesInfo& tt,
      int x,
      int y,
      int range,
      bool in_air,
      int currentFrame) const;
};
} // namespace cherrypi
