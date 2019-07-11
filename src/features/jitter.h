/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "cherrypi.h"

namespace cherrypi {

struct Unit;
class State;

class BaseJitter {
 public:
  BaseJitter(){};
  virtual ~BaseJitter() = default;

  virtual Position operator()(Unit* u) const = 0;
};

class NoJitter : public BaseJitter {
 public:
  NoJitter(){};
  Position operator()(Unit* u) const override;
};

/**
 * When featurizing units, we represent each 2D cell as having one unit.
 * Of course, StarCraft isn't so neat and tidy. Multiple units can be stacked on
 * one location; sometimes ground units, but frequently air units as well.
 *
 * In order to featurize units on a 2D grid, we apply jitter to shake those
 * units out into a one-to-one cell-to-(unit or no unit) mapping. Units get
 * moved into nearby cells for featurization.
 *
 * This jitter class treats all units indiscriminately. If allowSameType is
 * true, then we allow units of the same type to be on the same tile (no matter
 * if they are jittered or not).
 *
 * Warning: This will not behave as expected for tanks since sieged and unsieged
 * are two different units. However, stacked tanks should be almost impossible
 * in normal situations.
 *
 * Note that neutral units will always be ignored.
 */
class Jitter : public BaseJitter {
 public:
  Jitter(State* st, Rect const& crop, bool allowSameType);
  Position operator()(Unit* u) const override;

 protected:
  Jitter(){};

  void fillJitter(
      State* st,
      Rect const& crop,
      std::function<bool(Unit*, Unit*)> const& compatible);

  std::unordered_map<Unit*, Position> jitteredPos_;
};

/**
 * This jitter class treats all units depending on their height: flying, on
 * the ground or under ground (burrowed). For example, we make sure that each
 * flying unit is on a separate tile but a flying unit can be on the same tile
 * as a ground unit.
 *
 * Note that neutral units will always be ignored
 */
class LayeredJitter : public Jitter {
 public:
  LayeredJitter(
      State* st,
      Rect const& crop,
      bool allowSameTypeAir = true,
      bool allowSameTypeGround = false);
};

} // namespace cherrypi
