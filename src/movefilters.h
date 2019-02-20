/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "cherrypi.h"
#include "utils.h"

namespace cherrypi {
namespace movefilters {

enum class PositionFilterPolicy { ACCEPT_IF_ALL = 1, ACCEPT_IF_ANY };

/**
 * Assigns positions a score and validity, based on some criteria.
 * Used for threat-aware pathfinding.
 */
class PositionFilter {
 public:
  virtual bool isValid(Unit*, Position const&) = 0;
  virtual float score(Unit*, Position const&) = 0;
  virtual bool blocking() = 0;
};

typedef std::shared_ptr<PositionFilter> PPositionFilter;
typedef std::vector<PPositionFilter> PositionFilters;

/**
 * Input:
 *  - getter: for the list of objects to compare to
 *  - valid: f(agent, position, obj) => bool for whether this position is valid
 *    given the obj
 *  - score: f(agent, position, obj) => float to score this position
 *
 * The filter will decide if a position is valid by combining the valid func
 * and score function according to the PositionFilterPolicy.
 */
template <typename T, typename Container>
class FuncPositionFilter : public PositionFilter {
 public:
  FuncPositionFilter(
      std::function<Container const && (Unit*)> getter,
      std::function<bool(Unit*, Position const&, T)> valid,
      std::function<float(Unit*, Position const&, T)> scoreFunc,
      PositionFilterPolicy policy = PositionFilterPolicy::ACCEPT_IF_ALL,
      bool blocking = false)
      : getter_(std::move(getter)),
        valid_(std::move(valid)),
        score_(std::move(scoreFunc)),
        policy_(policy),
        blocking_(blocking) {}

  bool isValid(Unit* agent, Position const& pos) override {
    VLOG(4) << "PositionFilter: in filter valid";
    auto objects = getter_(agent);
    VLOG(4) << "PositionFilter: in valid non empty set? size is: "
            << objects.size();
    if (policy_ == PositionFilterPolicy::ACCEPT_IF_ALL) {
      for (auto obj : objects) {
        if (!valid_(agent, pos, obj)) {
          return false;
        }
      }
      return true;
    } else if (policy_ == PositionFilterPolicy::ACCEPT_IF_ANY) {
      for (auto obj : objects) {
        if (valid_(agent, pos, obj)) {
          return true;
        }
      }
      return false; // false if empty: must be valid for some
    } else {
      LOG(DFATAL) << "MoveHelpers: incorrect position filter policy";
    }
    return false;
  }

  float score(Unit* agent, Position const& pos) override {
    if (policy_ == PositionFilterPolicy::ACCEPT_IF_ANY) {
      auto best_score = kfInfty;
      for (auto obj : getter_(agent)) {
        auto s = score_(agent, pos, obj);
        if (s < best_score) {
          best_score = s;
        }
      }
      return best_score;
    } else if (policy_ == PositionFilterPolicy::ACCEPT_IF_ALL) {
      auto best_score = -kfInfty;
      for (auto obj : getter_(agent)) {
        auto s = score_(agent, pos, obj);
        if (s > best_score) {
          best_score = s;
        }
      }
      return best_score;
    } else {
      LOG(DFATAL) << "MoveHelpers: incorrect position filter policy";
      return 0;
    }
  }

  bool blocking() override {
    return blocking_;
  }

 protected:
  std::function<Container const && (Unit*)> getter_;
  std::function<bool(Unit*, Position const&, T)> valid_;
  std::function<float(Unit*, Position const&, T)> score_;
  PositionFilterPolicy policy_;
  bool blocking_;
};

/**
 * This filter uses the score of the base filter, only of all subfilters
 * return that the position is valid.
 */
class MultiPositionFilter : public PositionFilter {
 public:
  MultiPositionFilter(
      PPositionFilter base,
      PositionFilters l,
      bool blocking = false)
      : base_(base), allFilters_(l), blocking_(blocking) {}

  bool isValid(Unit* agent, Position const& pos) override {
    if (!base_->isValid(agent, pos)) {
      return false;
    }
    for (auto& filt : allFilters_) {
      if (!filt->isValid(agent, pos)) {
        return false;
      }
    }
    return true;
  }

  float score(Unit* agent, Position const& pos) override {
    return base_->score(agent, pos);
  }

  bool blocking() override {
    return blocking_;
  }

 protected:
  PPositionFilter base_;
  PositionFilters allFilters_;
  bool blocking_;
};

/**
 * class to combine filters
 */
class UnionPositionFilter : public PositionFilter {
 public:
  UnionPositionFilter(
      PositionFilters l,
      PositionFilterPolicy policy = PositionFilterPolicy::ACCEPT_IF_ALL,
      bool blocking = false)
      : allFilters_(l), policy_(policy), blocking_(blocking) {}

  bool isValid(Unit* agent, Position const& pos) override {
    if (policy_ == PositionFilterPolicy::ACCEPT_IF_ALL) {
      for (auto& filt : allFilters_) {
        if (!filt->isValid(agent, pos)) {
          return false;
        }
      }
      return true;
    } else if (policy_ == PositionFilterPolicy::ACCEPT_IF_ANY) {
      for (auto& filt : allFilters_) {
        if (filt->isValid(agent, pos)) {
          return true;
        }
      }
      return false;
    }
    LOG(ERROR) << "invalid moveFilter policy";
    return false;
  }

  float score(Unit* agent, Position const& pos) override {
    if (policy_ == PositionFilterPolicy::ACCEPT_IF_ALL) {
      float max_score = -kfInfty;
      for (auto& filt : allFilters_) {
        auto score = filt->score(agent, pos);
        if (score > max_score) {
          max_score = score;
        }
      }
      return max_score;
    } else if (policy_ == PositionFilterPolicy::ACCEPT_IF_ANY) {
      float min_score = kfInfty;
      for (auto& filt : allFilters_) {
        auto score = filt->score(agent, pos);
        if (score < min_score) {
          min_score = score;
        }
      }
      return min_score;
    }
    LOG(ERROR) << "invalid moveFilter policy";
    return -kfInfty;
  }

  bool blocking() override {
    return blocking_;
  }

 protected:
  PositionFilters allFilters_;
  PositionFilterPolicy policy_;
  bool blocking_;
};

class ConstantGetter {
 public:
  ConstantGetter(std::vector<Position> values) : storage_(values) {}
  std::vector<Position> const& operator()(Unit* agent) {
    return storage_;
  }

 private:
  std::vector<Position> storage_;
};

// Makes a functional position filter of an arbitrary container reference
template <typename T, typename UnaryFunctionReturnsContainerTType>
PPositionFilter makePositionFilter(
    UnaryFunctionReturnsContainerTType getter,
    std::function<bool(Unit*, Position const&, T)> valid,
    std::function<float(Unit*, Position const&, T)> scoreFunc,
    PositionFilterPolicy policy = PositionFilterPolicy::ACCEPT_IF_ALL,
    bool blocking = false) {
  auto filter = std::make_shared<
      FuncPositionFilter<T, decltype(getter(std::declval<Unit*>()))>>(
      std::move(getter),
      std::move(valid),
      std::move(scoreFunc),
      policy,
      blocking);
  return std::static_pointer_cast<PositionFilter>(filter);
}

PPositionFilter makePositionFilter(
    PPositionFilter base,
    PositionFilters l,
    bool blocking = false);

PPositionFilter makePositionFilter(
    PositionFilters l,
    PositionFilterPolicy policy = PositionFilterPolicy::ACCEPT_IF_ALL,
    bool blocking = false);

bool insideSpecificUnit(Position const& pos, Unit* bldg, int margin = 0);
bool insideSpecificUnit(Unit* unit, Position const& pos, Unit* bldg);
bool unitTouch(Unit* unit, Unit* v, int dirX = 0, int dirY = 0);
bool insideAnyUnit(Unit* unit, Position const& pos, std::vector<Unit*> units);
bool positionAvoids(Unit* agent, Position const& pos, Unit* nmy);
bool dangerousAttack(Unit* unit, Unit* tgt);

inline std::vector<Unit*>& threateningEnemiesGetter(Unit* u) {
  return u->threateningEnemies;
}
inline std::vector<Unit*>& beingAttackedByEnemiesGetter(Unit* u) {
  return u->beingAttackedByEnemies;
}
inline std::vector<Unit*>& unitsInSightRangeGetter(Unit* u) {
  return u->unitsInSightRange;
}
inline std::vector<Unit*>& obstaclesInSightRangeGetter(Unit* u) {
  return u->obstaclesInSightRange;
}
inline std::vector<Unit*>& enemyUnitsInSightRangeGetter(Unit* u) {
  return u->enemyUnitsInSightRange;
}
inline std::vector<Unit*>& allyUnitsInSightRangeGetter(Unit* u) {
  return u->allyUnitsInSightRange;
}
inline auto negDistanceScore(Unit* unit, Position const& pos, Unit* nmy) {
  return -pos.distanceTo(nmy);
}
inline auto distanceScore(Unit* unit, Position const& pos, Unit* nmy) {
  return pos.distanceTo(nmy);
}
inline auto zeroScore(Unit* unit, Position const& pos, Unit* nmy) {
  return 0;
}

PPositionFilter fleeAttackers();
PPositionFilter fleeThreatening();
PPositionFilter avoidAttackers();
PPositionFilter avoidThreatening();
PPositionFilter avoidEnemyUnitsInRange(float range);
PPositionFilter getCloserTo(std::vector<Position> coordinates);
PPositionFilter getCloserTo(Unit* bldg);
PPositionFilter getCloserTo(Position const& pos);
bool walkable(State* state, Position const& pos);

int constexpr kMoveLength = 16;
int constexpr kNumberPossibleMoves = 64;
int constexpr kMinMoveLength = 8;
int constexpr kMoveLOSStepSize = 4; // discretization to check line of sight
int constexpr kMinDistToTargetPos =
    8; // too close to be considered as direction
int constexpr kTimeUpdateMove = 7;

bool moveIsPossible(
    State* state,
    Position const& pos,
    std::vector<Unit*> const& obstacles,
    bool outOFBoundsInvalid);

std::vector<std::vector<std::pair<float, Position>>> getValidMovePositions(
    State* state,
    Unit* unit,
    PositionFilters const& filters,
    int moveLength = kMoveLength,
    int nbPossibleMoves = kNumberPossibleMoves,
    int stepSize = kMoveLOSStepSize,
    bool outOfBoundsInvalid = true);

// Get the best position to move under the position  filters. The best position
// is defined by the minimum score of the first filter in filters with minimum
// score. filters can be though of as a list of policies, where we follow them
// in order until we find a policy that gives a valid position.
template <typename T>
Position safeDirectionTo(State* state, Unit* unit, T tgt);
Position safeMoveTo(State* state, Unit* unit, Position const& pos);
Position pathMoveTo(State* state, Unit* unit, Position const& pos);

Position smartMove(
    State* state,
    Unit* unit,
    PositionFilters const& filters,
    int moveLength = kMoveLength,
    int nbPossibleMoves = kNumberPossibleMoves,
    int stepSize = kMoveLOSStepSize,
    bool outOfBoundsInvalid = true);

Position smartMove(
    State* state,
    Unit* unit,
    PPositionFilter const& filter,
    int moveLength = kMoveLength,
    int nbPossibleMoves = kNumberPossibleMoves,
    int stepSize = kMoveLOSStepSize,
    bool outOfBoundsInvalid = true);

Position smartMove(State* state, Unit* unit, Position tgt);

} // namespace movefilters
} // namespace cherrypi
