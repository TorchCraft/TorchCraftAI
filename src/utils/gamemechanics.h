/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <algorithm>
#include <cfloat>
#include <vector>

#include <torchcraft/client.h>

#include "cherrypi.h"
#include "debugging.h"
#include "filter.h"
#include "state.h"
#include "unitsinfo.h"
#include "utils.h"
#include <math.h>

namespace cherrypi {
namespace utils {

/// Approximation of Euclidian distance
/// This is the same approximation that StarCraft's engine uses
/// and thus should be more accurate than true Euclidian distance
inline unsigned int disthelper(unsigned int dx, unsigned int dy) {
  // Helper takes and returns pixels
  if (dx < dy) {
    std::swap(dx, dy);
  }
  if (dx / 4u < dy) {
    dx = dx - dx / 16u + dy * 3u / 8u - dx / 64u + dy * 3u / 256u;
  }
  return dx;
}

/// Pixel distance
inline unsigned int pxdistance(int px1, int py1, int px2, int py2) {
  unsigned int dx = std::abs(px1 - px2);
  unsigned int dy = std::abs(py1 - py2);
  return disthelper(dx, dy);
}

/// Walktile distance
inline float distance(int x1, int y1, int x2, int y2) {
  unsigned int dx = std::abs(x1 - x2) * unsigned(tc::BW::XYPixelsPerWalktile);
  unsigned int dy = std::abs(y1 - y2) * unsigned(tc::BW::XYPixelsPerWalktile);
  return float(disthelper(dx, dy)) / tc::BW::XYPixelsPerWalktile;
}

/// Walktile distance
inline float distance(Unit const* a, Unit const* b) {
  return distance(a->x, a->y, b->x, b->y);
}

/// Walktile distance
inline float distance(Position const& a, Position const& b) {
  return distance(a.x, a.y, b.x, b.y);
}

/// Walktile distance
inline float distance(Unit const* a, Position const& b) {
  return distance(a->x, a->y, b.x, b.y);
}

/// Walktile distance
inline float distance(Position const& a, Unit const* b) {
  return distance(a.x, a.y, b->x, b->y);
}

/// Distance between two bounding boxes, in pixels.
/// Brood War uses bounding boxes for both collisions and range checks
inline int pxDistanceBB(
    int xminA,
    int yminA,
    int xmaxA,
    int ymaxA,
    int xminB,
    int yminB,
    int xmaxB,
    int ymaxB) {
  if (xmaxB < xminA) { // To the left
    if (ymaxB < yminA) { // Fully above
      return pxdistance(xmaxB, ymaxB, xminA, yminA);
    } else if (yminB > ymaxA) { // Fully below
      return pxdistance(xmaxB, yminB, xminA, ymaxA);
    } else { // Adjecent
      return xminA - xmaxB;
    }
  } else if (xminB > xmaxA) { // To the right
    if (ymaxB < yminA) { // Fully above
      return pxdistance(xminB, ymaxB, xmaxA, yminA);
    } else if (yminB > ymaxA) { // Fully below
      return pxdistance(xminB, yminB, xmaxA, ymaxA);
    } else { // Adjecent
      return xminB - xmaxA;
    }
  } else if (ymaxB < yminA) { // Above
    return yminA - ymaxB;
  } else if (yminB > ymaxA) { // Below
    return yminB - ymaxA;
  }

  return 0;
}

inline int pxDistanceBB(Unit const* a, Unit const* b) {
  return pxDistanceBB(
      a->unit.pixel_x - a->type->dimensionLeft,
      a->unit.pixel_y - a->type->dimensionUp,
      a->unit.pixel_x + a->type->dimensionRight,
      a->unit.pixel_y + a->type->dimensionDown,
      b->unit.pixel_x - b->type->dimensionLeft,
      b->unit.pixel_y - b->type->dimensionUp,
      b->unit.pixel_x + b->type->dimensionRight,
      b->unit.pixel_y + b->type->dimensionDown);
}

inline float distanceBB(Unit const* a, Unit const* b) {
  return float(pxDistanceBB(a, b)) / tc::BW::XYPixelsPerWalktile;
}

// Bounding box distance given that unit a is in position a and unit b is in
// position b.
template <typename T>
inline float distanceBB(
    Unit const* a,
    Vec2T<T> const& pa,
    Unit const* b,
    Vec2T<T> const& pb) {
  return float(pxDistanceBB(
             int(pa.x * tc::BW::XYPixelsPerWalktile - a->type->dimensionLeft),
             int(pa.y * tc::BW::XYPixelsPerWalktile - a->type->dimensionUp),
             int(pa.x * tc::BW::XYPixelsPerWalktile + a->type->dimensionRight),
             int(pa.y * tc::BW::XYPixelsPerWalktile + a->type->dimensionDown),
             int(pb.x * tc::BW::XYPixelsPerWalktile - b->type->dimensionLeft),
             int(pb.y * tc::BW::XYPixelsPerWalktile - b->type->dimensionUp),
             int(pb.x * tc::BW::XYPixelsPerWalktile + b->type->dimensionRight),
             int(pb.y * tc::BW::XYPixelsPerWalktile +
                 b->type->dimensionDown))) /
      tc::BW::XYPixelsPerWalktile;
}

/// Predict the position of a unit some frames into the future
inline Position predictPosition(Unit const* unit, double frames) {
  return Position(
      static_cast<int>(unit->x + frames * unit->unit.velocityX),
      static_cast<int>(unit->y + frames * unit->unit.velocityY));
}

// Get movement towards position p, rotated by angle in degrees.
// If not exact, we click past it so we maintain flyer acceleration
// Positive angle rotates from the top right to the bottom left corner,
// since the y axis points down.
inline Position getMovePosHelper(
    int ux,
    int uy,
    int px,
    int py,
    int mx,
    int my,
    double angle,
    bool exact) {
  auto fdirX = px - ux;
  auto fdirY = py - uy;
  if (fdirX == 0 && fdirY == 0) {
    return Position(px, py);
  }
  auto rad = angle * kDegPerRad;
  auto c = std::cos(rad);
  auto s = std::sin(rad);
  auto dirX = fdirX * c - fdirY * s;
  auto dirY = fdirX * s + fdirY * c;
  if (!exact && dirX * dirX + dirY * dirY < 10) {
    // Approximate, I don't want to compute the magnitude
    // Clicks at least 10 walktiles ahead
    auto div = std::abs(dirX == 0 ? dirY : dirX);
    dirX = dirX / div * 10;
    dirY = dirY / div * 10;
  }
  return Position(
      utils::clamp(ux + (int)dirX, 0, mx - 1),
      utils::clamp(uy + (int)dirY, 0, my - 1));
}
inline Position getMovePos(
    const State* const state,
    const Unit* const u,
    Position p,
    double angle = 0,
    bool exact = true) {
  return getMovePosHelper(
      u->x,
      u->y,
      p.x,
      p.y,
      state->mapWidth(),
      state->mapHeight(),
      angle,
      exact);
}
inline Position getMovePos(
    const State* const state,
    const Unit* const u,
    const Unit* const p,
    double angle = 0,
    bool exact = true) {
  return getMovePosHelper(
      u->x,
      u->y,
      p->x,
      p->y,
      state->mapWidth(),
      state->mapHeight(),
      angle,
      exact);
}

inline Position clampPositionToMap(
    const State* const state,
    int const x,
    int const y,
    bool strict = false) {
  auto cx = utils::clamp(x, 1, state->mapWidth() - 1);
  auto cy = utils::clamp(y, 1, state->mapHeight() - 1);
  if (strict && (cx != x || cy != y)) {
    return Position(-1, -1);
  }
  return Position(cx, cy);
}

inline Position clampPositionToMap(
    const State* const state,
    Position const& pos,
    bool strict = false) {
  return clampPositionToMap(state, pos.x, pos.y, strict);
}

inline bool isWorker(tc::Unit const& unit) {
  auto ut = tc::BW::UnitType::_from_integral_nothrow(unit.type);
  if (ut) {
    return tc::BW::isWorker(*ut);
  }
  return false;
}

inline bool isBuilding(tc::Unit const& unit) {
  auto ut = tc::BW::UnitType::_from_integral_nothrow(unit.type);
  if (ut) {
    return tc::BW::isBuilding(*ut);
  }
  return false;
}

inline std::vector<tc::Unit> getWorkers(tc::State* state) {
  return utils::filterUnitsByType(
      state->units[state->player_id],
      static_cast<bool (*)(tc::BW::UnitType)>(&tc::BW::isWorker));
}

inline std::vector<tc::Unit> getMineralFields(tc::State* state) {
  return utils::filterUnitsByType(
      state->units[state->neutral_id], tc::BW::isMineralField);
}

// x,y in walktiles
inline bool isBuildable(tc::State* state, int x, int y) {
  if (x < 0 || y < 0 || x >= state->map_size[0] || y >= state->map_size[1]) {
    return false;
  }
  return state->buildable_data[y * state->map_size[0] + x];
}

inline bool prerequisitesReady(State* state, const BuildType* buildType) {
  auto& unitsInfo = state->unitsInfo();
  for (auto* prereq : buildType->prerequisites) {
    if (prereq->isUnit()) {
      bool hasPrereq = !unitsInfo.myCompletedUnitsOfType(prereq).empty();
      if (!hasPrereq) {
        if (prereq == buildtypes::Zerg_Spire) {
          hasPrereq =
              !unitsInfo.myUnitsOfType(buildtypes::Zerg_Greater_Spire).empty();
        } else if (prereq == buildtypes::Zerg_Hatchery) {
          hasPrereq =
              !unitsInfo.myCompletedUnitsOfType(buildtypes::Zerg_Lair).empty();
          if (!hasPrereq) {
            hasPrereq = !unitsInfo.myUnitsOfType(buildtypes::Zerg_Hive).empty();
          }
        } else if (prereq == buildtypes::Zerg_Lair) {
          hasPrereq = !unitsInfo.myUnitsOfType(buildtypes::Zerg_Hive).empty();
        }
      }
      if (!hasPrereq) {
        return false;
      }
    } else if (prereq->isUpgrade()) {
      if (state->getUpgradeLevel(prereq) < prereq->level) {
        return false;
      }
    } else if (prereq->isTech()) {
      if (!state->hasResearched(prereq)) {
        return false;
      }
    } else {
      VLOG(2) << "Unknown prerequisite " << buildTypeString(prereq) << " for "
              << buildTypeString(buildType);
      return false;
    }
  }
  return true;
}

} // namespace utils
} // namespace cherrypi
