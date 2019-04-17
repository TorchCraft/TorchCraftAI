/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "rewards.h"
#include "utils.h"

namespace cherrypi {

// TODO: Map sizes can VARY and these definitions are duplicated across
// rewards.cpp / microfixedscenariopool.cpp
constexpr int mapMidpointX = 128;
constexpr int mapMidpointY = 128;
const double kMapDiagonal =
    2 * sqrt(mapMidpointX * mapMidpointX + mapMidpointY * mapMidpointY);

void Reward::begin(State* state) {}

bool Reward::terminate(State* state) {
  return state->unitsInfo().myUnits().empty() ||
      state->unitsInfo().enemyUnits().empty();
}

bool Reward::terminateOnPeace() {
  return true;
}

std::tuple<float, float, float, float> getUnitCountsHealth(State* state) {
  auto allies = state->unitsInfo().myUnits();
  auto enemies = state->unitsInfo().enemyUnits();
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

struct RewardCombat : public Reward {
  void begin(State* state) override;
  void stepReward(State* state) override;
  int initialAllyCount = 0;
  int initialAllyHp = 0;
  int initialEnemyCount = 0;
  int initialEnemyHp = 0;
};
std::unique_ptr<Reward> combatReward() {
  return std::make_unique<RewardCombat>();
}

struct RewardKillSpeed : public Reward {
  void stepReward(State* state) override;
};
std::unique_ptr<Reward> killSpeedReward() {
  return std::make_unique<RewardKillSpeed>();
}

struct RewardProximityToEnemy : public Reward {
  void stepReward(State* state) override;
  bool terminate(State* state) override;
  bool terminateOnPeace() override {
    return false;
  }
};
std::unique_ptr<Reward> proximityToEnemyReward() {
  return std::make_unique<RewardProximityToEnemy>();
}

struct RewardProximityTo : public Reward {
  RewardProximityTo(int goalX, int goalY) : goalX_(goalX), goalY_(goalY) {}
  void stepReward(State* state) override;
  bool terminate(State* state) override;
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
  void stepReward(State* state) override;
  bool terminateOnPeace() override {
    return false;
  }
};
std::unique_ptr<Reward> protectCiviliansReward() {
  return std::make_unique<RewardProtectCivilians>();
}

struct RewardDefilerProtectZerglings : public Reward {
  void stepReward(State* state) override;
  bool terminateOnPeace() override {
    return false;
  }
  bool terminate(State* state) override {
    return state->unitsInfo().myUnits().empty() ||
        state->unitsInfo().enemyUnits().empty() ||
        state->unitsInfo().myUnitsOfType(buildtypes::Zerg_Zergling).empty();
  }
};

struct RewardDefilerWinLoss : public Reward {
  void stepReward(State* state) override;
  bool terminateOnPeace() override {
    return false;
  }
  bool terminate(State* state) override {
    return state->unitsInfo().myUnits().empty() ||
        state->unitsInfo().enemyUnits().empty() ||
        state->unitsInfo().myUnitsOfType(buildtypes::Zerg_Zergling).empty();
  }
};

std::unique_ptr<Reward> defilerProtectZerglingsReward() {
  return std::make_unique<RewardDefilerProtectZerglings>();
}

void RewardCombat::begin(State* state) {
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
void RewardCombat::stepReward(State* state) {
  float allyCount, enemyCount, allyHp, enemyHp;
  std::tie(allyCount, enemyCount, allyHp, enemyHp) = getUnitCountsHealth(state);

  float kills = (initialEnemyCount - enemyCount) / initialEnemyCount;
  float enemyDamage = (initialEnemyHp - enemyHp) / initialEnemyHp;
  float lives = allyCount / initialAllyCount;
  float win = enemyCount == 0 && allyCount != 0;

  reward = (enemyDamage + lives * 2 + kills * 4 + win * 8) / 16;
}

void RewardKillSpeed::stepReward(State* state) {
  auto allies = state->unitsInfo().myUnits();
  if (allies.empty()) {
    reward = -24 * 60 * 60;
  } else {
    reward = -state->currentFrame();
  }
}

void RewardProximityToEnemy::stepReward(State* state) {
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
      minDistance = std::min(minDistance, (double)utils::distance(ally, enemy));
    }
    reward -= minDistance;
  }
}

bool RewardProximityToEnemy::terminate(State* state) {
  return reward > -1 || Reward::terminate(state);
}

void RewardProximityTo::stepReward(State* state) {
  auto allies = state->unitsInfo().myUnits();
  reward = 0.0;
  for (auto ally : allies) {
    reward -= utils::distance(ally->x, ally->y, goalX_, goalY_);
  }
}

bool RewardProximityTo::terminate(State* state) {
  return reward > -1 || Reward::terminate(state);
}

void RewardProtectCivilians::stepReward(State* state) {
  auto isCivilian = [](Unit* unit) {
    return unit->type == buildtypes::Terran_Civilian;
  };
  auto isEnemy = [&isCivilian](Unit* unit) {
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

void RewardDefilerProtectZerglings::stepReward(State* state) {
  auto isZerglings = [](Unit* unit) {
    return unit->type == buildtypes::Zerg_Zergling;
  };
  auto isEnemy = [](Unit* unit) { return unit->isEnemy; };
  auto unitsEver = state->unitsInfo().allUnitsEver();
  auto unitsLive = state->unitsInfo().liveUnits();
  auto ZerglingsMax =
      std::count_if(unitsEver.begin(), unitsEver.end(), isZerglings);
  auto zerglingsNow =
      std::count_if(unitsLive.begin(), unitsLive.end(), isZerglings);
  auto enemiesMax = std::count_if(unitsEver.begin(), unitsEver.end(), isEnemy);
  auto enemiesNow = std::count_if(unitsLive.begin(), unitsLive.end(), isEnemy);
  reward = (enemiesMax - enemiesNow) / enemiesMax -
      ((ZerglingsMax - zerglingsNow) / ZerglingsMax);
}

void RewardDefilerWinLoss::stepReward(State* state) {
  reward = state->unitsInfo().enemyUnits().empty() ? 1 : 0;
}

std::unique_ptr<Reward> defilerWinLossReward() {
  return std::make_unique<RewardDefilerWinLoss>();
}

} // namespace cherrypi