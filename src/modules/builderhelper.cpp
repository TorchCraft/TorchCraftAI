/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "builderhelper.h"

#include "cherrypi.h"
#include "state.h"
#include "unitsinfo.h"
#include "utils.h"

#include <BWAPI.h>
#include <bwem/map.h>

#include <fstream>
#include <iomanip>

namespace cherrypi {
namespace builderhelpers {

DEFINE_bool(builderhelper_logfailure, false, "Always log failed placements");

namespace {

constexpr int kMaxGeyserToRefineryDistance = 4 * 12;

bool isInPsionicMatrixRange(int relX, int relY);
/// For a given unit type returns its addon type, or nullptr if cannot have
/// an addon. If multiple addons can be built, returns one of the addons.
/// @param type Type of the building
/// @return Type that corresponds to the addon, or nullptr, if the unit
///   cannot have an addon
const BuildType* getAddon(const BuildType* type);
/// Check whether the building can be placed at a tile. We always leave space
/// for addons.
bool canPlaceBuildingAtTile(
    State* state,
    BuildType const* type,
    UPCTuple const& upc,
    Tile const* tile);
/// Compute score for placing building at the specified tile
double scoreBuildingAtTile(
    cherrypi::State* state,
    const cherrypi::BuildType* type,
    const cherrypi::Tile* tile);

void fullReserveImpl(
    TilesInfo& tt,
    const BuildType* type,
    int x,
    int y,
    bool reserve) {
  const BuildType* addon = nullptr;
  if (type == buildtypes::Terran_Command_Center) {
    addon = buildtypes::Terran_Comsat_Station;
  } else if (type == buildtypes::Terran_Factory) {
    addon = buildtypes::Terran_Machine_Shop;
  } else if (type == buildtypes::Terran_Starport) {
    addon = buildtypes::Terran_Control_Tower;
  } else if (type == buildtypes::Terran_Science_Facility) {
    addon = buildtypes::Terran_Physics_Lab;
  }
  if (addon) {
    int addonX = x + tc::BW::XYWalktilesPerBuildtile * type->tileWidth;
    int addonY = y +
        tc::BW::XYWalktilesPerBuildtile *
            (type->tileHeight - addon->tileHeight);
    if (reserve) {
      tt.reserveArea(type, x, y);
      tt.reserveArea(addon, addonX, addonY);
    } else {
      tt.unreserveArea(type, x, y);
      tt.unreserveArea(addon, addonX, addonY);
    }
  } else {
    if (reserve)
      tt.reserveArea(type, x, y);
    else
      tt.unreserveArea(type, x, y);
  }
}

} // namespace

std::shared_ptr<UPCTuple> upcWithPositionForBuilding(
    State* state,
    UPCTuple const& upc,
    BuildType const* type) {
  Position upcPos;
  float prob;
  std::tie(upcPos, prob) = upc.positionArgMax();

  Position pos;
  if (prob > 0.99f) {
    VLOG(1) << "UPC with dirac position for " << utils::buildTypeString(type)
            << " at " << upcPos;

    // With a sharp position, try to build there. If we can't for some reason,
    // try to find a close by position.
    if (builderhelpers::canBuildAt(state, type, upcPos, false, true)) {
      pos = upcPos;
    } else {
      VLOG(1) << "Asked to build " << utils::buildTypeString(type) << " at "
              << upcPos
              << " but can't build there. Let's see if we can place it "
                 "somewhere close by";
      // Restrict backup UPC by area
      UPCTuple backupUpc(upc);
      backupUpc.position = state->areaInfo().tryGetArea(upcPos);
      pos = builderhelpers::findBuildLocation(
          state,
          {upcPos},
          type,
          backupUpc,
          [&](State* state, const BuildType* type, const Tile* tile) {
            return utils::distance(tile->x, tile->y, upcPos.x, upcPos.y);
          });
    }
  } else {
    // Otherwise, find a location according to heuristics defined in
    // builderhelpers and using the position probabilities in the UPC.
    if (type->isRefinery) {
      pos = builderhelpers::findRefineryLocation(state, type, upc);
    } else {
      auto seeds =
          builderhelpers::buildLocationSeeds(state, type, upc, nullptr);
      pos = builderhelpers::findBuildLocation(state, seeds, type, upc);
    }
  }

  if (pos.x == -1 && pos.y == -1) {
    VLOG(0) << "Build " << utils::buildTypeString(type)
            << ": failed to find build location";
    return nullptr;
  }
  VLOG(1) << "Found location for " << utils::buildTypeString(type) << ": "
          << pos;

  auto newUpc = std::make_shared<UPCTuple>(upc);
  newUpc->position = pos;
  newUpc->scale = 1;
  return newUpc;
}

template <typename ScoreFunc>
Position findBuildLocation(
    State* state,
    std::vector<Position> const& seeds,
    BuildType const* type,
    UPCTuple const& upc,
    ScoreFunc&& scoreFunc) {
  auto& tilesInfo = state->tilesInfo();
  if (tilesInfo.mapTileWidth() <= 1 || tilesInfo.mapTileHeight() <= 1) {
    return Position(-1, -1);
  }

  std::vector<uint8_t> visited(tilesInfo.tilesHeight * tilesInfo.tilesWidth);
  std::deque<const Tile*> open;

  for (auto const& loc : seeds) {
    if (loc.x < 0 || loc.y < 0) {
      continue;
    }
    size_t index = tilesInfo.tilesWidth *
            (loc.y / (unsigned)tc::BW::XYWalktilesPerBuildtile) +
        (loc.x / (unsigned)tc::BW::XYWalktilesPerBuildtile);
    if (index < tilesInfo.tiles.size()) {
      open.push_back(&tilesInfo.tiles.at(index));
      visited[index] = 1;
    }
  }

  constexpr size_t maxValidLocations = 64;
  constexpr int maxIterations = 1024;
  std::vector<const Tile*> validLocations;

  // This loop finds valid build locations (up to maxValidLocations of them).
  // The best one is selected and returned below.
  int iterations = 0;
  unsigned lastX = tilesInfo.mapTileWidth() - 1;
  unsigned lastY = tilesInfo.mapTileHeight() - 1;
  while (!open.empty() && iterations < maxIterations) {
    ++iterations;
    const Tile* tile = open.front();
    open.pop_front();

    if (canPlaceBuildingAtTile(state, type, upc, tile)) {
      validLocations.push_back(tile);
      if (validLocations.size() >= maxValidLocations) {
        break;
      }
    }

    auto add = [&](size_t index) {
      auto& v = visited[index];
      if (v != 0) {
        return;
      }
      v = 1;
      const Tile* newTile = &tilesInfo.tiles[index];
      if (!newTile->entirelyWalkable) {
        return;
      }
      open.push_back(newTile);
    };

    size_t index = tile - tilesInfo.tiles.data();
    unsigned tileX = index % TilesInfo::tilesWidth;
    unsigned tileY = index / TilesInfo::tilesWidth;

    if (tileX != 0) {
      add(index - 1);
    }
    if (tileY != 0) {
      add(index - TilesInfo::tilesWidth);
    }
    if (tileX != lastX) {
      add(index + 1);
    }
    if (tileY != lastY) {
      add(index + TilesInfo::tilesWidth);
    }
  }

  if (!validLocations.empty()) {
    const Tile* r = utils::getBestScoreCopy(validLocations, [&](const Tile* t) {
      return scoreFunc(state, type, t);
    });
    return Position(r->x, r->y);
  }

  return Position(-1, -1);
}

Position findBuildLocation(
    State* state,
    std::vector<Position> const& seeds,
    BuildType const* type,
    UPCTuple const& upc) {
  return findBuildLocation(state, seeds, type, upc, scoreBuildingAtTile);
}

Position findBuildLocation(
    State* state,
    std::vector<Position> const& seeds,
    BuildType const* type,
    UPCTuple const& upc,
    std::function<double(State*, const BuildType*, const Tile*)> scoreFunc) {
  return findBuildLocation<decltype(scoreFunc)&>(
      state, seeds, type, upc, scoreFunc);
}

bool canBuildAt(
    State* state,
    const BuildType* type,
    const Position& pos,
    bool ignoreReserved,
    bool logFailure) {
  logFailure = logFailure || FLAGS_builderhelper_logfailure;
  auto checkPsi = [state, type](int x, int y) {
    int centerPixelX = tc::BW::XYPixelsPerWalktile * x +
        tc::BW::XYPixelsPerBuildtile * type->tileWidth / 2;
    int centerPixelY = tc::BW::XYPixelsPerWalktile * y +
        tc::BW::XYPixelsPerBuildtile * type->tileHeight / 2;
    for (Unit* u :
         state->unitsInfo().myUnitsOfType(buildtypes::Protoss_Pylon)) {
      if (u->completed() || u->remainingBuildTrainTime <= 30) {
        if (isInPsionicMatrixRange(
                centerPixelX - u->unit.pixel_x,
                centerPixelY - u->unit.pixel_y)) {
          return true;
        }
      }
    }
    return false;
  };

  const auto& tt = state->tilesInfo();

  unsigned beginX = pos.x / (unsigned)tc::BW::XYWalktilesPerBuildtile;
  unsigned beginY = pos.y / (unsigned)tc::BW::XYWalktilesPerBuildtile;
  unsigned endX = beginX + type->tileWidth;
  unsigned endY = beginY + type->tileHeight;
  if (beginX >= tt.mapTileWidth() || endX >= tt.mapTileWidth()) {
    if (logFailure) {
      VLOG(0) << "Cannot build at " << pos << ": map to small";
    }
    return false;
  }
  if (beginY >= tt.mapTileHeight() || endY >= tt.mapTileHeight()) {
    if (logFailure) {
      VLOG(0) << "Cannot build at " << pos << ": map to small";
    }
    return false;
  }
  unsigned tileWidth = endX - beginX;
  unsigned tileHeight = endY - beginY;
  size_t stride = TilesInfo::tilesWidth - (endX - beginX);
  const Tile* ptr = &tt.tiles[TilesInfo::tilesWidth * beginY + beginX];
  bool isResourceDepot = type->isResourceDepot;
  bool requiresCreep = type->requiresCreep;
  bool requiresNotCreep = !requiresCreep && type != buildtypes::Zerg_Hatchery;
  bool requiresPsi = type->requiresPsi;
  bool isRefinery = type->isRefinery;
  bool isDefence = type->hasGroundWeapon || type->hasAirWeapon ||
      type == buildtypes::Zerg_Creep_Colony;
  int frame = state->currentFrame();
  int creepLookaheadFrame = frame + 24 * 9;
  auto illustrate = [&](const Tile& tile, int color) {
    if (VLOG_IS_ON(1) && logFailure) {
      utils::drawLine(
          state, {tile.x - 1, tile.y - 1}, {tile.x + 1, tile.y + 1}, color);
      utils::drawLine(
          state, {tile.x + 1, tile.y - 1}, {tile.x - 1, tile.y + 1}, color);
    }
  };
  for (unsigned iy = tileHeight; iy; --iy, ptr += stride) {
    for (unsigned ix = tileWidth; ix; --ix, ++ptr) {
      const Tile& tile = *ptr;
      if (!tile.buildable || (tile.reservedAsUnbuildable && !ignoreReserved)) {
        if (!tile.buildable) {
          illustrate(tile, tc::BW::Color::Grey);
          VLOG_IF(0, logFailure) << "Cannot build at " << pos
                                 << ": tile not buildable";
        }
        if (tile.reservedAsUnbuildable && !ignoreReserved) {
          illustrate(tile, tc::BW::Color::Yellow);
          VLOG_IF(0, logFailure) << "Cannot build at " << pos
                                 << ": tile reserved";
        }
        return false;
      }
      if (isRefinery) {
        if (tile.building == nullptr ||
            tile.building->type != buildtypes::Resource_Vespene_Geyser) {
          illustrate(tile, tc::BW::Color::Green);
          VLOG_IF(0, logFailure) << "Cannot build at " << pos
                                 << ": requires vespene geyser";
          return false;
        }
      }
      if (tile.building) {
        if (tile.building->type != buildtypes::Resource_Vespene_Geyser ||
            !isRefinery) {
          illustrate(tile, tc::BW::Color::Blue);
          VLOG_IF(0, logFailure) << "Cannot build at " << pos
                                 << ": contains building";
          return false;
        }
      }
      const int expectsCreepBy = tile.expectsCreepBy();
      if (requiresCreep && !tile.hasCreep &&
          expectsCreepBy > creepLookaheadFrame) {
        illustrate(tile, tc::BW::Color::Purple);
        VLOG_IF(0, logFailure)
            << "Cannot build at " << pos
            << ": requires creep but none is expected until frame "
            << expectsCreepBy << "(+" << expectsCreepBy - frame << ")";
        return false;
      }
      if (requiresNotCreep && tile.hasCreep) {
        illustrate(tile, tc::BW::Color::Purple);
        VLOG_IF(0, logFailure) << "Cannot build at " << pos
                               << ": requires non-creep but there is some";
        return false;
      }
      if (requiresPsi && !checkPsi(tile.x, tile.y)) {
        illustrate(tile, tc::BW::Color::Cyan);
        VLOG_IF(0, logFailure) << "Cannot build at " << pos
                               << ": requires psi but not present";
        return false;
      }
      if (!isResourceDepot && !isRefinery && !isDefence &&
          tile.reservedForGathering) {
        illustrate(tile, tc::BW::Color::Teal);
        VLOG_IF(0, logFailure) << "Cannot build at " << pos
                               << ": reserved for gathering";
        return false;
      }
      if (isResourceDepot) {
        if (tile.resourceDepotUnbuildable) {
          illustrate(tile, tc::BW::Color::Black);
          VLOG_IF(0, logFailure) << "Cannot build at " << pos
                                 << ": unbuildable for resource depot";
          return false;
        }
      } else {
        if (tile.reservedForResourceDepot) {
          illustrate(tile, tc::BW::Color::Brown);
          VLOG_IF(0, logFailure) << "Cannot build at " << pos
                                 << ": reserved for resource depot";
          return false;
        }
      }
      if (tile.blockedUntil > frame) {
        illustrate(tile, tc::BW::Color::Red);
        VLOG_IF(0, logFailure) << "Cannot build at " << pos
                               << ": is blocked until frame "
                               << tile.blockedUntil;
        return false;
      }
    }
  }
  return true;
}

bool checkCreepAt(State* state, const BuildType* type, const Position& pos) {
  const auto& tt = state->tilesInfo();

  unsigned beginX = pos.x / (unsigned)tc::BW::XYWalktilesPerBuildtile;
  unsigned beginY = pos.y / (unsigned)tc::BW::XYWalktilesPerBuildtile;
  unsigned endX = beginX + type->tileWidth;
  unsigned endY = beginY + type->tileHeight;
  if (beginX >= tt.mapTileWidth() || endX >= tt.mapTileWidth()) {
    return false;
  }
  if (beginY >= tt.mapTileHeight() || endY >= tt.mapTileHeight()) {
    return false;
  }
  if (type == buildtypes::Zerg_Hatchery) {
    return true;
  }
  unsigned tileWidth = endX - beginX;
  unsigned tileHeight = endY - beginY;
  size_t stride = TilesInfo::tilesWidth - (endX - beginX);
  const Tile* ptr = &tt.tiles[TilesInfo::tilesWidth * beginY + beginX];
  bool requiresCreep = type->requiresCreep;
  for (unsigned iy = tileHeight; iy; --iy, ptr += stride) {
    for (unsigned ix = tileWidth; ix; --ix, ++ptr) {
      const Tile& tile = *ptr;
      if (tile.hasCreep != requiresCreep) {
        return false;
      }
    }
  }
  return true;
}

Unit* findGeyserForRefinery(
    State* state,
    BuildType const* type,
    UPCTuple const& upc) {
  Unit* r = utils::getBestScoreCopy(
      state->unitsInfo().resourceUnits(),
      [&](Unit* r) {
        if (r->type != buildtypes::Resource_Vespene_Geyser) {
          return kdInfty;
        }
        if (!canBuildAt(state, type, Position(r->buildX, r->buildY))) {
          return kdInfty;
        }
        if (upc.positionProb(r->buildX, r->buildY) == 0.0f) {
          return kdInfty;
        }

        // Build at earlier bases first since they might have more worker units
        // available already. Bases are enumerated by when they were built, so
        // use that as a multiplicative factor below.
        auto baseIdx = state->areaInfo().myClosestBaseIdx(r->pos());

        double nearestDepotDistance = kdInfty;
        for (const Unit* depot : state->unitsInfo().myResourceDepots()) {
          if (depot->completed()) {
            double d = utils::distance(depot, r);
            if (d < nearestDepotDistance) {
              nearestDepotDistance = d;
            }
          }
        }

        if (nearestDepotDistance >= kMaxGeyserToRefineryDistance) {
          return kdInfty;
        }
        return nearestDepotDistance * (baseIdx + 1);
      },
      kdInfty);
  return r;
}

Position
findRefineryLocation(State* state, BuildType const* type, UPCTuple const& upc) {
  Unit* r = findGeyserForRefinery(state, type, upc);
  if (r) {
    return Position(
        (r->unit.pixel_x - r->type->dimensionLeft) /
            tc::BW::XYPixelsPerWalktile,
        (r->unit.pixel_y - r->type->dimensionUp) / tc::BW::XYPixelsPerWalktile);
  }
  return Position(-1, -1);
}

void dumpMap(
    const std::string& fname,
    std::function<bool(const Tile*)> predicate,
    State* state,
    const std::vector<Position>& candidateLocations) {
  std::vector<char> mapChars(
      TilesInfo::tilesHeight * TilesInfo::tilesWidth, '.');
  char* pBase = &mapChars[0];
  char* pLine = pBase;
  auto& tilesInfo = state->tilesInfo();
  for (auto y = 0U; y < tilesInfo.mapTileHeight(); ++y) {
    for (auto x = 0U; x < tilesInfo.mapTileWidth(); ++x) {
      Tile* t = tilesInfo.tryGetTile(
          x * tc::BW::XYWalktilesPerBuildtile,
          y * tc::BW::XYWalktilesPerBuildtile);
      if (t) {
        *(pLine + x) = (predicate(t) ? '*' : '.');
      } else {
        *(pLine + x) = 'x';
      }
    }
    pLine += TilesInfo::tilesWidth;
  }

  auto& unitsInfo = state->unitsInfo();
  for (auto* unit : unitsInfo.resourceUnits()) {
    int xBt = unit->x / tc::BW::XYWalktilesPerBuildtile;
    int yBt = unit->y / tc::BW::XYWalktilesPerBuildtile;
    if (xBt >= 0 && yBt >= 0 && (uint32_t)xBt < tilesInfo.mapTileWidth() &&
        (uint32_t)yBt < tilesInfo.mapTileHeight()) {
      mapChars[xBt + yBt * TilesInfo::tilesWidth] = 'R';
    }
  }

  for (auto* unit : unitsInfo.myUnits()) {
    int xBt = unit->x / tc::BW::XYWalktilesPerBuildtile;
    int yBt = unit->y / tc::BW::XYWalktilesPerBuildtile;
    if (xBt >= 0 && yBt >= 0 && (uint32_t)xBt < tilesInfo.mapTileWidth() &&
        (uint32_t)yBt < tilesInfo.mapTileHeight()) {
      mapChars[xBt + yBt * TilesInfo::tilesWidth] = 'U';
    }
  }

  for (auto& pos : candidateLocations) {
    int xBt = pos.x / tc::BW::XYWalktilesPerBuildtile;
    int yBt = pos.y / tc::BW::XYWalktilesPerBuildtile;
    if (xBt >= 0 && yBt >= 0 && (uint32_t)xBt < tilesInfo.mapTileWidth() &&
        (uint32_t)yBt < tilesInfo.mapTileHeight()) {
      mapChars[xBt + yBt * TilesInfo::tilesWidth] = 'P';
    }
  }

  std::ofstream ofs(fname);
  pLine = pBase;
  for (auto y = 0U; y < tilesInfo.mapTileHeight(); ++y) {
    char* pc = pLine;
    for (auto x = 0U; x < tilesInfo.mapTileWidth(); ++x) {
      ofs << *pc++;
    }
    ofs << std::endl;
    pLine += TilesInfo::tilesWidth;
  }
}

Position findResourceDepotLocation(
    State* state,
    const BuildType* type,
    const std::vector<Position>& candidateLocations,
    bool isExpansion) {
  for (const auto& locCentre : candidateLocations) {
    Position loc(
        locCentre.x - type->dimensionLeft / tc::BW::XYPixelsPerWalktile,
        locCentre.y - type->dimensionUp / tc::BW::XYPixelsPerWalktile);
    if (canBuildAt(state, type, loc)) {
      VLOG(3) << "can build resource depot at x=" << loc.x << ", y=" << loc.y;
      if (VLOG_IS_ON(3)) {
        VLOG(3) << std::endl << buildLocationMasks(state, type, loc);
      }
      return loc;
    }
    VLOG(3) << "cannot build resource depot at x=" << loc.x << ", y=" << loc.y;
    if (VLOG_IS_ON(3)) {
      VLOG(3) << std::endl << buildLocationMasks(state, type, loc);
    }
  }
  return Position(-1, -1);
}

std::vector<Position> candidateExpansionResourceDepotLocations(State* state) {
  typedef std::pair<Position, int> PosDist;
  std::list<PosDist> candDistList =
      candidateExpansionResourceDepotLocationsDistances(state);
  std::vector<Position> candidates;
  std::transform(
      candDistList.begin(),
      candDistList.end(),
      candidates.begin(),
      [](const PosDist& posDist) { return posDist.first; });
  return candidates;
}

std::list<std::pair<Position, int>>
candidateExpansionResourceDepotLocationsDistances(State* state) {
  typedef std::pair<Position, int> PosDist;
  std::list<PosDist> candDistList;
  Position myBaseLocWalktiles;
  try {
    myBaseLocWalktiles = state->areaInfo().myStartLocation();
  } catch (const std::runtime_error& e) {
    LOG(WARNING) << "could not propose candidate resource depot locations "
                    "- self main base area unknown";
    return candDistList;
  }
  BWAPI::WalkPosition myBaseLocWalktilesBwapi(
      myBaseLocWalktiles.x, myBaseLocWalktiles.y);
  BWAPI::Position myBaseLocPxBwapi(myBaseLocWalktilesBwapi);
  const auto* bwemMap = state->map();
  int length;
  for (const auto& area : bwemMap->Areas()) {
    for (const auto& base : area.Bases()) {
      const auto& baseLocPx = base.Center();
      bwemMap->GetPath(myBaseLocPxBwapi, baseLocPx, &length);
      if (length >= 0) {
        Position baseLocWalkTiles(
            baseLocPx.x / tc::BW::XYPixelsPerWalktile,
            baseLocPx.y / tc::BW::XYPixelsPerWalktile);
        candDistList.push_back(std::make_pair(baseLocWalkTiles, length));
      }
    }
  }
  candDistList.sort(
      [](const PosDist& a, const PosDist& b) { return a.second < b.second; });
  return candDistList;
}

std::vector<Position> buildLocationSeeds(
    State* state,
    BuildType const* type,
    UPCTuple const& upc,
    Unit* builder) {
  std::vector<Position> buildLocationSeeds;

  // Use locations from UPC as seeds
  if (upc.position.is<Position>()) {
    buildLocationSeeds.emplace_back(
        upc.position.get_unchecked<Position>() * upc.scale);
  } else if (upc.position.is<Area*>()) {
    for (auto& basePos : upc.position.get_unchecked<Area*>()->baseLocations) {
      buildLocationSeeds.emplace_back(basePos.x, basePos.y);
    }
  }

  // Use builder and existing resource depots
  if (builder) {
    if (upc.positionProb(builder->x, builder->y) > 0.0f) {
      buildLocationSeeds.emplace_back(builder->x, builder->y);
    }
  }
  for (const Unit* depot : state->unitsInfo().myResourceDepots()) {
    if (upc.positionProb(depot->x, depot->y) > 0.0f) {
      buildLocationSeeds.emplace_back(depot->x, depot->y);
      if (state->unitsInfo().myResourceDepots().size() <= 3 ||
          state->unitsInfo().myWorkers().size() < 30) {
        break;
      }
    }
  }
  if (buildLocationSeeds.empty()) {
    for (const Unit* worker : state->unitsInfo().myWorkers()) {
      buildLocationSeeds.emplace_back(worker->x, worker->y);
    }
  }
  return buildLocationSeeds;
}

std::string
buildLocationMasks(State* state, const BuildType* type, const Position& pos) {
  std::ostringstream oss;

  constexpr int delta = 3;
  const int beginX = pos.x / (unsigned)tc::BW::XYWalktilesPerBuildtile;
  const int beginY = pos.y / (unsigned)tc::BW::XYWalktilesPerBuildtile;
  const int endX = beginX + type->tileWidth;
  const int endY = beginY + type->tileHeight;
  const unsigned tileWidth = static_cast<unsigned>(endX - beginX);
  const unsigned tileHeight = static_cast<unsigned>(endY - beginY);
  const int beginXD = beginX - delta;
  const int beginYD = beginY - delta;
  const int endXD = endX + delta;
  const int endYD = endY + delta;
  const unsigned tileWidthD = static_cast<unsigned>(endXD - beginXD);
  const unsigned tileHeightD = static_cast<unsigned>(endYD - beginYD);
  const auto& tt = state->tilesInfo();
  const Tile* const ptrBase =
      &tt.tiles[TilesInfo::tilesWidth * beginYD + beginXD];

  std::map<std::string, std::function<bool(const Tile*)>> field2GetterMap;
  field2GetterMap["buildable"] = [](const Tile* t) { return t->buildable; };
  field2GetterMap["building"] = [](const Tile* t) {
    return (t->building != nullptr);
  };
  field2GetterMap["reservedUnbuild"] = [](const Tile* t) {
    return t->reservedAsUnbuildable;
  };
  field2GetterMap["depotUnbuild"] = [](const Tile* t) {
    return t->resourceDepotUnbuildable;
  };
  field2GetterMap["reservedDepot"] = [](const Tile* t) {
    return t->reservedForResourceDepot;
  };

  constexpr unsigned kMaxLineLength = 300;
  unsigned lineLength = 0;
  std::list<std::string> fields;
  auto dumpFields = [&]() {
    // output header
    for (const auto& field : fields) {
      auto fieldWidth = std::max<unsigned>(tileWidthD, field.length());
      oss << std::setfill(' ') << std::setw(fieldWidth) << field << ' ';
    }
    oss << std::endl;
    // output masks
    const Tile* ptrLine = ptrBase;
    for (unsigned i = 0; i < tileHeightD; ++i) {
      for (const auto& field : fields) {
        auto fieldWidth = std::max<unsigned>(tileWidthD, field.length());
        std::ostringstream maskLine;
        const Tile* ptr = ptrLine;
        for (unsigned j = 0; j < tileWidthD; ++j) {
          if ((beginXD + i >= tt.mapTileWidth()) ||
              (beginYD + j >= tt.mapTileHeight())) {
            maskLine << 'X';
          } else if (
              i < delta || j < delta || i >= tileHeight + delta ||
              j >= tileWidth + delta) {
            bool val = field2GetterMap[field](ptr);
            maskLine << (val ? '+' : '-');
          } else {
            bool val = field2GetterMap[field](ptr);
            maskLine << (val ? '1' : '0');
          }
          ++ptr;
        }
        oss << std::setfill(' ') << std::setw(fieldWidth) << maskLine.str()
            << ' ';
      }
      ptrLine += TilesInfo::tilesWidth;
      oss << std::endl;
    }
    // clear buffer
    lineLength = 0;
    fields.clear();
  };

  for (const auto& field2Getter : field2GetterMap) {
    auto fieldWidth =
        std::max<unsigned>(tileWidthD, field2Getter.first.length());
    if (lineLength + fieldWidth + 1 > kMaxLineLength) {
      dumpFields();
    }
    fields.push_back(field2Getter.first);
    lineLength += fieldWidth + 1;
  }
  if (!fields.empty()) {
    dumpFields();
  }
  return oss.str();
}

void fullReserve(TilesInfo& tt, BuildType const* type, Position const& pos) {
  fullReserveImpl(tt, type, pos.x, pos.y, true);
}

void fullUnreserve(TilesInfo& tt, BuildType const* type, Position const& pos) {
  fullReserveImpl(tt, type, pos.x, pos.y, false);
}

namespace {

using namespace cherrypi;
using namespace cherrypi::builderhelpers;

/**
 * This function checks that the group of buildings at the specified position
 * does not block paths, each other, etc. This includes reserved tiles, so one
 * can reserve some tiles then call this to see if the base layout is still
 * good.
 */
bool buildingLayoutValid(
    cherrypi::State* state,
    const cherrypi::Position& startPos) {
  const auto& tt = state->tilesInfo();

  enum {
    vNotVisited,
    vOccupiedVisited,
    vNeighboringFreeTileNotVisited,
    vNeighboringFreeTileVisited,
  };

  std::vector<uint8_t> visited(tt.tilesHeight * tt.tilesWidth);
  std::deque<const Tile*> open;

  unsigned tileWidthLimit = 12;
  unsigned tileHeightLimit = 12;

  unsigned startTileX = startPos.x / (unsigned)tc::BW::XYWalktilesPerBuildtile;
  unsigned startTileY = startPos.y / (unsigned)tc::BW::XYWalktilesPerBuildtile;

  unsigned minTileX = startTileX;
  unsigned maxTileX = startTileX;
  unsigned minTileY = startTileY;
  unsigned maxTileY = startTileY;

  const Tile* firstEmptyTile = nullptr;
  size_t neighboringEmptyTileCount = 0;

  unsigned lastX = tt.mapTileWidth() - 1;
  unsigned lastY = tt.mapTileHeight() - 1;

  // First check that the group of buildings do not span a too large area,
  // or that we are building next to any unbuildable terrain.

  size_t startIndex = tt.tilesWidth * startTileY + startTileX;
  open.push_back(&tt.tiles.at(startIndex));
  visited[startIndex] = vOccupiedVisited;
  while (!open.empty()) {
    const Tile* t = open.front();
    open.pop_front();

    auto add = [&](size_t index) {
      auto& v = visited[index];
      if (v != vNotVisited) {
        return true;
      }
      v = vOccupiedVisited;
      const Tile* newTile = &tt.tiles[index];
      if (newTile->entirelyWalkable && !newTile->building &&
          !newTile->reservedAsUnbuildable) {
        if (!firstEmptyTile) {
          firstEmptyTile = newTile;
        }
        v = vNeighboringFreeTileNotVisited;
        ++neighboringEmptyTileCount;
        return true;
      }
      if (newTile->buildable) {
        open.push_back(newTile);
      }
      return true;
    };

    size_t index = t - tt.tiles.data();
    unsigned tileX = index % TilesInfo::tilesWidth;
    unsigned tileY = index / TilesInfo::tilesWidth;

    if (tileX < minTileX) {
      minTileX = tileX;
      if (1 + maxTileX - minTileX > tileWidthLimit) {
        return false;
      }
    }
    if (tileX > maxTileX) {
      maxTileX = tileX;
      if (1 + maxTileX - minTileX > tileWidthLimit) {
        return false;
      }
    }
    if (tileY < minTileY) {
      minTileY = tileY;
      if (1 + maxTileY - minTileY > tileHeightLimit) {
        return false;
      }
    }
    if (tileY > maxTileY) {
      maxTileY = tileY;
      if (1 + maxTileY - minTileY > tileHeightLimit) {
        return false;
      }
    }

    if (tileX == 0 || tileX == lastX) {
      return false;
    }
    if (tileY == 0 || tileY == lastY) {
      return false;
    }

    if (!add(index - 1)) {
      return false;
    }
    if (!add(index - 1 - TilesInfo::tilesWidth)) {
      return false;
    }
    if (!add(index - TilesInfo::tilesWidth)) {
      return false;
    }
    if (!add(index + 1 - TilesInfo::tilesWidth)) {
      return false;
    }
    if (!add(index + 1)) {
      return false;
    }
    if (!add(index + 1 + TilesInfo::tilesWidth)) {
      return false;
    }
    if (!add(index + TilesInfo::tilesWidth)) {
      return false;
    }
    if (!add(index - 1 + TilesInfo::tilesWidth)) {
      return false;
    }
  }

  // Then check that we did not wall in any empty tiles.

  if (!firstEmptyTile) {
    return true;
  }
  open.push_back(firstEmptyTile);
  visited[firstEmptyTile - tt.tiles.data()] = vNeighboringFreeTileVisited;
  --neighboringEmptyTileCount;
  while (!open.empty()) {
    const Tile* t = open.front();
    open.pop_front();

    auto add = [&](size_t index) {
      auto& v = visited[index];
      if (v != vNeighboringFreeTileNotVisited) {
        return;
      }
      v = vNeighboringFreeTileVisited;
      --neighboringEmptyTileCount;
      const Tile* newTile = &tt.tiles[index];
      open.push_back(newTile);
    };

    size_t index = t - tt.tiles.data();
    unsigned tileX = index % TilesInfo::tilesWidth;
    unsigned tileY = index / TilesInfo::tilesWidth;

    if (tileX != 0) {
      add(index - 1);
    }
    if (tileY != 0) {
      add(index - TilesInfo::tilesWidth);
    }
    if (tileX != lastX) {
      add(index + 1);
    }
    if (tileY != lastY) {
      add(index + TilesInfo::tilesWidth);
    }
  }
  if (neighboringEmptyTileCount) {
    return false;
  }

  return true;
}

const bool psiFieldMask[5][8] = {{1, 1, 1, 1, 1, 1, 1, 1},
                                 {1, 1, 1, 1, 1, 1, 1, 1},
                                 {1, 1, 1, 1, 1, 1, 1, 0},
                                 {1, 1, 1, 1, 1, 1, 0, 0},
                                 {1, 1, 1, 0, 0, 0, 0, 0}};

bool isInPsionicMatrixRange(int relX, int relY) {
  unsigned x = std::abs(relX);
  unsigned y = std::abs(relY);
  if (x >= 256 || y >= 160) {
    return false;
  }
  if (relX < 0) {
    --x;
  }
  if (relY < 0) {
    --y;
  }
  return psiFieldMask[y / 32u][x / 32u];
}

const BuildType* getAddon(const BuildType* type) {
  if (type == buildtypes::Terran_Command_Center) {
    return buildtypes::Terran_Comsat_Station;
  } else if (type == buildtypes::Terran_Factory) {
    return buildtypes::Terran_Machine_Shop;
  } else if (type == buildtypes::Terran_Starport) {
    return buildtypes::Terran_Control_Tower;
  } else if (type == buildtypes::Terran_Science_Facility) {
    return buildtypes::Terran_Physics_Lab;
  }
  return nullptr;
}

bool canPlaceBuildingAtTile(
    State* state,
    BuildType const* type,
    UPCTuple const& upc,
    Tile const* tile) {
  if (!canBuildAt(state, type, Position(tile->x, tile->y))) {
    return false;
  }
  if (upc.positionProb(tile->x, tile->y) == 0.0f) {
    return false;
  }

  auto& tilesInfo = state->tilesInfo();
  const BuildType* addon = getAddon(type);
  if (addon) {
    int addonX = tile->x + tc::BW::XYWalktilesPerBuildtile * type->tileWidth;
    int addonY = tile->y +
        tc::BW::XYWalktilesPerBuildtile *
            (type->tileHeight - addon->tileHeight);
    if (!canBuildAt(state, addon, Position(addonX, addonY))) {
      return false;
    }

    tilesInfo.reserveArea(type, tile->x, tile->y);
    tilesInfo.reserveArea(addon, addonX, addonY);
    bool ok = buildingLayoutValid(state, {tile->x, tile->y});
    tilesInfo.unreserveArea(type, tile->x, tile->y);
    tilesInfo.unreserveArea(addon, addonX, addonY);

    return ok;
  } else {
    tilesInfo.reserveArea(type, tile->x, tile->y);
    bool ok = buildingLayoutValid(state, {tile->x, tile->y});
    tilesInfo.unreserveArea(type, tile->x, tile->y);

    return ok;
  }
}

double scoreBuildingAtTile(
    cherrypi::State* state,
    const cherrypi::BuildType* type,
    const cherrypi::Tile* tile) {
  auto& tilesInfo = state->tilesInfo();
  int neighboringOccupiedTiles = 0;
  double r = 0.0;

  // Sum up the score for each neighboring tile.
  auto visit = [&](int x, int y) {
    const Tile* nt = tilesInfo.tryGetTile(x, y);
    if (!nt) {
      // Edge of map is good
      r -= 1.0;
      return;
    }
    if ((nt->building && nt->building->isMine) || nt->reservedAsUnbuildable) {
      // Next to other buildings is good
      ++neighboringOccupiedTiles;
      r -= 1.0;
    }
    if (nt->building && nt->building->isMine) {
      if (type->canProduce != nt->building->type->canProduce) {
        // We don't want to place buildings that produce units next to
        // buildings that don't produce units.
        r += 1.5;
      }
      if (nt->building->buildX == tile->x &&
          type->tileWidth == nt->building->type->tileWidth) {
        // We love placing buildings such that they align to a
        // neighboring building horizontally...
        r -= 1000.0;
      }
      if (nt->building->buildY == tile->y &&
          type->tileHeight == nt->building->type->tileHeight) {
        // ...or vertically
        r -= 1000.0;
      }
    }
  };

  int w = type->tileWidth;
  int h = type->tileHeight;

  for (int x = 0; x != w; ++x) {
    visit(
        tile->x + tc::BW::XYWalktilesPerBuildtile * x,
        tile->y - tc::BW::XYWalktilesPerBuildtile);
    visit(
        tile->x + tc::BW::XYWalktilesPerBuildtile * x,
        tile->y + tc::BW::XYWalktilesPerBuildtile * h);
  }
  for (int y = 0; y != h; ++y) {
    visit(
        tile->x - tc::BW::XYWalktilesPerBuildtile,
        tile->y + tc::BW::XYWalktilesPerBuildtile * y);
    visit(
        tile->x + tc::BW::XYWalktilesPerBuildtile * w,
        tile->y + tc::BW::XYWalktilesPerBuildtile * y);
  }

  if (type == buildtypes::Zerg_Hatchery) {
    // Prefer not to place hatcheries next to anything, to have some space
    // for larva.
    r += neighboringOccupiedTiles * 4;
  }

  if (tile->reservedForGathering) {
    bool isDefence = type->hasGroundWeapon || type->hasAirWeapon ||
        type == buildtypes::Zerg_Creep_Colony;
    if (isDefence) {
      // We're allowed to place defence buildings in resource gathering lines
      // but we don't really like to do it unless asked for it specifically.
      r += 50000.0f;
    }
  }

  return r;
}

} // namespace
} // namespace builderhelpers
} // namespace cherrypi
