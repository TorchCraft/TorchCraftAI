/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "buildtype.h"
#include "cherrypi.h"

#include <functional>
#include <list>
#include <vector>

namespace cherrypi {

class State;
class TilesInfo;
struct Tile;
struct UPCTuple;
struct Unit;

namespace builderhelpers {

/// Refine a building UPC by selecting a dirac location according.
/// This will use a combination of the individual rules defined in this
/// namespace.
/// @param upc Source UPC
/// @param type A concrete building type selected from the source UPC
std::shared_ptr<UPCTuple> upcWithPositionForBuilding(
    State* state,
    UPCTuple const& upc,
    BuildType const* type);

/// Find location to construct the building
/// @param state Bot's state
/// @param seeds Seed locations for building location search
/// @param type Type of the building
/// @param upc UPCTuple with possible restrictions regarding position
/// @return Location to construct the building; (-1, -1) if no suitable
///   location was found
Position findBuildLocation(
    State* state,
    std::vector<Position> const& seeds,
    BuildType const* type,
    UPCTuple const& upc);

Position findBuildLocation(
    State* state,
    std::vector<Position> const& seeds,
    BuildType const* type,
    UPCTuple const& upc,
    std::function<double(State*, const BuildType*, const Tile*)> scoreFunc);

/// Check whether the building can be constructed at specified location
/// @param state Bot's state
/// @param type Type of the building
/// @param pos Location to be checked
bool canBuildAt(
    State* state,
    const BuildType* type,
    const Position& pos,
    bool ignoreReserved = false,
    bool logFailure = false);

/// Check whether there is enough creep for the building at the specified
/// position.
/// If the building does not require creep, checks that there isn't any.
/// Does not anticipate creep.
/// @param state Bot's state
/// @param type Type of the building
/// @param pos Location to be checked
bool checkCreepAt(State* state, const BuildType* type, const Position& pos);

/// Find a free Vespene Geyser for a refinery
/// @param state Bot's state
/// @param type Building type of the refinery
/// @param upc UPCTuple with possible restrictions regarding position
/// @return Unit that corresponds to the Vespene Geyser on which to
/// build the refinery; nullptr if no suitable Vespene Geyser could be
/// found
Unit* findGeyserForRefinery(
    State* state,
    BuildType const* type,
    UPCTuple const& upc);

/// Find Vespene Geyser location for a refinery
/// @param state Bot's state
/// @param type Building type of the refinery
/// @param upc UPCTuple with possible restrictions regarding position
/// @return Location that corresponds to the Vespene Geyser on which to
/// build the refinery; (-1, -1) if no suitable location could be
/// found
Position
findRefineryLocation(State* state, BuildType const* type, UPCTuple const& upc);

/// Find location for a new resource depot
/// @param state Bot's state
/// @param type Type of the building
/// @param candidateLocations Candidate locations for resource depots,
///   sorted in the order of location preference - the most preferred
///   comes first
/// @return Proposed resource depot location (in walktiles) or (-1, -1),
///  if no suitable location was found
Position findResourceDepotLocation(
    State* state,
    const BuildType* type,
    const std::vector<Position>& candidateLocations,
    bool isExpansion = true);

/// Use map information to produce candidate resource depot locations
/// sorted by their proximity to the main base
std::vector<Position> candidateExpansionResourceDepotLocations(State* state);

/// Use map information to produce candidate resource depot locations
/// sorted by their proximity to the main base
std::list<std::pair<Position, int>>
candidateExpansionResourceDepotLocationsDistances(State* state);

/// Produce seed locations for the building
/// @param state Bot's state
/// @param type Building type
/// @param upc UPCTuple with possible restrictions regarding position
/// @param builder Unit selected to construct the building,
///   nullptr if not yet selected
/// @return Proposed seed locations (in walktiles)
std::vector<Position> buildLocationSeeds(
    State* state,
    BuildType const* type,
    UPCTuple const& upc,
    Unit* builder = nullptr);

/// Produces string representation of masks (buildable, building,
/// reservedAsUnbuildable, resourceDepotUnbuildable, reservedForResourceDepot)
/// around the provided build location. Used for verbose logs mostly for
/// debugging purposes.
/// @param state Bot's state
/// @param type Building type
/// @param pos Coordinates of an upper-left corner of the proposed location
///    (in walktiles)
/// @return String containing masks, where '1' (or '+' outside the building
///    area) stands for true, '0' (or '-') for false, 'X' for cells over
///    the map edge
std::string
buildLocationMasks(State* state, const BuildType* type, const Position& pos);

/// Sets Tile::reservedAsUnbuildable to reserve the tiles occupied by a given
/// building type when placed at pos.
void fullReserve(TilesInfo& tt, BuildType const* type, Position const& pos);

/// Clears Tile::reservedAsUnbuildable to free the tiles occupied by a given
/// building type when placed at pos.
void fullUnreserve(TilesInfo& tt, BuildType const* type, Position const& pos);

} // namespace builderhelpers
} // namespace cherrypi
