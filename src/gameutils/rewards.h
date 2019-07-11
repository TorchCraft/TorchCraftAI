/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "state.h"

namespace cherrypi {

struct Reward {
  int initialAllyCount = 0;
  int initialAllyHp = 0;
  int initialEnemyCount = 0;
  int initialEnemyHp = 0;
  float allyCount, enemyCount, allyHp, enemyHp;
  // We always gives the -1 / 1 as win lose reward
  bool won;

  virtual ~Reward() = default;
  virtual void begin(cherrypi::State* state);
  virtual void stepReward(cherrypi::State* state);
  virtual void computeReward(cherrypi::State* state) = 0;
  virtual bool terminate(cherrypi::State* state);
  virtual bool terminateOnPeace();
  // This function will log the metrics we want for micro scenarios
  virtual void logMetrics(cherrypi::State* state);
  double reward = -1e10;
};

std::unique_ptr<Reward> combatReward();
std::unique_ptr<Reward> killSpeedReward();
std::unique_ptr<Reward> proximityToReward(int y, int x);
std::unique_ptr<Reward> proximityToEnemyReward();
std::unique_ptr<Reward> protectCiviliansReward();
std::unique_ptr<Reward> defilerProtectZerglingsReward();
std::unique_ptr<Reward> defilerWinLossReward();
std::unique_ptr<Reward> defilerFullGameCombatReward();

} // namespace cherrypi
