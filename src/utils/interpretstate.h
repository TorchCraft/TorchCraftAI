/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "cherrypi.h"
#include "state.h"
#include "tilesinfo.h"

#include <deque>

namespace cherrypi {
namespace utils {

/* Fills in a vector of ints corresponding to all tiles in the map with a
 * 1 if the tile is within ally base area.
 */
inline void updateInBaseArea(State* state, std::vector<uint8_t>& inBaseArea) {
  std::fill(inBaseArea.begin(), inBaseArea.end(), 0);

  auto& tilesInfo = state->tilesInfo();
  auto* tilesData = tilesInfo.tiles.data();

  const int mapWidth = state->mapWidth();
  const int mapHeight = state->mapHeight();

  struct OpenNode {
    const Tile* tile;
    const Tile* sourceTile;
    float maxDistance;
  };

  std::vector<Position> staticDefence;
  for (Unit* u :
       state->unitsInfo().myUnitsOfType(buildtypes::Zerg_Sunken_Colony)) {
    staticDefence.emplace_back(u->x, u->y);
  }

  const Tile* mainBaseTile = nullptr;
  std::deque<OpenNode> open;
  bool isMain = true;
  for (Unit* u : state->unitsInfo().myResourceDepots()) {
    auto* tile = tilesInfo.tryGetTile(u->x, u->y);
    if (tile) {
      float maxDistance = isMain ? 4 * 24 : 4 * 14;
      if (isMain) {
        mainBaseTile = tile;
      }
      isMain = false;
      open.push_back({tile, tile, maxDistance});
      inBaseArea.at(tile - tilesData) = 1;
    }
  }
  while (!open.empty()) {
    OpenNode curNode = open.front();
    open.pop_front();

    auto add = [&](const Tile* ntile) {
      if (!curNode.tile->entirelyWalkable) {
        return;
      }

      float sourceDistance = utils::distance(
          ntile->x, ntile->y, curNode.sourceTile->x, curNode.sourceTile->y);
      if (sourceDistance >= curNode.maxDistance) {
        return;
      }

      if (!mainBaseTile) {
        bool staticDefenceIsCloserThanHome = false;
        bool isInStaticDefenceRange = false;
        for (Position pos : staticDefence) {
          float d = utils::distance(ntile->x, ntile->y, pos.x, pos.y);
          if (d <= 4 * 6) {
            isInStaticDefenceRange = true;
          }
          if (d < sourceDistance) {
            staticDefenceIsCloserThanHome = true;
          }
        }
        if (staticDefenceIsCloserThanHome && !isInStaticDefenceRange) {
          return;
        }
      }

      auto& v = inBaseArea[ntile - tilesData];
      if (v) {
        return;
      }
      v = 1;
      open.push_back({ntile, curNode.sourceTile, curNode.maxDistance});
    };

    const Tile* tile = curNode.tile;

    if (tile->x > 0) {
      add(tile - 1);
      if (tile->y > 0) {
        add(tile - 1 - TilesInfo::tilesWidth);
        add(tile - TilesInfo::tilesWidth);
      }
      if (tile->y < mapHeight - tc::BW::XYWalktilesPerBuildtile) {
        add(tile - 1 + TilesInfo::tilesHeight);
        add(tile + TilesInfo::tilesHeight);
      }
    } else {
      if (tile->y > 0) {
        add(tile - TilesInfo::tilesWidth);
      }
      if (tile->y < mapHeight - tc::BW::XYWalktilesPerBuildtile) {
        add(tile + TilesInfo::tilesHeight);
      }
    }
    if (tile->x < mapWidth - tc::BW::XYWalktilesPerBuildtile) {
      add(tile + 1);
      if (tile->y > 0) {
        add(tile + 1 - TilesInfo::tilesWidth);
      }
      if (tile->y < mapHeight - tc::BW::XYWalktilesPerBuildtile) {
        add(tile + 1 + TilesInfo::tilesHeight);
      }
    }
  }
}

} // namespace utils
} // namespace cherrypi
