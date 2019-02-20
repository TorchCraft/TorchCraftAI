/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "areainfo.h"

#include "state.h"
#include "utils.h"

#include <bwem/map.h>

#include <tuple>

namespace cherrypi {

namespace {
constexpr int kBaseLocationToDepotDistanceThreshold =
    tc::BW::XYWalktilesPerBuildtile * 2;
constexpr int kMyBaseAliveBuildingCountThreshold = 5;
constexpr int kEnemyBaseAliveBuildingCountThreshold = 5;
} // namespace

// Helper function to compute walk path or length as required
void AreaInfo::walkPathHelper(
    BWEM::Map* map,
    Position a,
    Position b,
    std::vector<Area const*>* areasOut,
    std::vector<Position>* chokePoints,
    float* length) const {
  // If one of the positions is not walkable (i.e. in a lake), BWEM's path
  // computation can be very costly. Hence, check this upfront and return an
  // empty path in this case
  auto wpA = BWAPI::WalkPosition(a.x, a.y);
  auto wpB = BWAPI::WalkPosition(b.x, b.y);
  auto* arA = map->GetArea(wpA);
  auto* arB = map->GetArea(wpB);
  const bool noPath = arA == nullptr || arB == nullptr;
  const bool trivialPath = arA == arB;
  if (noPath || trivialPath) {
    // Trivial cases: There's no walk path, or they're in the same area anyhow
    if (length != nullptr) {
      *length = trivialPath ? utils::distance(a, b) : kfInfty;
    }
    if (areasOut != nullptr) {
      areasOut->clear();
    }
    if (chokePoints != nullptr) {
      chokePoints->clear();
    }
    return;
  }

  BWEM::CPPath cpath;
  if (length != nullptr) {
    int pxLength;
    cpath = map->GetPath(BWAPI::Position(wpA), BWAPI::Position(wpB), &pxLength);
    if (pxLength < 0) {
      // No walk path between a and b
      *length = kfInfty;
    } else {
      *length = float(pxLength) / tc::BW::XYPixelsPerWalktile;
    }
  } else if (chokePoints != nullptr) {
    cpath = map->GetPath(BWAPI::Position(wpA), BWAPI::Position(wpB));
  }
  if (areasOut != nullptr) {
    areasOut->clear();
    // Populate areas along the path
    Area const* next = &getArea(arA->Id());
    for (auto* cp : cpath) {
      Area const& areaA = getArea(cp->GetAreas().first->Id());
      Area const& areaB = getArea(cp->GetAreas().second->Id());
      next = next == &areaA ? &areaB : &areaA;
      areasOut->push_back(next);
    }
  }
  if (chokePoints != nullptr) {
    chokePoints->clear();
    chokePoints->reserve(cpath.size());
    for (auto* cp : cpath) {
      BWAPI::WalkPosition pos(cp->Center());
      chokePoints->emplace_back(pos.x, pos.y);
    }
  }
}

AreaInfo::AreaInfo(State* state) : state_(state) {
  map_ = state_->map();
}

void AreaInfo::update() {
  if (state_->mapWidth() <= 0 || state_->mapHeight() <= 0) {
    return;
  }

  if (map_ == nullptr) {
    map_ = state_->map();
  } else if (map_ != state_->map()) {
    throw std::runtime_error("Map data has changed in-game");
  }

  if (areas_.empty()) {
    initialize();
    populateCache();
  }

  updateUnits();
  updateEnemyStartLocations();
  updateStrengths();
  updateNeighbors();
  updateBases();
}

std::vector<Area> const& AreaInfo::areas() const {
  return areas_;
}

Area& AreaInfo::getArea(int id) {
  if (id < 1 || (size_t)id > areas_.size()) {
    throw std::runtime_error("Attempt to get invalid area");
  }
  return areas_[id - 1];
}

Area const& AreaInfo::getArea(int id) const {
  return const_cast<AreaInfo*>(this)->getArea(id);
}

void AreaInfo::populateCache() {
  neighborAreaCache_.clear();
  // We need to compute the nearest area to the non-walkable tiles. We do that
  // by running a BFS from the walkable tiles

  const int width = map_->WalkSize().x;
  const int height = map_->WalkSize().y;
  std::vector<bool> seen(width * height, false);

  auto getId = [width](const Vec2T<int>& p) { return width * p.y + p.x; };

  // in the BFS queue, we store pairs of {pos, areaId}
  std::queue<std::pair<Vec2T<int>, int>> bfs_queue;
  for (int x = 0; x < width; ++x) {
    for (int y = 0; y < height; ++y) {
      // try to get an area. If we get it, then it's a walkable tile that we use
      // as seed
      auto curArea = map_->GetArea(BWAPI::WalkPosition(x, y));
      if (curArea != nullptr) {
        bfs_queue.push({{x, y}, curArea->Id()});
        seen[getId({x, y})] = true;
      }
    }
  }

  auto dirs = std::vector<Vec2T<int>>{
      {-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};

  while (!bfs_queue.empty()) {
    auto current = bfs_queue.front();
    Vec2T<int> pos = current.first;
    int area = current.second;
    bfs_queue.pop();
    for (const auto& d : dirs) {
      auto next = pos + d;
      if (next.x >= 0 && next.y >= 0 && next.x < width && next.y < height &&
          !seen[getId(next)]) {
        seen[getId(next)] = true;
        neighborAreaCache_[getId(next)] = area;
        bfs_queue.push({next, area});
      }
    }
  }
}

Area* AreaInfo::getCachedArea(Position p) {
  // Check if the current position is an area first. If it is
  // then we don't need to call GetNearestArea below.
  auto curArea = map_->GetArea(BWAPI::WalkPosition(p.x, p.y));
  if (curArea != nullptr) {
    return &getArea(curArea->Id());
  }

  // Linerized index, logic stolen from BWEM
  const auto pos = map_->WalkSize().x * p.y + p.x;
  bool hitCache = (neighborAreaCache_.find(pos) != neighborAreaCache_.end());
  if (!hitCache) {
    return nullptr;
  }
  return &getArea(neighborAreaCache_[pos]);
}

Area& AreaInfo::getArea(Position p) {
  p = utils::clampPositionToMap(state_, p);
  auto area = getCachedArea(p);

  if (area == nullptr) {
    throw std::runtime_error("No area at or near this position");
  } else {
    return *area;
  }
}

Area const& AreaInfo::getArea(Position p) const {
  return const_cast<AreaInfo*>(this)->getArea(p);
}

Area& AreaInfo::getArea(Tile const& tile) {
  return getArea(Position(tile.x, tile.y));
}

Area const& AreaInfo::getArea(Tile const& tile) const {
  return const_cast<AreaInfo*>(this)->getArea(Position(tile.x, tile.y));
}

Area* AreaInfo::tryGetArea(int id) {
  if (id < 1 || (size_t)id > areas_.size()) {
    return nullptr;
  }
  return &areas_[id - 1];
}

Area const* AreaInfo::tryGetArea(int id) const {
  return const_cast<AreaInfo*>(this)->tryGetArea(id);
}

Area* AreaInfo::tryGetArea(Position p) {
  if (p.x < 0 || p.y < 0 || p.x >= state_->mapWidth() ||
      p.y >= state_->mapHeight()) {
    return nullptr;
  }
  return getCachedArea(p);
}

Area const* AreaInfo::tryGetArea(Position p) const {
  return const_cast<AreaInfo*>(this)->tryGetArea(p);
}

int AreaInfo::numMyBases() const {
  return myBases_.size();
}

const AreaInfo::BaseInfo* AreaInfo::myBase(int n) const {
  if (n < 0 || (size_t)n >= myBases_.size()) {
    return nullptr;
  }
  return &myBases_[n];
}

std::vector<AreaInfo::BaseInfo> const& AreaInfo::myBases() const {
  return myBases_;
}

bool AreaInfo::foundMyStartLocation() const {
  return myStartLoc_ != kInvalidPosition;
}

Position AreaInfo::myStartLocation() const {
  return myStartLoc_;
}

int AreaInfo::myClosestBaseIdx(Position const& p) const {
  int closest = -1;
  float dMin = kfInfty;
  for (size_t i = 0; i < myBases_.size(); i++) {
    auto d = utils::distance(p, myBases_[i].resourceDepot);
    if (d < dMin) {
      closest = int(i);
      dMin = d;
    }
  }
  return closest;
}

std::vector<Unit*> AreaInfo::myBaseResources(int n) const {
  std::vector<Unit*> resources;
  const BaseInfo* base = myBase(n);
  if (!base) {
    LOG(WARNING) << "Invalid base index " << n;
    return resources;
  }
  const Area* area = base->area;
  if (!area) {
    LOG(WARNING) << "Base area not defined, base " << n;
    return resources;
  }
  // We could cache per-base resources but would need to update them
  // periodically as mineral patches disappear once they've been fully mined.
  auto* bwemArea = area->area;
  if (!bwemArea) {
    LOG(WARNING) << "BWEM area not defined for area for base " << n;
    return resources;
  }
  if (base->baseId < 0 || (size_t)base->baseId >= bwemArea->Bases().size()) {
    LOG(WARNING) << "Invalid base ID: " << base->baseId;
    return resources;
  }
  auto& bwemBase = bwemArea->Bases()[base->baseId];

  auto& unitsInfo = state_->unitsInfo();
  for (auto* mineral : bwemBase.Minerals()) {
    Unit* unit = unitsInfo.getUnit(mineral->Unit()->getID());
    if (unit == nullptr) {
      LOG(WARNING) << "Null unit from BWEM u" << mineral->Unit()->getID();
    } else if (!unit->type->isMinerals) {
      LOG(WARNING) << "BWEM mineral is not actually a mineral: "
                   << utils::unitString(unit);
    } else {
      resources.push_back(unit);
    }
  }

  for (auto* geyser : bwemBase.Geysers()) {
    Unit* unit = unitsInfo.getUnit(geyser->Unit()->getID());
    if (unit == nullptr) {
      LOG(WARNING) << "Null unit from BWEM u" << geyser->Unit()->getID();
    } else if (!unit->type->isGas) {
      // This is ok for drones -- presumably, we've pulled off the gas trick
      LOG_IF(WARNING, unit->type != buildtypes::Zerg_Drone)
          << "BWEM geyser is not actually gas: " << utils::unitString(unit);
    } else {
      resources.push_back(unit);
    }
  }

  return resources;
}

int AreaInfo::numEnemyBases() const {
  return enemyBases_.size();
}

const AreaInfo::BaseInfo* AreaInfo::enemyBase(int n) const {
  if (n < 0 || (size_t)n >= enemyBases_.size()) {
    return nullptr;
  }
  return &enemyBases_[n];
}

bool AreaInfo::foundEnemyStartLocation() const {
  return candidateEnemyStartLoc_.size() == 1;
}

Position AreaInfo::enemyStartLocation() const {
  if (!foundEnemyStartLocation()) {
    return kInvalidPosition;
  }
  return candidateEnemyStartLoc_[0];
}

std::vector<Position> const& AreaInfo::candidateEnemyStartLocations() const {
  return candidateEnemyStartLoc_;
}

std::vector<Position> AreaInfo::walkPath(Position a, Position b, float* length)
    const {
  std::vector<Position> chokePoints;
  walkPathHelper(
      map_, std::move(a), std::move(b), nullptr, &chokePoints, length);
  return chokePoints;
}

std::vector<Area const*>
AreaInfo::walkPathAreas(Position a, Position b, float* length) const {
  std::vector<Area const*> areas;
  walkPathHelper(map_, std::move(a), std::move(b), &areas, nullptr, length);
  return areas;
}

float AreaInfo::walkPathLength(Position a, Position b) const {
  float length;
  walkPathHelper(map_, std::move(a), std::move(b), nullptr, nullptr, &length);
  return length;
}

void AreaInfo::initialize() {
  areas_.clear();

  auto& mapAreas = map_->Areas();
  areas_.resize(mapAreas.size());
  for (size_t i = 0; i < mapAreas.size(); i++) {
    areas_[i].areaInfo = this;
    areas_[i].id = mapAreas[i].Id();
    areas_[i].area = &(mapAreas[i]);
    // We rely on area IDs being equal to the index of the area in the vector
    // (plus one). This is an implementation detail of BWEM, so better check
    // for it.
    if (areas_[i].id != (int)(i + 1)) {
      throw std::runtime_error("Unexpected Area ID");
    }

    // Compute center of bounding box
    auto topLeft = mapAreas[i].TopLeft();
    auto bottomRight = mapAreas[i].BottomRight();
    areas_[i].x = (topLeft.x + (bottomRight.x - topLeft.x) / 2) *
        unsigned(tc::BW::XYWalktilesPerBuildtile);
    areas_[i].y = (topLeft.y + (bottomRight.y - topLeft.y) / 2) *
        unsigned(tc::BW::XYWalktilesPerBuildtile);
    areas_[i].topLeft.x = topLeft.x * unsigned(tc::BW::XYWalktilesPerBuildtile);
    areas_[i].topLeft.y = topLeft.y * unsigned(tc::BW::XYWalktilesPerBuildtile);
    areas_[i].bottomRight.x =
        bottomRight.x * unsigned(tc::BW::XYWalktilesPerBuildtile);
    areas_[i].bottomRight.y =
        bottomRight.y * unsigned(tc::BW::XYWalktilesPerBuildtile);
    areas_[i].size = mapAreas[i].MiniTiles();
    areas_[i].groupId = mapAreas[i].GroupId();

    for (auto& base : mapAreas[i].Bases()) {
      BWAPI::WalkPosition pos(base.Center());
      areas_[i].baseLocations.emplace_back(pos.x, pos.y);
    }
  }

  findMyStartLocation();
  initializePossibleEnemyLocations();
}

void AreaInfo::findMyStartLocation() {
  if (foundMyStartLocation()) {
    return;
  }
  // Find our initial resource depot and determine the closest start location.
  for (auto unit : state_->unitsInfo().myUnits()) {
    if (unit->type->isResourceDepot && unit->visible) {
      float dMin = kfMax;
      Position pos = kInvalidPosition;
      for (auto& loc : state_->tcstate()->start_locations) {
        auto d = utils::distance(loc.x, loc.y, unit->x, unit->y);
        if (d < dMin) {
          dMin = d;
          pos = Position(loc.x, loc.y);
        }
      }
      if (pos != kInvalidPosition) {
        myStartLoc_ = pos;
        auto& myArea = getArea(myStartLoc_);
        myArea.isMyBase = true;
        myArea.wasMyBase = true;
        break;
      }
    }
  }
}

void AreaInfo::initializePossibleEnemyLocations() {
  for (auto& loc : state_->tcstate()->start_locations) {
    if (loc.x != myStartLoc_.x || loc.y != myStartLoc_.y) {
      candidateEnemyStartLoc_.push_back(Position(loc.x, loc.y));
    }
  }
  switch (candidateEnemyStartLoc_.size()) {
    case 0:
      LOG(WARNING) << "no possible enemy starting locations";
      break;
    case 1: {
      VLOG(1)
          << "scouting info: enemy base known from the start by elimination";
      auto& nmyArea = getArea(candidateEnemyStartLoc_[0]);
      nmyArea.isEnemyBase = true;
      nmyArea.wasEnemyBase = true;
      break;
    }
    default:
      for (auto& loc : candidateEnemyStartLoc_) {
        auto& nmyArea = getArea(loc);
        nmyArea.isPossibleEnemyStartLocation = true;
      }
      break;
  }
}

int AreaInfo::findBaseLocationIndexInArea(Unit* unit, Area& area) {
  // Is unit placed at possible base location?
  int correspondingBaseLoc = -1;
  auto upos = Position(unit);
  for (size_t i = 0; i < area.baseLocations.size(); i++) {
    if (utils::distance(upos, area.baseLocations[i]) <=
        kBaseLocationToDepotDistanceThreshold) {
      // Yup
      correspondingBaseLoc = (int)i;
      break;
    }
  }
  return correspondingBaseLoc;
}

void AreaInfo::updateUnits() {
  for (auto& area : areas_) {
    area.liveUnits.clear();
    area.visibleUnits.clear();
    area.minerals.clear();
    area.geysers.clear();
    area.isMyBase = false;
    area.isEnemyBase = false;
    area.hasMyBuildings = false;
    area.hasEnemyBuildings = false;
  }

  auto frame = state_->currentFrame();

  // Collect info about live units
  // TODO: Some of the code below only needs to run for getShowUnits() really
  for (auto& unit : state_->unitsInfo().liveUnits()) {
    Area& area = getArea(unit->pos());
    if (unit->type->isMinerals) {
      area.minerals.push_back(unit);
    } else if (unit->type->isGas) {
      area.geysers.push_back(unit);
    }

    if (unit->type->isSpecialBuilding) {
      continue;
    }

    area.liveUnits.push_back(unit);
    if (unit->visible) {
      area.visibleUnits.push_back(unit);
    }
    if (unit->isMine) {
      area.lastExplored = frame;
    }

    if (unit->type->isResourceDepot && unit->completed()) {
      if (unit->isMine) {
        if ((macroDepots_.find(unit) == macroDepots_.end()) &&
            (myDepot2Base_.find(unit) == myDepot2Base_.end())) {
          auto baseLocIdx = findBaseLocationIndexInArea(unit, area);
          if (baseLocIdx >= 0) {
            VLOG(1) << "Registered new base #" << myBases_.size() << ": "
                    << utils::unitString(unit) << " at " << unit->x << ","
                    << unit->y;
            BaseInfo baseInfo;
            baseInfo.area = &area;
            baseInfo.baseId = baseLocIdx;
            baseInfo.resourceDepot = unit;
            myBases_.push_back(baseInfo);
            myDepot2Base_[unit] = &myBases_.back();
            area.isMyBase = true;
            area.wasMyBase = true;
          } else {
            macroDepots_.insert(unit);
            area.hasMyBuildings = true;
          }
        }
      } else if (unit->isEnemy) {
        if (enemyDepot2Base_.find(unit) == enemyDepot2Base_.end()) {
          int baseLocIdx = findBaseLocationIndexInArea(unit, area);
          if (baseLocIdx >= 0) {
            VLOG(1) << "Registered new enemy base #" << enemyBases_.size()
                    << ": " << utils::unitString(unit) << " at " << unit->x
                    << "," << unit->y;
            BaseInfo baseInfo;
            baseInfo.area = &area;
            baseInfo.baseId = baseLocIdx;
            baseInfo.resourceDepot = unit;
            enemyBases_.push_back(baseInfo);
            enemyDepot2Base_[unit] = &enemyBases_.back();
            area.isEnemyBase = true;
            area.wasEnemyBase = true;
          } else {
            area.hasEnemyBuildings = true;
          }
        }
      }
    }

    if (unit->type->isBuilding) {
      if (unit->isMine) {
        area.hasMyBuildings = true;
      } else if (unit->isEnemy) {
        area.hasEnemyBuildings = true;
      }
    }
  }
}

void AreaInfo::updateStrengths() {
  auto unitValue = [](Unit* u) {
    // Heuristic from Gab's thesis:
    // http://emotion.inrialpes.fr/people/synnaeve/phdthesis/phdthesis.html#x1-131002r2
    return u->type->mineralCost + 4.0 / 3.0 * u->type->gasCost +
        50 * u->type->supplyRequired;
  };

  for (auto& area : areas_) {
    area.myGndStrength = 0;
    area.myAirStrength = 0;
    area.myDetStrength = 0;
    area.enemyGndStrength = 0;
    area.enemyAirStrength = 0;
    area.enemyDetStrength = 0;

    for (Unit* unit : area.liveUnits) {
      auto type = unit->type;
      if (unit->isMine) {
        if (type->hasGroundWeapon) {
          area.myGndStrength += unitValue(unit);
        }
        if (type->hasAirWeapon) {
          area.myAirStrength += unitValue(unit);
        }
        // TODO Include detector buildings based on area size?
        if (type->isDetector && !type->isBuilding) {
          area.myDetStrength += unitValue(unit);
        }
      } else {
        if (type->hasGroundWeapon) {
          area.enemyGndStrength += unitValue(unit);
        }
        if (type->hasAirWeapon) {
          area.enemyAirStrength += unitValue(unit);
        }
        // TODO Include detector buildings based on area size?
        if (type->isDetector && !type->isBuilding) {
          area.enemyDetStrength += unitValue(unit);
        }
      }
    }
  }
}

void AreaInfo::updateNeighbors() {
  auto& mapAreas = map_->Areas();
  assert(areas_.size() == mapAreas.size());
  for (size_t i = 0; i < mapAreas.size(); i++) {
    auto const& neighbors = mapAreas[i].AccessibleNeighbours();
    // XXX This check may not be super robust
    if (neighbors.size() != areas_[i].neighbors.size()) {
      areas_[i].neighbors.resize(neighbors.size());
      for (size_t j = 0; j < neighbors.size(); j++) {
        areas_[i].neighbors[j] = tryGetArea(neighbors[j]->Id());
        assert(areas_[i].neighbors[j] != nullptr);
      }
    }
  }
}

void AreaInfo::updateBases() {
  for (auto baseInfoIt = myBases_.begin(); baseInfoIt != myBases_.end();) {
    if ((!baseInfoIt->resourceDepot || baseInfoIt->resourceDepot->dead) &&
        !isMyBaseAlive(*baseInfoIt)) {
      baseInfoIt->area->isMyBase = false;
      if (baseInfoIt->resourceDepot) {
        myDepot2Base_.erase(baseInfoIt->resourceDepot);
      }
      baseInfoIt = myBases_.erase(baseInfoIt);
    } else {
      baseInfoIt++;
    }
  }
  for (auto baseInfoIt = enemyBases_.begin();
       baseInfoIt != enemyBases_.end();) {
    if ((!baseInfoIt->resourceDepot || baseInfoIt->resourceDepot->dead) &&
        !isEnemyBaseAlive(*baseInfoIt)) {
      baseInfoIt->area->isEnemyBase = false;
      if (baseInfoIt->resourceDepot) {
        enemyDepot2Base_.erase(baseInfoIt->resourceDepot);
      }
      baseInfoIt = enemyBases_.erase(baseInfoIt);
    } else {
      baseInfoIt++;
    }
  }
}

bool AreaInfo::isMyBaseAlive(const BaseInfo& baseInfo) const {
  if (!baseInfo.area->hasMyBuildings) {
    return false;
  }
  int buildingCount = 0;
  for (Unit* unit : baseInfo.area->visibleUnits) {
    if (unit->isMine && unit->type->isBuilding) {
      buildingCount++;
    }
  }
  // TODO: implement more realistic criteria
  return buildingCount > kMyBaseAliveBuildingCountThreshold;
}

bool AreaInfo::isEnemyBaseAlive(const BaseInfo& baseInfo) const {
  if (!baseInfo.area->hasEnemyBuildings) {
    return false;
  }
  int buildingCount = 0;
  for (Unit* unit : baseInfo.area->liveUnits) {
    if (unit->isEnemy && unit->type->isBuilding) {
      buildingCount++;
    }
  }
  // TODO: implement more realistic criteria
  return buildingCount > kEnemyBaseAliveBuildingCountThreshold;
}

// checks if a starting location is confirmed not to be that of the opopnent
void AreaInfo::updateEnemyStartLocations() {
  // we found the enemy base by elimination
  if (foundEnemyStartLocation()) {
    // nothing to do at this stage
    return;
  }
  Position nmyPos;
  int nmyAreaId = -1;
  for (auto it = candidateEnemyStartLoc_.begin();
       it != candidateEnemyStartLoc_.end();) {
    auto pos = *it;
    auto& checkArea = getArea(pos);
    // we found the base
    if (checkArea.hasEnemyBuildings) {
      nmyPos = pos;
      nmyAreaId = checkArea.id;
      break;
    }
    if (state_->tilesInfo().getTile(pos.x, pos.y).visible) {
      checkArea.isPossibleEnemyStartLocation = false;
      it = candidateEnemyStartLoc_.erase(it);
    } else {
      it++;
    }
  }
  // base found by elimination
  if (candidateEnemyStartLoc_.size() == 1) {
    nmyPos = candidateEnemyStartLoc_[0];
    auto& checkArea = getArea(nmyPos);
    nmyAreaId = checkArea.id;
  }

  if (nmyAreaId == -1) {
    for (Unit* u : state_->unitsInfo().enemyUnits()) {
      if (!u->gone && !u->flying() && u->type->isBuilding) {
        Position pos = utils::getBestScoreCopy(
            candidateEnemyStartLoc_,
            [&](Position pos) {
              float d = utils::distance(pos, u);
              if (d > 4 * 30) {
                return kfInfty;
              }
              return d;
            },
            kfInfty);
        if (pos != Position()) {
          nmyPos = pos;
          auto& checkArea = getArea(nmyPos);
          nmyAreaId = checkArea.id;
        }
      }
    }
  }

  if (nmyAreaId >= 0) { // cleanup if we found
    for (auto& area : areas_) {
      if (area.id != nmyAreaId) {
        area.isPossibleEnemyStartLocation = false;
      }
    }
    candidateEnemyStartLoc_.clear();
    candidateEnemyStartLoc_.push_back(nmyPos);
    VLOG(1) << "Enemy location found at " << nmyPos.x << ", " << nmyPos.y;
    if (VLOG_IS_ON(3)) {
      for (auto area : areas()) {
        if (area.id != nmyAreaId && area.isEnemyBase) {
          LOG(WARNING) << "more than one enemy area";
        }
      }
    }
  }

  // debug
  if (VLOG_IS_ON(3) && foundEnemyStartLocation()) {
    auto nmyPos = enemyStartLocation();
    auto& nmyArea = getArea(nmyPos);
    for (auto area : areas()) {
      if ((area.id != nmyArea.id) && area.isPossibleEnemyStartLocation) {
        LOG(ERROR) << "Area improperly marked as possible enemy start"
                      " location";
      }
    }
  }
}

} // namespace cherrypi
