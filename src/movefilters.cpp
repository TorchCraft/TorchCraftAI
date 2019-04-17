/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "movefilters.h"
#include <bwem/map.h>

namespace cherrypi {
namespace movefilters {

PPositionFilter
makePositionFilter(PPositionFilter base, PositionFilters l, bool blocking) {
  auto filter = std::make_shared<MultiPositionFilter>(base, l, blocking);
  return std::static_pointer_cast<PositionFilter>(filter);
}

PPositionFilter makePositionFilter(
    PositionFilters l,
    PositionFilterPolicy policy,
    bool blocking) {
  auto filter = std::make_shared<UnionPositionFilter>(l, policy, blocking);
  return std::static_pointer_cast<PositionFilter>(filter);
}

bool insideSpecificUnit(Position const& pos, Unit* bldg, int margin) {
  auto type = bldg->type;
  auto ulx = (bldg->unit.pixel_x - type->dimensionLeft) / 8 - margin;
  auto uly = (bldg->unit.pixel_y - type->dimensionUp) / 8 - margin;
  auto bly = (bldg->unit.pixel_y + type->dimensionDown) / 8 + margin;
  auto urx = (bldg->unit.pixel_x + type->dimensionRight) / 8 + margin;
  bool inside = pos.x >= ulx && pos.x <= urx && pos.y >= uly && pos.y <= bly;
  return inside;
}

// Whether two units touch each other
bool unitTouch(Unit* unit, Unit* v, int dirX, int dirY) {
  auto type = unit->type;
  auto ulx = (unit->unit.pixel_x - type->dimensionLeft) / 8 + dirX;
  auto uly = (unit->unit.pixel_y - type->dimensionUp) / 8 + dirY;
  auto bly = (unit->unit.pixel_y + type->dimensionDown) / 8 + dirY;
  auto urx = (unit->unit.pixel_x + type->dimensionRight) / 8 + dirX;
  // units touch if any of the corners of unit are within range 1 of v
  // points are {{ulx, uly}, {ulx, bly}, {urx, uly}, {urx, bly}}
  for (auto pos : {Position(ulx, uly),
                   Position(ulx, bly),
                   Position(urx, uly),
                   Position(urx, bly)}) {
    if (insideSpecificUnit(pos, v, 1)) {
      return true;
    }
  }
  return false;
}

bool insideSpecificUnit(Unit* unit, Position const& pos, Unit* bldg) {
  if ( // unitTouch(unit, bldg) && // not sure, maybe we can restrict
      unitTouch(unit, bldg, pos.x - unit->x, pos.y - unit->y)) {
    VLOG(4) << "insideSpecificUnit: " << utils::unitString(unit)
            << "with target position " << pos;
    return true;
  }
  return false;
}

bool insideAnyUnit(Unit* unit, Position const& pos, std::vector<Unit*> units) {
  return std::any_of(units.begin(), units.end(), [unit, pos](Unit* o) {
    return insideSpecificUnit(unit, pos, o);
  });
}

bool positionAvoids(Unit* unit, Position const& pos, Unit* nmy) {
  if (!nmy->canAttack(unit)) {
    return true;
  }
  double unitSpeed = unit->topSpeed;
  double nmySpeed = nmy->topSpeed;

  Vec2T<double> unitPos = Vec2T<double>(unit);
  Vec2T<double> nmyPos = Vec2T<double>(nmy);
  Vec2T<double> tgtPos = Vec2T<double>(pos);
  // give us a few frames of advantage for enemy to fix its direction
  auto velocityUnit = Vec2T<double>(unit->unit.velocityX, unit->unit.velocityY);
  auto velocityNmy = Vec2T<double>(nmy->unit.velocityX, nmy->unit.velocityY);
  // this gives us more room that is acceptable because the one who follows us
  // will need a few frames to adapt its direction
  double discount = 1.5 *
      (1 + Vec2T<double>::cos(velocityUnit, Vec2T<double>(tgtPos - unitPos)) -
       Vec2T<double>::cos(velocityNmy, Vec2T<double>(tgtPos - nmyPos)));

  unitPos += (tgtPos - unitPos).normalize() * unitSpeed * discount;

  // center on unitPos
  nmyPos -= unitPos;
  tgtPos -= unitPos;
  unitPos = Vec2T<double>(0, 0);
  // line from unitPos to tgtPos
  auto directionUnit = tgtPos - unitPos;
  directionUnit.normalize();
  if (std::abs(directionUnit.length() - 1) >= 1.0e-6) {
    LOG(ERROR) << "bad normalization";
  }
  double constexpr timeFrame = kTimeUpdateMove + 7; // to account for lag
  auto dir2unit = tgtPos * unitSpeed * timeFrame / tgtPos.length() - nmyPos;
  double dist2unit = dir2unit.length();
  auto s = dist2unit / timeFrame;
  auto minDist = sqrt(
      dist2unit * dist2unit + nmySpeed * nmySpeed * timeFrame * timeFrame -
      2 * dist2unit * nmySpeed * timeFrame);
  if (s <= nmySpeed) { // we will be intercepted
    return false;
  }
  double nmyRange =
      unit->type->isFlyer ? nmy->unit.airRange : nmy->unit.groundRange;
  nmyRange +=
      std::max(
          std::max(unit->type->dimensionUp, unit->type->dimensionDown),
          std::max(unit->type->dimensionLeft, unit->type->dimensionRight)) /
      8.;
  nmyRange +=
      std::max(
          std::max(nmy->type->dimensionUp, nmy->type->dimensionDown),
          std::max(nmy->type->dimensionLeft, nmy->type->dimensionRight)) /
      8.;
  nmyRange = std::ceil(nmyRange);
  // can't hope to be much better than current distance
  return std::floor(minDist) > nmyRange;
}

bool dangerousAttack(Unit* unit, Unit* tgt) {
  auto dir = Vec2(tgt->x - unit->x, tgt->y - unit->y);
  dir.normalize();
  auto nextPos = Position(Vec2(unit) + dir * unit->topSpeed * 12);
  for (auto nmy : unit->enemyUnitsInSightRange) {
    double nmyRange =
        unit->type->isFlyer ? nmy->unit.airRange : nmy->unit.groundRange;
    nmyRange +=
        std::max(
            std::max(unit->type->dimensionUp, unit->type->dimensionDown),
            std::max(unit->type->dimensionLeft, unit->type->dimensionRight)) /
        8.;
    nmyRange +=
        std::max(
            std::max(nmy->type->dimensionUp, nmy->type->dimensionDown),
            std::max(nmy->type->dimensionLeft, nmy->type->dimensionRight)) /
        8.;
    nmyRange = std::ceil(nmyRange);
    if (!nmy->type->isWorker && nmy->canAttack(unit)) {
      if (nextPos.distanceTo(nmy) <= nmyRange) {
        return true;
      }
    }
  }
  return false;
}

// predefined filters
PPositionFilter fleeAttackers() {
  return makePositionFilter<Unit*>(
      beingAttackedByEnemiesGetter,
      [](Unit* unit, Position const& pos, Unit* nmy) -> bool {
        return !insideSpecificUnit(
            pos, nmy); // goal: false if enemy is blocking
      },
      negDistanceScore,
      PositionFilterPolicy::ACCEPT_IF_ALL,
      true);
}

PPositionFilter fleeThreatening() {
  return makePositionFilter<Unit*>(
      threateningEnemiesGetter,
      [](Unit* unit, Position const& pos, Unit* nmy) -> bool {
        return !insideSpecificUnit(
            pos, nmy); // goal: false if enemy is blocking
      },
      negDistanceScore,
      PositionFilterPolicy::ACCEPT_IF_ALL,
      true);
}

PPositionFilter avoidAttackers() {
  return makePositionFilter<Unit*>(
      beingAttackedByEnemiesGetter, positionAvoids, negDistanceScore);
}

PPositionFilter avoidThreatening() {
  return makePositionFilter<Unit*>(
      threateningEnemiesGetter, positionAvoids, negDistanceScore);
}

PPositionFilter avoidEnemyUnitsInRange(float range) {
  return makePositionFilter<Unit*>(
      enemyUnitsInSightRangeGetter,
      [range](Unit* unit, Position const& pos, Unit* nmy) -> bool {
        return utils::distance(unit, nmy) <= range &&
            positionAvoids(unit, pos, nmy);
      },
      negDistanceScore);
}

PPositionFilter getCloserTo(std::vector<Position> coordinates) {
  if (coordinates.empty()) {
    LOG(ERROR) << "PositionFilter: making a filter from no coordinates";
  }
  return makePositionFilter<Position>(
      ConstantGetter(
          coordinates), //[coordinates] (Unit*) {return coordinates;},
      [](Unit* unit, Position const& pos, Position tgtPos) -> bool {
        auto distanceToUnit = tgtPos.distanceTo(unit);
        // there are peculiarities here if we very close already ***
        auto distanceToTarget = tgtPos.distanceTo(pos);
        return (
            distanceToUnit > kMinDistToTargetPos &&
            distanceToTarget < distanceToUnit);
      },
      [](Unit* unit, Position const& pos, Position tgt) {
        return pos.distanceTo(tgt);
      },
      PositionFilterPolicy::ACCEPT_IF_ANY);
}

PPositionFilter getCloserTo(Unit* bldg) {
  if (!bldg) {
    return getCloserTo(std::vector<Position>());
  }
  auto type = bldg->type;
  auto ulx = (bldg->unit.pixel_x - type->dimensionLeft) / 8 - 3;
  auto uly = (bldg->unit.pixel_y - type->dimensionUp) / 8 - 3;
  auto bly = (bldg->unit.pixel_y + type->dimensionDown) / 8 + 3;
  auto urx = (bldg->unit.pixel_x + type->dimensionRight) / 8 + 3;
  auto coordinates =
      std::vector<Position>({{ulx, uly}, {ulx, bly}, {urx, uly}, {urx, bly}});
  return getCloserTo(coordinates);
}

PPositionFilter getCloserTo(Position const& pos) {
  if (pos.x < 0 || pos.y < 0) {
    return getCloserTo(std::vector<Position>());
  }
  return makePositionFilter<Position>(
      // [pos] (Unit*) {return ;},
      ConstantGetter(std::vector<Position>({pos})),
      [](Unit* unit, Position const& pos, Position tgtPos) {
        return tgtPos.distanceTo(pos) < tgtPos.distanceTo(unit);
      },
      [](Unit* unit, Position const& pos, Position tgt) {
        return pos.distanceTo(tgt);
      },
      PositionFilterPolicy::ACCEPT_IF_ANY);

  return getCloserTo(std::vector<Position>({pos}));
}

bool walkable(State* state, Position const& pos) {
  auto tcstate = state->tcstate();
  return tcstate->walkable_data[pos.y * tcstate->map_size[0] + pos.x] != 0;
}

bool moveIsPossible(
    State* state,
    Position const& pos,
    std::vector<Unit*> const& obstacles,
    bool outOfBoundsInvalid) {
  auto checkValid = [pos](Unit* u) { return insideSpecificUnit(pos, u); };
  if (pos.x < 0 || pos.y < 0) {
    return false;
  }
  if (!walkable(state, pos) ||
      std::any_of(obstacles.begin(), obstacles.end(), checkValid)) {
    return false;
  }
  return true;
}

// avoid attacking and threatening units
template <typename T>
Position safeDirectionTo(State* state, Unit* unit, T tgt) {
  return movefilters::smartMove(
      state,
      unit,
      movefilters::PositionFilters(
          {movefilters::makePositionFilter(
               movefilters::getCloserTo(tgt),
               {movefilters::avoidEnemyUnitsInRange(unit->sightRange)}),
           movefilters::makePositionFilter(
               movefilters::getCloserTo(tgt),
               {movefilters::avoidAttackers(),
                movefilters::avoidThreatening()}),
           movefilters::makePositionFilter({movefilters::avoidAttackers(),
                                            movefilters::avoidThreatening()}),
           movefilters::makePositionFilter(
               {movefilters::fleeAttackers(), movefilters::fleeThreatening()}),
           movefilters::fleeAttackers()}));
}
template Position safeDirectionTo<Unit*>(State* state, Unit* unit, Unit* tgt);
template Position
safeDirectionTo<Position const&>(State* state, Unit* unit, Position const& tgt);
template Position safeDirectionTo<std::vector<Position>>(
    State* state,
    Unit* unit,
    std::vector<Position> tgt);

// got to nearby checkpoint if attacked and target is far way
Position safeMoveTo(State* state, Unit* unit, Position const& pos) {
  auto moveTo_ = [state, unit](Position const& tgtPos) -> Position {
    if (unit->beingAttackedByEnemies.empty() &&
        unit->threateningEnemies.empty()) {
      return tgtPos;
    }
    return safeDirectionTo<Position const&>(state, unit, tgtPos);
  };

  if (unit->type->isFlyer) {
    return moveTo_(pos);
  }

  auto tgt = pathMoveTo(state, unit, pos);
  return moveTo_(tgt);
}

Position pathMoveTo(State* state, Unit* unit, Position const& pos) {
  // BWEM segfaults if we pass an invalid position
  auto posc = utils::clampPositionToMap(state, pos);
  auto tarea = state->map()->GetArea(BWAPI::WalkPosition(posc.x, posc.y));
  auto uarea = state->map()->GetArea(BWAPI::WalkPosition(unit->x, unit->y));
  auto tgt = posc;
  if (tarea && uarea && tarea != uarea) {
    int pLength;
    auto path = state->map()->GetPath(
        BWAPI::Position(BWAPI::WalkPosition(unit->x, unit->y)),
        BWAPI::Position(BWAPI::WalkPosition(posc.x, posc.y)),
        &pLength);
    if (pLength < 0) {
      return Position(-1, -1);
    }
    if (!path.empty()) {
      auto ch1 = path[0]->Center();
      auto chkPos = Position(ch1.x, ch1.y);
      if (chkPos.distanceTo(unit) > 20) {
        tgt = chkPos;
      } else {
        if (path.size() >= 2) {
          auto ch2 = path[1]->Center();
          tgt = Position(ch2.x, ch2.y);
        }
      }
    }
  }
  return tgt;
}

Position smartMove(
    State* state,
    Unit* unit,
    PositionFilters const& filters,
    int moveLength,
    int nbPossibleMoves,
    int stepSize,
    bool outOfBoundsInvalid) {
  auto unitPos = Vec2(unit);

  if (stepSize <= 0) {
    stepSize = kMoveLOSStepSize;
  }
  if (nbPossibleMoves % stepSize != 0 || kMinMoveLength % stepSize != 0) {
    LOG(WARNING) << "[getValidMovePositions] Invalid (moveLength, stepSize) "
                    "argument pair"
                 << " got (" << moveLength << ", " << stepSize << ")"
                 << " expected moveLength to be a multiple of stepSize";
  }
  auto deg = (2 * M_PI) / nbPossibleMoves;
  auto obstacles = unit->obstaclesInSightRange;
  auto lastj = moveLength / stepSize;
  auto firstj = kMinMoveLength / stepSize;
  for (auto filter : filters) {
    auto positions = std::vector<std::pair<float, Position>>();
    for (int i = 0; i < nbPossibleMoves; i++) {
      auto dir = Vec2(cos(i * deg), sin(i * deg));
      for (int j = 1; j <= lastj; j++) {
        auto pos = utils::clampPositionToMap(
            state, Position(unitPos + dir * stepSize * j), outOfBoundsInvalid);

        if (!moveIsPossible(state, pos, obstacles, outOfBoundsInvalid)) {
          break;
        }
        if (insideAnyUnit(unit, pos, obstacles)) {
          continue; // direction may be valid, but path planning will interfere
        }
        if (j >= firstj) {
          if (filter->isValid(unit, pos)) {
            positions.push_back({filter->score(unit, pos), pos});
          } else if (filter->blocking()) {
            break;
          }
        }
      }
    }
    if (!positions.empty()) {
      return std::min_element(positions.begin(), positions.end())->second;
    }
  }
  return Position(-1, -1);
}

Position smartMove(
    State* state,
    Unit* unit,
    PPositionFilter const& filter,
    int moveLength,
    int nbPossibleMoves,
    int stepSize,
    bool outOfBoundsInvalid) {
  return smartMove(
      state,
      unit,
      PositionFilters({filter}),
      moveLength,
      nbPossibleMoves,
      stepSize,
      outOfBoundsInvalid);
}

Position smartMove_(State* state, Unit* unit, PositionFilters filters) {
  // TODO Check for ramps
  // We utilize smartmove when we are nearby to not path onto cliffs and
  // into enemy unit groups. When we are far away, smartMove will get us
  // stuck running into cliffs.
  return movefilters::smartMove(state, unit, filters);
}

Position smartMove(State* state, Unit* unit, Position tgt) {
  return tgt;
}
} // namespace movefilters
} // namespace cherrypi
