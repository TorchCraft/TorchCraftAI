/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "basetypes.h"

#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace BWEM {
class Map;
class Area;
class ChokePoint;
} // namespace BWEM

namespace cherrypi {

class AreaInfo;
class State;
struct Unit;
struct Tile;

/**
 * Represents an area on the map.
 *
 * Areas are regions determined by static map analysis using BWEM
 * (Brood War Easy Map). Areas correspond to the respective BWEM::Area. This
 * struct is used to aggregate all game state information (e.g. units,
 * visibility) local to the respective area.
 */
struct Area {
  /// ID of BWEM area. This corresponds to the index in the areas vector (+1)
  int id = -1;
  /// Center in the area's bounding box in walk tiles.
  int x = 0;
  /// Center of the area's bounding box in walk tiles.
  int y = 0;
  /// Top left of the area's bounding box in walk tiles.
  Position topLeft;
  /// Bottom right of the area's bounding box in walk tiles.
  Position bottomRight;
  /// Area size in walk tiles; includes walkable tiles only.
  int size = 0;
  /// Possible base locations.
  std::vector<Position> baseLocations;
  /// All units in this area that are not dead. This includes gone units.
  std::vector<Unit*> liveUnits;
  /// All units in this area that are currently visible.
  std::vector<Unit*> visibleUnits;
  /// All Minerals in this area
  std::vector<Unit*> minerals;
  /// All Geysers/Extractors/Refineries/Assimilators in this area
  std::vector<Unit*> geysers;
  /// All areas that are accessible from/to each other by ground share the same
  /// groupId.
  int groupId = -1;

  BWEM::Area const* area;

  /// Pointer to container object
  AreaInfo* areaInfo = nullptr;
  /// Accessible neighbors
  std::vector<Area*> neighbors;

  FrameNum lastExplored = 0;

  /// True if this area contains our base (our resource depot has been
  /// constructed in one of the base locations at some point). This
  /// flag is cleared if the area no longer contains any our buildings
  bool isMyBase = false;
  /// True if this area contains enemy base (we've seen enemy's resource depot
  /// constructed at one of the base locations at some point). This
  /// flag is cleared if the area no longer contains any enemy buildings, or if
  /// we guessed that this location should contain enemy base, but haven't seen
  /// the resource depot yet.
  bool isEnemyBase = false;
  /// True if this area may contain enemy's start location. Used only in the
  /// beginning of the game to determine the enemy's start location.
  /// Once the enemy start location is found, this flag is true only for the
  /// corresponding area.
  bool isPossibleEnemyStartLocation = false;
  /// True if we ever had a base at one of the area's baseLocations.
  bool wasMyBase = false;
  /// True if enemy ever had a base at one of the area's baseLocations.
  bool wasEnemyBase = false;
  bool hasMyBuildings = false;
  bool hasEnemyBuildings = false;
  /// Strength of all ground units in this area.
  double myGndStrength = 0;
  /// Strength of all air units in this area.
  double myAirStrength = 0;
  /// Strength of all detector units in this area.
  double myDetStrength = 0;
  double enemyGndStrength = 0;
  double enemyAirStrength = 0;
  double enemyDetStrength = 0;
};

/**
 * Access point for area and base information.
 *
 * Beside providing access to area information, this class also provides a few
 * convenience wrappers for functionality in BWEM::Map.
 */
class AreaInfo {
 public:
  struct BaseInfo {
    /// Area to which the base belongs
    Area* area;
    /// Base index within the area, which corresponds to the build location
    int baseId;
    /// Resource depot constructed on the base
    Unit* resourceDepot;
    /// How saturated are this base's resources?
    /// Currently calculated by GathererController for speed + expediency.
    /// (No, this isn't a good design)
    float saturation = 0.0f;
  };

  AreaInfo(State* state);
  AreaInfo(AreaInfo const&) = delete;
  AreaInfo& operator=(AreaInfo const&) = delete;

  void update();

  std::vector<Area> const& areas() const;
  Area& getArea(int id);
  Area const& getArea(int id) const;
  Area& getArea(Position p);
  Area const& getArea(Position p) const;
  Area& getArea(Tile const& tile);
  Area const& getArea(Tile const& tile) const;
  Area* tryGetArea(int id);
  Area const* tryGetArea(int id) const;
  Area* tryGetArea(Position p);
  Area const* tryGetArea(Position p) const;

  int numMyBases() const;
  /// Returns data for our n-th base
  const BaseInfo* myBase(int n) const;
  /// Returns data for all our bases
  std::vector<BaseInfo> const& myBases() const;
  /// Returns true, if our start location is known
  bool foundMyStartLocation() const;
  /// Returns our start location if it is known, otherwise returns
  /// kInvalidPosition
  Position myStartLocation() const;
  /// Returns the closest base according to Euclidean distance
  int myClosestBaseIdx(Position const& p) const;
  std::vector<Unit*> myBaseResources(int n) const;

  int numEnemyBases() const;
  /// Returns data for enemy's n-th base
  const BaseInfo* enemyBase(int n) const;
  /// Returns true, if enemy start location is known
  bool foundEnemyStartLocation() const;
  /// Returns enemy start location if it is known, otherwise returns
  /// kInvalidPosition
  Position enemyStartLocation() const;

  std::vector<Position> const& candidateEnemyStartLocations() const;

  /// Returns a path of choke points to walk from a to b.
  /// If a or b are not accessible or a is not accessible from b, returns an
  /// empty path and sets length to infinity.
  std::vector<Position>
  walkPath(Position a, Position b, float* length = nullptr) const;

  /// Returns a path of areas to walk from a to b.
  /// If a or b are not accessible or a is not accessible from b, returns an
  /// empty path and sets length to infinity.
  std::vector<Area const*>
  walkPathAreas(Position a, Position b, float* length = nullptr) const;

  /// Returns the distance in walktiles for a walking path a to b.
  float walkPathLength(Position a, Position b) const;

 private:
  void initialize();
  void findMyStartLocation();
  void initializePossibleEnemyLocations();
  /// Returns area base location index, if the unit coresponds to some
  /// base location (i.e. the distance between the 2 is within certain
  /// threshold). Returns -1, if no corresponding base location was found
  int findBaseLocationIndexInArea(Unit* unit, Area& area);
  void updateUnits();
  void updateEnemyStartLocations();
  void updateStrengths();
  void updateNeighbors();
  void updateBases();
  void populateCache();
  Area* getCachedArea(Position p);

  bool isMyBaseAlive(const BaseInfo& baseInfo) const;
  bool isEnemyBaseAlive(const BaseInfo& baseInfo) const;
  void walkPathHelper(
      BWEM::Map* map,
      Position a,
      Position b,
      std::vector<Area const*>* areas,
      std::vector<Position>* chokePoints,
      float* length) const;

  State* state_ = nullptr;
  BWEM::Map* map_ = nullptr;
  std::vector<Area> areas_;
  std::vector<Position> candidateEnemyStartLoc_;
  Position myStartLoc_ = kInvalidPosition;
  std::vector<BaseInfo> myBases_;
  std::vector<BaseInfo> enemyBases_;
  std::unordered_map<Unit*, BaseInfo*> myDepot2Base_;
  std::unordered_map<Unit*, BaseInfo*> enemyDepot2Base_;
  std::unordered_set<Unit*> macroDepots_;
  std::unordered_map<int, int> neighborAreaCache_;
};

} // namespace cherrypi
