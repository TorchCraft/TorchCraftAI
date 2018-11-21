/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "fogofwar.h"

#include "cherrypi.h"
#include "tilesinfo.h"

namespace cherrypi {

FogOfWar::FogOfWar() {
  generateSightValues();
}

void FogOfWar::revealSightAt(
    TilesInfo& tt,
    int x,
    int y,
    int range,
    bool in_air,
    int currentFrame) const {
  range /= tc::BW::XYWalktilesPerBuildtile;
  enum {
    flag_very_high = 0x100,
    flag_middle = 0x200,
    flag_high = 0x400,
  };
  Tile* startTile = tt.tryGetTile(x, y);
  if (!startTile)
    return;
  int height_mask = 0;
  if (!in_air) {
    int bwapi_height = startTile->height;
    int height = bwapi_height & 4 ? 2 : bwapi_height & 2 ? 1 : 0;
    if (height == 2)
      height_mask = flag_very_high;
    else if (height == 1)
      height_mask = flag_very_high | flag_high;
    else
      height_mask = flag_very_high | flag_high | flag_middle;
  }
  const size_t max_width = 11 * 2 + 3;
  std::array<uint8_t, max_width * max_width> vision_propagation;
  uint32_t required_tile_mask = (uint32_t)height_mask << 16 | 1;
  const auto& sight_vals = sight_values.at(range);
  size_t tile_x = (size_t)x / (size_t)tc::BW::XYWalktilesPerBuildtile;
  size_t tile_y = (size_t)y / (size_t)tc::BW::XYWalktilesPerBuildtile;
  Tile* base_tile = startTile;
  if (!in_air) {
    size_t index = 0;
    size_t end = sight_vals.min_mask_size;
    for (; index != end; ++index) {
      const auto& cur = sight_vals.maskdat[index];
      vision_propagation[index] = 0xff;
      if (tile_x + cur.x >= tt.mapTileWidth())
        continue;
      if (tile_y + cur.y >= tt.mapTileHeight())
        continue;
      auto& tile = base_tile[cur.relative_tile_index];
      tile.visible = true;
      tile.lastSeen = currentFrame;
      vision_propagation[index] = (uint32_t)(tile.height << 8) << 16;
    }
    end += sight_vals.ext_masked_count;
    for (; index != end; ++index) {
      const auto& cur = sight_vals.maskdat[index];
      vision_propagation[index] = 0xff;
      if (tile_x + cur.x >= tt.mapTileWidth())
        continue;
      if (tile_y + cur.y >= tt.mapTileHeight())
        continue;
      if (vision_propagation[cur.prev] & required_tile_mask) {
        if (cur.prev2 == (size_t)~0 ||
            (vision_propagation[cur.prev2] & required_tile_mask))
          continue;
      }
      auto& tile = base_tile[cur.relative_tile_index];
      tile.visible = true;
      tile.lastSeen = currentFrame;
      vision_propagation[index] = (uint32_t)(tile.height << 8) << 16;
    }
  } else {
    auto* cur = sight_vals.maskdat.data();
    auto* end = cur + sight_vals.ext_masked_count;
    for (; cur != end; ++cur) {
      if (tile_x + cur->x >= tt.mapTileWidth())
        continue;
      if (tile_y + cur->y >= tt.mapTileHeight())
        continue;
      auto& tile = base_tile[cur->relative_tile_index];
      tile.visible = true;
      tile.lastSeen = currentFrame;
    }
  }
}

void FogOfWar::generateSightValues() {
  for (size_t i = 0; i != sight_values.size(); ++i) {
    auto& v = sight_values[i];
    v.max_width = 3 + (int)i * 2;
    v.max_height = 3 + (int)i * 2;
    v.min_width = 3;
    v.min_height = 3;
    v.min_mask_size = 0;
    v.ext_masked_count = 0;
  }

  struct base_mask_t {
    sight_values_t::maskdat_node_t* maskdat_node;
    bool masked;
  };
  std::vector<base_mask_t> base_mask;
  for (auto& v : sight_values) {
    base_mask.clear();
    base_mask.resize(v.max_width * v.max_height);
    auto mask = [&](size_t index) { base_mask[index].masked = true; };
    v.min_mask_size = v.min_width * v.min_height;
    int offx = v.max_width / 2 - v.min_width / 2;
    int offy = v.max_height / 2 - v.min_height / 2;
    for (int y = 0; y < v.min_height; ++y) {
      for (int x = 0; x < v.min_width; ++x) {
        mask((offy + y) * v.max_width + offx + x);
      }
    }
    auto generate_base_mask = [&]() {
      int offset = v.max_height / 2 - v.max_width / 2;
      int half_width = v.max_width / 2;
      int max_x2 = half_width;
      int max_x1 = half_width * 2;
      int cur_x1 = 0;
      int cur_x2 = half_width;
      int i = 0;
      int max_i = half_width;
      int cursize1 = 0;
      int cursize2 = half_width * half_width;
      int min_cursize2 = half_width * (half_width - 1);
      int min_cursize2_chg = half_width * 2;
      while (true) {
        if (cur_x1 <= max_x1) {
          for (int i = 0; i <= max_x1 - cur_x1; ++i) {
            mask((offset + cur_x2) * v.max_width + cur_x1 + i);
            mask((offset + max_x2) * v.max_width + cur_x1 + i);
          }
        }
        if (cur_x2 <= max_x2) {
          for (int i = 0; i <= max_x2 - cur_x2; ++i) {
            mask((offset + cur_x1) * v.max_width + cur_x2 + i);
            mask((offset + max_x1) * v.max_width + cur_x2 + i);
          }
        }
        cursize2 += 1 - cursize1 - 2;
        cursize1 += 2;
        --cur_x2;
        ++max_x2;
        if (cursize2 <= min_cursize2) {
          --max_i;
          ++cur_x1;
          --max_x1;
          min_cursize2 -= min_cursize2_chg - 2;
          min_cursize2_chg -= 2;
        }

        ++i;
        if (i > max_i)
          break;
      }
    };
    generate_base_mask();
    int masked_count = 0;
    for (auto& v : base_mask) {
      if (v.masked)
        ++masked_count;
    }

    v.ext_masked_count = masked_count - v.min_mask_size;
    v.maskdat.clear();
    v.maskdat.resize(masked_count);

    size_t center_index = v.max_height / 2 * v.max_width + v.max_width / 2;
    base_mask[center_index].maskdat_node = &v.maskdat.front();

    auto at = [&](int relative_index) -> base_mask_t& {
      size_t index = center_index + relative_index;
      return base_mask[index];
    };

    size_t next_entry_index = 1;

    int cur_x = -1;
    int cur_y = -1;
    int added_count = 1;
    for (int i = 2; added_count < masked_count; i += 2) {
      for (int dir = 0; dir < 4; ++dir) {
        static const std::array<int, 4> direction_x = {1, 0, -1, 0};
        static const std::array<int, 4> direction_y = {0, 1, 0, -1};
        int this_x;
        int this_y;
        auto do_n = [&](int n) {
          for (int i = 0; i < n; ++i) {
            if (at(this_y * v.max_width + this_x).masked) {
              if (this_x || this_y) {
                auto* this_entry = &v.maskdat.at(next_entry_index++);

                auto index = [&](auto* n) {
                  if (!n)
                    return (size_t)-1;
                  return (size_t)(n - v.maskdat.data());
                };

                int prev_x = this_x;
                int prev_y = this_y;
                if (prev_x > 0)
                  --prev_x;
                else if (prev_x < 0)
                  ++prev_x;
                if (prev_y > 0)
                  --prev_y;
                else if (prev_y < 0)
                  ++prev_y;
                if (std::abs(prev_x) == std::abs(prev_y) ||
                    (this_x == 0 && direction_x[dir]) ||
                    (this_y == 0 && direction_y[dir])) {
                  this_entry->prev =
                      index(at(prev_y * v.max_width + prev_x).maskdat_node);
                  this_entry->prev2 = (size_t)-1;
                } else {
                  this_entry->prev =
                      index(at(prev_y * v.max_width + prev_x).maskdat_node);
                  int prev2_x = prev_x;
                  int prev2_y = prev_y;
                  if (std::abs(prev2_x) <= std::abs(prev2_y)) {
                    if (this_x >= 0)
                      ++prev2_x;
                    else
                      --prev2_x;
                  } else {
                    if (this_y >= 0)
                      ++prev2_y;
                    else
                      --prev2_y;
                  }
                  this_entry->prev2 =
                      index(at(prev2_y * v.max_width + prev2_x).maskdat_node);
                }
                this_entry->relative_tile_index =
                    this_y * (int)TilesInfo::tilesWidth + this_x;
                this_entry->x = this_x;
                this_entry->y = this_y;
                at(this_y * v.max_width + this_x).maskdat_node = this_entry;
                ++added_count;
              }
            }
            this_x += direction_x[dir];
            this_y += direction_y[dir];
          }
        };
        const std::array<int, 4> max_i = {
            v.max_height, v.max_width, v.max_height, v.max_width};
        if (i > max_i[dir]) {
          this_x = cur_x + i * direction_x[dir];
          this_y = cur_y + i * direction_y[dir];
          do_n(1);
        } else {
          this_x = cur_x + direction_x[dir];
          this_y = cur_y + direction_y[dir];
          do_n(std::min(max_i[(dir + 1) % 4] - 1, i));
        }
        cur_x = this_x - direction_x[dir];
        cur_y = this_y - direction_y[dir];
      }
      if (i < v.max_width - 1)
        --cur_x;
      if (i < v.max_height - 1)
        --cur_y;
    }
  }
}
}
