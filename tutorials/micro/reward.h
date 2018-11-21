/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "common.h"
#include "utils.h"

namespace microbattles {

struct Reward {
  virtual void begin(cherrypi::State* state);
  virtual void stepReward(cherrypi::State* state) = 0;
  virtual bool terminate(cherrypi::State* state);
  virtual bool terminateOnPeace() {
    return true;
  };
  double reward = -1e10;
};

std::unique_ptr<Reward> combatReward();
std::unique_ptr<Reward> killSpeedReward();
std::unique_ptr<Reward> proximityToEnemyReward();
std::unique_ptr<Reward> proximityToReward(int y, int x);
std::unique_ptr<Reward> protectCiviliansReward();
} // namespace microbattles
