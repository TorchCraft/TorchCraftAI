/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "reward.h"

#include "utils.h"

namespace microbattles {

struct RewardCombat : public Reward {
  void begin(cherrypi::State* state) override;
  void stepReward(cherrypi::State* state) override;
  int initialAllyCount = 0;
  int initialAllyHp = 0;
  int initialEnemyCount = 0;
  int initialEnemyHp = 0;
};
std::unique_ptr<Reward> combatReward() {
  return std::make_unique<RewardCombat>();
}

struct RewardKillSpeed : public Reward {
  void stepReward(cherrypi::State* state) override;
};
std::unique_ptr<Reward> killSpeedReward() {
  return std::make_unique<RewardKillSpeed>();
}

struct RewardProximityToEnemy : public Reward {
  void stepReward(cherrypi::State* state) override;
  bool terminate(cherrypi::State* state) override;
  bool terminateOnPeace() override {
    return false;
  }
};
std::unique_ptr<Reward> proximityToEnemyReward() {
  return std::make_unique<RewardProximityToEnemy>();
}

struct RewardProximityTo : public Reward {
  RewardProximityTo(int goalX, int goalY) : goalX_(goalX), goalY_(goalY) {}
  void stepReward(cherrypi::State* state) override;
  bool terminate(cherrypi::State* state) override;
  bool terminateOnPeace() override {
    return false;
  }

 private:
  int goalX_, goalY_;
};
std::unique_ptr<Reward> proximityToReward(int y, int x) {
  return std::make_unique<RewardProximityTo>(y, x);
}

struct RewardProtectCivilians : public Reward {
  void stepReward(cherrypi::State* state) override;
  bool terminateOnPeace() override {
    return false;
  }
};
std::unique_ptr<Reward> protectCiviliansReward() {
  return std::make_unique<RewardProtectCivilians>();
}

void Reward::begin(cherrypi::State* state) {}

bool Reward::terminate(cherrypi::State* state) {
  return state->unitsInfo().myUnits().empty() ||
      state->unitsInfo().enemyUnits().empty();
}

void RewardCombat::begin(cherrypi::State* state) {
  for (auto unit : state->unitsInfo().allUnitsEver()) {
    auto hp = unit->type->maxHp + unit->type->maxShields;
    if (unit->isMine) {
      ++initialAllyCount;
      initialAllyHp += hp;
    }
    if (unit->isEnemy) {
      ++initialEnemyCount;
      initialEnemyHp += hp;
    }
  }
}
void RewardCombat::stepReward(cherrypi::State* state) {
  float allyCount, enemyCount, allyHp, enemyHp;
  std::tie(allyCount, enemyCount, allyHp, enemyHp) = getUnitCountsHealth(state);

  float kills = (initialEnemyCount - enemyCount) / initialEnemyCount;
  float enemyDamage = (initialEnemyHp - enemyHp) / initialEnemyHp;
  float lives = allyCount / initialAllyCount;
  float win = enemyCount == 0 && allyCount != 0;

  reward = (enemyDamage + lives * 2 + kills * 4 + win * 8) / 16;
}

void RewardKillSpeed::stepReward(cherrypi::State* state) {
  auto allies = state->unitsInfo().myUnits();
  if (allies.empty()) {
    reward = -24 * 60 * 60;
  } else {
    reward = -state->currentFrame();
  }
}

void RewardProximityToEnemy::stepReward(cherrypi::State* state) {
  auto allies = state->unitsInfo().myUnits();
  auto enemies = state->unitsInfo().enemyUnits();
  if (enemies.empty()) {
    reward = -kMapDiagonal * 100;
    return;
  }
  reward = 0.0;
  for (auto enemy : enemies) {
    auto minDistance = kMapDiagonal / 2;
    for (auto ally : allies) {
      minDistance =
          std::min(minDistance, (double)cherrypi::utils::distance(ally, enemy));
    }
    reward -= minDistance;
  }
}

bool RewardProximityToEnemy::terminate(cherrypi::State* state) {
  return reward > -1 || Reward::terminate(state);
}

void RewardProximityTo::stepReward(cherrypi::State* state) {
  auto allies = state->unitsInfo().myUnits();
  reward = 0.0;
  for (auto ally : allies) {
    reward -= cherrypi::utils::distance(ally->x, ally->y, goalX_, goalY_);
  }
}

bool RewardProximityTo::terminate(cherrypi::State* state) {
  return reward > -1 || Reward::terminate(state);
}

void RewardProtectCivilians::stepReward(cherrypi::State* state) {
  auto isCivilian = [](cherrypi::Unit* unit) {
    return unit->type == cherrypi::buildtypes::Terran_Civilian;
  };
  auto isEnemy = [&isCivilian](cherrypi::Unit* unit) {
    return !isCivilian(unit) && unit->isEnemy;
  };
  auto unitsEver = state->unitsInfo().allUnitsEver();
  auto unitsLive = state->unitsInfo().liveUnits();
  auto civiliansMax =
      std::count_if(unitsEver.begin(), unitsEver.end(), isCivilian);
  auto civiliansNow =
      std::count_if(unitsLive.begin(), unitsLive.end(), isCivilian);
  auto enemiesMax = std::count_if(unitsEver.begin(), unitsEver.end(), isEnemy);
  auto enemiesNow = std::count_if(unitsLive.begin(), unitsLive.end(), isEnemy);
  reward = (enemiesMax - enemiesNow) - 5 * (civiliansMax - civiliansNow);
}

} // namespace microbattles
