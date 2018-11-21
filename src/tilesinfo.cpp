/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "tilesinfo.h"
#include "cherrypi.h"
#include "fogofwar.h"
#include "state.h"
#include "utils.h"

#include <array>
#include <glog/logging.h>
#include <stdexcept>

namespace cherrypi {

static const FogOfWar FOW;

TilesInfo::TilesInfo(State* state) : state_(state) {
  mapTileWidth_ = state->mapWidth() / (unsigned)tc::BW::XYWalktilesPerBuildtile;
  mapTileHeight_ =
      state->mapHeight() / (unsigned)tc::BW::XYWalktilesPerBuildtile;
  if (mapTileWidth_ > tilesWidth) {
    throw std::runtime_error("bad map width");
  }
  if (mapTileHeight_ > tilesHeight) {
    throw std::runtime_error("bad map height");
  }
  tiles.resize(tilesHeight * tilesWidth);

  auto tcstate = state->tcstate();
  for (unsigned tileY = 0; tileY != mapTileHeight(); ++tileY) {
    for (unsigned tileX = 0; tileX != mapTileWidth(); ++tileX) {
      Tile& t = tiles[tilesWidth * tileY + tileX];
      t.x = tileX * tc::BW::XYWalktilesPerBuildtile;
      t.y = tileY * tc::BW::XYWalktilesPerBuildtile;
      t.buildable = tcstate->buildable_data[t.y * tcstate->map_size[0] + t.x];
      t.height = tcstate->ground_height_data[t.y * tcstate->map_size[0] + t.x];
      bool entirelyWalkable = true;
      for (int suby = 0; suby != tc::BW::XYWalktilesPerBuildtile; ++suby) {
        for (int subx = 0; subx != tc::BW::XYWalktilesPerBuildtile; ++subx) {
          if (!tcstate->walkable_data[(t.y + suby) * tcstate->map_size[0] +
                                      (t.x + subx)]) {
            entirelyWalkable = false;
            break;
          }
        }
      }
      t.entirelyWalkable = entirelyWalkable;
    }
  }
}

template <typename F>
static void forAllTiles(TilesInfo& tt, F&& f) {
  size_t stride = TilesInfo::tilesWidth - tt.mapTileWidth();
  Tile* ptr = tt.tiles.data();
  for (unsigned tileY = 0; tileY != tt.mapTileHeight();
       ++tileY, ptr += stride) {
    for (unsigned tileX = 0; tileX != tt.mapTileWidth(); ++tileX, ++ptr) {
      f(*ptr);
    }
  }
}

void TilesInfo::preUnitsUpdate() {}

void TilesInfo::postUnitsUpdate() {
  FrameNum frame = state_->currentFrame();

  if (lastFowCreepUpdate_ == 0 || frame - lastFowCreepUpdate_ >= 9) {
    forAllTiles(*this, [&](Tile& t) { t.visible = false; });

    for (Unit* u : state_->unitsInfo().myUnits()) {
      FOW.revealSightAt(
          *this,
          u->x,
          u->y,
          u->sightRange,
          u->type->isFlyer || u->lifted(),
          frame);
    }

    auto* tcframe = state_->tcstate()->frame;
    unsigned stride = mapTileWidth_;
    forAllTiles(*this, [tcframe, stride](Tile& t) {
      if (t.visible) {
        unsigned index =
            t.y / unsigned(tc::BW::XYWalktilesPerBuildtile) * stride +
            t.x / unsigned(tc::BW::XYWalktilesPerBuildtile);
        t.hasCreep = (tcframe->creep_map[index / 8] >> (index % 8)) & 1;
      }
    });
  }

  auto anticipateCreep = [&]() {
    // Anticipate where creep is likely to arrive.
    // This is helpful for sending builders to their destination
    // timed to the arrival of the creep.
    for (const Unit* u : state_->unitsInfo().myResourceDepots()) {
      if (!u->type->producesCreep) {
        continue;
      }
      // We want to use remainingBuildTrainTime to estimate when a building
      // will begin spewing creep. But this is complicated by morphs --
      // if a Hatchery finishes and immediately morphs into a Lair,
      // we want to use the time the Hatchery finished, not the time the Lair
      // will finish.
      const bool isHatch = u->type == buildtypes::Zerg_Hatchery;
      const bool wasHatch =
          u->type == buildtypes::Zerg_Lair || u->type == buildtypes::Zerg_Hive;
      const bool wasCreep = u->type == buildtypes::Zerg_Sunken_Colony ||
          u->type == buildtypes::Zerg_Spore_Colony;
      const int firstSpewFrame = (u->completed() || wasHatch || wasCreep)
          ? u->firstSeen + ((isHatch || wasHatch)
                                ? buildtypes::Zerg_Hatchery->buildTime
                                : buildtypes::Zerg_Creep_Colony->buildTime)
          : frame + u->remainingBuildTrainTime;

      if (frame - firstSpewFrame > 24 * 60 * 2) {
        // Time saver: Assume all the creep has been produced already
        continue;
      }

      const int buildX32 = u->buildX / tc::BW::XYWalktilesPerBuildtile;
      const int buildY32 = u->buildY / tc::BW::XYWalktilesPerBuildtile;
      const int width32 = u->type->tileWidth;
      const int height32 = u->type->tileHeight;
      constexpr int radius32 = 4;
      VLOG(4) << "Creeping " << utils::unitString(u) << " frame "
              << firstSpewFrame << ": " << buildX32 << ", " << buildY32;
      for (int dx32 = -radius32; dx32 < width32 + radius32; ++dx32) {
        for (int dy32 = -radius32; dy32 < height32 + radius32; ++dy32) {
          const int x32 = buildX32 + dx32;
          const int y32 = buildY32 + dy32;
          if (dx32 >= 0 && dx32 < width32 && dy32 >= 0 && dy32 < height32) {
            continue;
          }
          // It looks like the first two creep tiles around the Hatchery spew
          // immediately; the others then grow outwards. OpenBW contains the
          // real
          // formula but this approximation should suffice.
          const int distance32 = std::max(
              std::max(-dx32, dx32 - width32),
              std::max(-dy32, dy32 - height32));
          const int spewFramesAhead =
              distance32 < 3 ? 0 : 240 * (distance32 - 2) * (distance32 - 2);
          const int spewFrame = firstSpewFrame + spewFramesAhead;
          auto* tile = tryGetTile(
              x32 * tc::BW::XYWalktilesPerBuildtile,
              y32 * tc::BW::XYWalktilesPerBuildtile);
          if (tile && !tile->hasCreep) {
            tile->expectsCreepFrame_ = tile->expectsCreepUpdated_ == frame
                ? std::min(spewFrame, tile->expectsCreepFrame_)
                : spewFrame;
            tile->expectsCreepUpdated_ = frame;
            if (VLOG_IS_ON(1)) {
              const int radius = 1 +
                  int(3 * (1.0 - utils::clamp(
                                     (tile->expectsCreepFrame_ - frame) / 240.0,
                                     0.0,
                                     1.0)));
              utils::drawCircle(
                  state_,
                  {tile->x + 2, tile->y + 2},
                  radius,
                  tc::BW::Color::Purple);
              VLOG(5) << "Creep expected on frame " << spewFramesAhead << " at "
                      << x32 << ", " << y32 << " (" << tile->x << ", "
                      << tile->y << ")";
            }
          }
        }
      }
    }
  };

  anticipateCreep();

  // Perform more expensive updates less frequently
  if (lastSlowTileUpdate_ == 0 || frame - lastSlowTileUpdate_ >= 15 * 10) {
    lastSlowTileUpdate_ = frame;

    forAllTiles(*this, [&frame](Tile& t) {
      t.reservedForGathering = false;
      t.resourceDepotUnbuildable = false;
      t.reservedForResourceDepot = false;
      t.lastSlowUpdate = frame;
    });

    // anticipateCreep();

    // Mark off those tiles that are too close to resources as unbuildable
    // for resource depots.
    for (const Unit* u : state_->unitsInfo().resourceUnits()) {
      int tileLeft = (u->unit.pixel_x - u->type->dimensionLeft) /
          tc::BW::XYPixelsPerBuildtile;
      int tileTop = (u->unit.pixel_y - u->type->dimensionUp) /
          tc::BW::XYPixelsPerBuildtile;
      for (int y = -3; y != 3 + u->type->tileHeight; ++y) {
        for (int x = -3; x != 3 + u->type->tileWidth; ++x) {
          auto* t = tryGetTile(
              tc::BW::XYWalktilesPerBuildtile * (tileLeft + x),
              tc::BW::XYWalktilesPerBuildtile * (tileTop + y));
          if (t) {
            t->resourceDepotUnbuildable = true;
          }
        }
      }
    }

    // Mark some tiles between the resource depot and resources, to prevent us
    // from building in the middle of the mineral line.
    for (const Unit* u : state_->unitsInfo().myResourceDepots()) {
      for (const Unit* r : state_->unitsInfo().resourceUnits()) {
        if (utils::distance(u, r) >= 4 * 12) {
          continue;
        }
        auto reserve = [&](int x, int y) {
          auto* t = tryGetTile(x, y);
          if (t) {
            t->reservedForGathering = true;
          }
        };
        int relx = r->x - u->x;
        int rely = r->y - u->y;
        for (int i = 0; i != 8; ++i) {
          reserve(u->x + (relx * i / 8), u->y + (rely * i / 8));
          reserve(u->x + (relx * i / 8) - 8, u->y + (rely * i / 8) - 8);
          reserve(u->x + (relx * i / 8) + 8, u->y + (rely * i / 8) - 8);
          reserve(u->x + (relx * i / 8) + 8, u->y + (rely * i / 8) + 8);
          reserve(u->x + (relx * i / 8) - 8, u->y + (rely * i / 8) + 8);
        }
      }
    }
  }

  if (lastUpdateBuildings_ == 0 || frame - lastUpdateBuildings_ >= 4) {
    lastUpdateBuildings_ = frame;

    // This block sets the building field in tiles to any building that is
    // on that tile, and sets it back to null when the building is no longer
    // there.
    for (auto i = tileOccupyingBuildings_.begin();
         i != tileOccupyingBuildings_.end();) {
      auto& v = i->second;
      Unit* u = v.u;
      if (u->lifted() || u->gone || u->dead || u->type != v.type ||
          u->unit.pixel_x != v.pixelX || u->unit.pixel_y != v.pixelY) {
        for (Tile* t : v.tiles) {
          if (t->building == u)
            t->building = nullptr;
        }
        i = tileOccupyingBuildings_.erase(i);
      } else {
        for (Tile* t : v.tiles) {
          if (!t->building)
            t->building = u;
        }
        ++i;
      }
    }

    for (Unit* u : state_->unitsInfo().visibleBuildings()) {
      if (!u->lifted()) {
        auto in = tileOccupyingBuildings_.emplace(
            std::piecewise_construct, std::make_tuple(u), std::make_tuple());
        if (in.second) {
          auto& v = in.first->second;
          v.u = u;
          v.type = u->type;
          v.pixelX = u->unit.pixel_x;
          v.pixelY = u->unit.pixel_y;
          int left =
              (v.pixelX - v.type->dimensionLeft) / tc::BW::XYPixelsPerWalktile;
          int top =
              (v.pixelY - v.type->dimensionUp) / tc::BW::XYPixelsPerWalktile;
          int right =
              left + tc::BW::XYWalktilesPerBuildtile * v.type->tileWidth;
          int bottom =
              top + tc::BW::XYWalktilesPerBuildtile * v.type->tileHeight;
          VLOG(3) << "TilesInfo visible building "
                  << utils::buildTypeString(u->type) << ": "
                  << "top=" << top << ", left=" << left << ", bottom=" << bottom
                  << ", right=" << right;
          for (int y = top; y != bottom; y += tc::BW::XYWalktilesPerBuildtile) {
            for (int x = left; x != right;
                 x += tc::BW::XYWalktilesPerBuildtile) {
              Tile* t = tryGetTile(x, y);
              if (t) {
                v.tiles.push_back(t);
                if (!t->building)
                  t->building = u;
              }
            }
          }
          const BuildType* addon = nullptr;
          if (u->type == buildtypes::Terran_Command_Center) {
            addon = buildtypes::Terran_Comsat_Station;
          } else if (u->type == buildtypes::Terran_Factory) {
            addon = buildtypes::Terran_Machine_Shop;
          } else if (u->type == buildtypes::Terran_Starport) {
            addon = buildtypes::Terran_Control_Tower;
          } else if (u->type == buildtypes::Terran_Science_Facility) {
            addon = buildtypes::Terran_Physics_Lab;
          }
          if (addon) {
            int addonX = u->buildX +
                tc::BW::XYWalktilesPerBuildtile * u->type->tileWidth;
            int addonY = u->buildY +
                tc::BW::XYWalktilesPerBuildtile *
                    (u->type->tileHeight - addon->tileHeight);
            for (int y = 0; y != addon->tileHeight; ++y) {
              for (int x = 0; x != addon->tileWidth; ++x) {
                Tile* t = tryGetTile(
                    addonX + tc::BW::XYWalktilesPerBuildtile * x,
                    addonY + tc::BW::XYWalktilesPerBuildtile * y);
                if (t) {
                  v.tiles.push_back(t);
                  if (!t->building)
                    t->building = u;
                }
              }
            }
          }
        }
      }
    }

    auto blockTile = [&](int walkX, int walkY) {
      Tile* t = tryGetTile(walkX, walkY);
      if (t) {
        t->blockedUntil = std::max(t->blockedUntil, frame + 30);
      }
    };

    auto blockAt = [&](Unit* u) {
      blockTile(u->x, u->y);
      blockTile(u->x - 2, u->y - 2);
      blockTile(u->x + 2, u->y - 2);
      blockTile(u->x + 2, u->y + 2);
      blockTile(u->x - 2, u->y + 2);
    };

    for (Unit* u : state_->unitsInfo().myUnitsOfType(buildtypes::Zerg_Larva)) {
      blockAt(u);
    }
    for (Unit* u : state_->unitsInfo().myUnitsOfType(buildtypes::Zerg_Egg)) {
      blockAt(u);
    }
    for (Unit* u :
         state_->unitsInfo().myUnitsOfType(buildtypes::Zerg_Lurker_Egg)) {
      blockAt(u);
    }
  }
}

Tile& TilesInfo::getTile(int walkX, int walkY) {
  Tile* tile = tryGetTile(walkX, walkY);
  if (!tile) {
    throw std::runtime_error("attempt to get invalid tile");
  }
  return *tile;
}

const Tile& TilesInfo::getTile(int walkX, int walkY) const {
  return const_cast<TilesInfo*>(this)->getTile(walkX, walkY);
}

Tile* TilesInfo::tryGetTile(int walkX, int walkY) {
  unsigned tileX = walkX / (unsigned)tc::BW::XYWalktilesPerBuildtile;
  unsigned tileY = walkY / (unsigned)tc::BW::XYWalktilesPerBuildtile;
  if (tileX >= mapTileWidth() || tileY >= mapTileHeight()) {
    return nullptr;
  }
  return &tiles[tilesWidth * tileY + tileX];
}

const Tile* TilesInfo::tryGetTile(int walkX, int walkY) const {
  return const_cast<TilesInfo*>(this)->tryGetTile(walkX, walkY);
}

template <bool reserve>
static void
reserveAreaImpl(TilesInfo& tt, const BuildType* type, int walkX, int walkY) {
  unsigned beginX = walkX / (unsigned)tc::BW::XYWalktilesPerBuildtile;
  unsigned beginY = walkY / (unsigned)tc::BW::XYWalktilesPerBuildtile;
  unsigned endX = beginX + type->tileWidth;
  unsigned endY = beginY + type->tileHeight;
  if (beginX >= tt.mapTileWidth() || endX >= tt.mapTileWidth() ||
      beginY >= tt.mapTileHeight() || endY >= tt.mapTileHeight()) {
    if (reserve) {
      throw std::runtime_error("attempt to reserve area out of bounds");
    } else {
      throw std::runtime_error("attempt to unreserve area out of bounds");
    }
  }
  unsigned tileWidth = endX - beginX;
  unsigned tileHeight = endY - beginY;
  size_t stride = TilesInfo::tilesWidth - (endX - beginX);
  Tile* ptr = &tt.tiles[TilesInfo::tilesWidth * beginY + beginX];

  std::vector<Tile*> changedTiles;
  changedTiles.reserve(tileWidth * tileHeight);
  auto rollback = [&]() {
    for (auto* t : changedTiles) {
      t->reservedAsUnbuildable = !reserve;
    }
  };

  for (unsigned iy = tileHeight; iy; --iy, ptr += stride) {
    for (unsigned ix = tileWidth; ix; --ix, ++ptr) {
      Tile& t = *ptr;
      if (t.reservedAsUnbuildable != !reserve) {
        if (reserve) {
          rollback();
          throw std::runtime_error("attempt to reserve reserved tile");
        } else {
          rollback();
          throw std::runtime_error("attempt to unreserve unreserved tile");
        }
      }
      t.reservedAsUnbuildable = reserve;
      changedTiles.push_back(ptr);
    }
  }
}

void TilesInfo::reserveArea(const BuildType* type, int walkX, int walkY) {
  reserveAreaImpl<true>(*this, type, walkX, walkY);
}

void TilesInfo::unreserveArea(const BuildType* type, int walkX, int walkY) {
  reserveAreaImpl<false>(*this, type, walkX, walkY);
}

FrameNum Tile::expectsCreepBy() const {
  return expectsCreepUpdated_ >= lastSlowUpdate ? expectsCreepFrame_ : kForever;
}
} // namespace cherrypi
