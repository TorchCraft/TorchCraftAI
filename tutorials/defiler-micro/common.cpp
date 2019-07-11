/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 */

#include "common.h"
#include "flags.h"

namespace microbattles {
std::tuple<float, float, float, float> getUnitCountsHealth(
    cherrypi::State* state) {
  auto& allies = state->unitsInfo().myUnits();
  auto& enemies = state->unitsInfo().enemyUnits();
  float allyCount = allies.size();
  float enemyCount = enemies.size();
  float allyHp = 0;
  float enemyHp = 0;
  for (auto& ally : allies) {
    allyHp += ally->unit.health + ally->unit.shield; // Include shield in HP
  }
  for (auto& enemy : enemies) {
    enemyHp += enemy->unit.health + enemy->unit.shield;
  }
  return std::make_tuple(allyCount, enemyCount, allyHp, enemyHp);
}

at::Device defaultDevice() {
  return FLAGS_gpu ? torch::Device("cuda") : torch::Device("cpu");
}

} // namespace microbattles
