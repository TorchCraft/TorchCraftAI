/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "basetypes.h"

#include <unordered_map>
#include <vector>

namespace cherrypi {

class State;
struct Unit;
struct BuildType;

/**
 * Represents a tile on the map.
 *
 * buildable and height are static map data, the rest are updated throughout
 * the game. All fields might not be updated immediately every frame, but most
 * things should only have a few frames of delay in the worst case.
 */

struct Tile {
 public:
  /// X position of tile in walk tiles
  int x = 0;
  /// Y position of tile in walk tiles
  int y = 0;
  bool visible = false;
  bool buildable = false;
  /// Set by builderhelpers to help with planning building placement.
  bool reservedAsUnbuildable = false;
  bool hasCreep = false;

  /// For lazily-updated info (at time of writing, just hasCreepIn):
  /// When this tile was last updated
  int lazyUpdateFrame = -kForever;
  /// When this tile is expected to have creep.
  /// first: Last frame this was updated
  /// second: Frame creep is expected
  FrameNum expectsCreepUpdated_ = -kForever;
  FrameNum expectsCreepFrame_ = kForever;
  /// If this tile is expected to contain creep within a certain number of
  /// frames
  FrameNum expectsCreepBy() const;

  /// Indicates that this tile is in the mineral line and that buildings
  /// should not be placed here.
  bool reservedForGathering = false;
  /// Indicates that this tile is unbuildable for resource depots (too close
  /// to resources).
  bool resourceDepotUnbuildable = false;
  /// Indicates that this is a resource depot tile at an expansion, and should
  /// not be occupied by regular buildings.
  bool reservedForResourceDepot = false;
  /// Special field that can be set if a particular tile should not be used
  /// for buildings until this frame. Usually set by BuilderModule when trying
  /// and, for an unknown reason, failing to build something, such that we can
  /// try to build somewhere else.
  FrameNum blockedUntil = 0;
  /// The building that is currently occupying this tile. There might be other
  /// (stacked) buildings too.
  Unit* building = nullptr;
  /// Every walk tile within this tile is walkable. This usually means that
  /// any unit can pass through this tile, ignoring buildings or other units.
  bool entirelyWalkable = false;
  int height = 0;
  FrameNum lastSeen = 0;
  FrameNum lastSlowUpdate = 0;
};

/**
 * Manages and updates per-tile data.
 */
class TilesInfo {
 public:
  TilesInfo(State* state_);
  TilesInfo(TilesInfo const&) = delete;
  TilesInfo& operator=(TilesInfo const&) = delete;

  void preUnitsUpdate();
  void postUnitsUpdate();

  unsigned mapTileWidth() const {
    return mapTileWidth_;
  }

  unsigned mapTileHeight() const {
    return mapTileHeight_;
  }

  static const unsigned tilesWidth = 256;
  static const unsigned tilesHeight = 256;

  Tile& getTile(int walkX, int walkY);
  const Tile& getTile(int walkX, int walkY) const;
  Tile* tryGetTile(int walkX, int walkY);
  const Tile* tryGetTile(int walkX, int walkY) const;

  /// This sets reservedAsUnbuildable in the tiles that would be occupied
  /// by the specified building at the specified build location (upper left
  /// corner coordinates). Should only be used by BuilderModule.
  void reserveArea(const BuildType* type, int walkX, int walkY);
  // Complements reserveArea.
  void unreserveArea(const BuildType* type, int walkX, int walkY);

  /// All the tiles. Prefer to use getTile, this is only here in case it is
  /// needed for performance.
  std::vector<Tile> tiles;

 protected:
  struct TileOccupyingBuilding {
    Unit* u;
    const BuildType* type;
    int pixelX;
    int pixelY;
    std::vector<Tile*> tiles;
  };

  unsigned mapTileWidth_ = 0;
  unsigned mapTileHeight_ = 0;

  std::unordered_map<const Unit*, TileOccupyingBuilding>
      tileOccupyingBuildings_;

  State* state_ = nullptr;
  FrameNum lastSlowTileUpdate_ = 0;
  FrameNum lastUpdateBuildings_ = 0;
  FrameNum lastFowCreepUpdate_ = 0;
};

} // namespace cherrypi
