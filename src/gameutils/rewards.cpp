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

std::pair<float, float> getUnitCountsHealth(std::vector<Unit*> units) {
  float unitsCount = units.size();
  float unitsHp = 0;
  for (auto& unit : units) {
    unitsHp += unit->unit.health + unit->unit.shield; // Include shield in HP
  }
  return std::make_pair(unitsCount, unitsHp);
}

void Reward::begin(State* state) {
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

void Reward::stepReward(State* state) {
  computeReward(state);
  logMetrics(state);
}

void Reward::logMetrics(State* state) {
  std::tie(allyCount, allyHp) =
      getUnitCountsHealth(state->unitsInfo().myUnits());
  std::tie(enemyCount, enemyHp) =
      getUnitCountsHealth(state->unitsInfo().enemyUnits());
  won = state->unitsInfo().myUnits().size() != 0 &&
      state->unitsInfo().enemyUnits().size() == 0;
}

bool Reward::terminate(State* state) {
  return state->unitsInfo().myUnits().empty() ||
      state->unitsInfo().enemyUnits().empty();
}

bool Reward::terminateOnPeace() {
  return true;
}

struct RewardCombat : public Reward {
  void begin(State* state) override;
  void computeReward(State* state) override;
};

std::unique_ptr<Reward> combatReward() {
  return std::make_unique<RewardCombat>();
}

struct RewardKillSpeed : public Reward {
  void computeReward(State* state) override;
};

std::unique_ptr<Reward> killSpeedReward() {
  return std::make_unique<RewardKillSpeed>();
}

struct RewardProximityToEnemy : public Reward {
  void computeReward(State* state) override;
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
  void computeReward(State* state) override;
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
  void computeReward(State* state) override;
  bool terminateOnPeace() override {
    return false;
  }
};
std::unique_ptr<Reward> protectCiviliansReward() {
  return std::make_unique<RewardProtectCivilians>();
}

struct RewardDefilerProtectZerglings : public Reward {
  void computeReward(State* state) override;
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
  void computeReward(State* state) override;
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
void RewardCombat::computeReward(State* state) {
  std::tie(allyCount, allyHp) =
      getUnitCountsHealth(state->unitsInfo().myUnits());
  std::tie(enemyCount, enemyHp) =
      getUnitCountsHealth(state->unitsInfo().enemyUnits());
  float kills = (initialEnemyCount - enemyCount) / initialEnemyCount;
  float enemyDamage = (initialEnemyHp - enemyHp) / initialEnemyHp;
  float lives = allyCount / initialAllyCount;
  float win = enemyCount == 0 && allyCount != 0;

  reward = (enemyDamage + lives * 2 + kills * 4 + win * 8) / 16;
}

void RewardKillSpeed::computeReward(State* state) {
  auto allies = state->unitsInfo().myUnits();
  if (allies.empty()) {
    reward = -24 * 60 * 60;
  } else {
    reward = -state->currentFrame();
  }
}

void RewardProximityToEnemy::computeReward(State* state) {
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

void RewardProximityTo::computeReward(State* state) {
  auto allies = state->unitsInfo().myUnits();
  reward = 0.0;
  for (auto ally : allies) {
    reward -= utils::distance(ally->x, ally->y, goalX_, goalY_);
  }
}

bool RewardProximityTo::terminate(State* state) {
  return reward > -1 || Reward::terminate(state);
}

void RewardProtectCivilians::computeReward(State* state) {
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

void RewardDefilerProtectZerglings::computeReward(State* state) {
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

void RewardDefilerWinLoss::computeReward(State* state) {
  reward = state->unitsInfo().enemyUnits().empty() ? 1 : 0;
}

std::unique_ptr<Reward> defilerWinLossReward() {
  return std::make_unique<RewardDefilerWinLoss>();
}

struct DefilerFullGameCombatReward : public RewardCombat {
  void begin(State* state) override;
  bool terminate(State* state) override;
  void computeReward(State* state) override;
  void logMetrics(cherrypi::State* state) override;

 protected:
  void updateMilitaryUnits(State* state);
  std::vector<Unit*> myMilitaryUnits_;
  std::vector<Unit*> enemyMilitaryUnits_;
  long lastUpdateFrame_;
};

std::unique_ptr<Reward> defilerFullGameCombatReward() {
  return std::make_unique<DefilerFullGameCombatReward>();
}

void DefilerFullGameCombatReward::updateMilitaryUnits(State* state) {
  auto isMilitary = [](Unit* unit) {
    return (!unit->type->isBuilding) && (!unit->type->isWorker) &&
        (unit->type != buildtypes::Zerg_Overlord);
  };
  if (lastUpdateFrame_ == state->currentFrame()) {
    return;
  }
  auto myUnits = state->unitsInfo().myUnits();
  myMilitaryUnits_.clear();
  enemyMilitaryUnits_.clear();
  std::copy_if(
      myUnits.begin(),
      myUnits.end(),
      std::back_inserter(myMilitaryUnits_),
      isMilitary);
  auto enemyUnits = state->unitsInfo().enemyUnits();
  std::copy_if(
      enemyUnits.begin(),
      enemyUnits.end(),
      std::back_inserter(enemyMilitaryUnits_),
      isMilitary);
  lastUpdateFrame_ = state->currentFrame();
}

void DefilerFullGameCombatReward::begin(State* state) {
  updateMilitaryUnits(state);
  for (auto unit : myMilitaryUnits_) {
    auto hp = unit->type->maxHp + unit->type->maxShields;
    ++initialAllyCount;
    initialAllyHp += hp;
  }
  for (auto unit : enemyMilitaryUnits_) {
    auto hp = unit->type->maxHp + unit->type->maxShields;
    ++initialEnemyCount;
    initialEnemyHp += hp;
  }
}

void DefilerFullGameCombatReward::computeReward(State* state) {
  updateMilitaryUnits(state);
  std::tie(allyCount, allyHp) = getUnitCountsHealth(myMilitaryUnits_);
  std::tie(enemyCount, enemyHp) = getUnitCountsHealth(enemyMilitaryUnits_);
  float kills = (initialEnemyCount - enemyCount) / initialEnemyCount;
  float enemyDamage = (initialEnemyHp - enemyHp) / initialEnemyHp;
  float lives = allyCount / initialAllyCount;
  won = (enemyCount == 0 && allyCount != 0) ||
      (enemyCount != 0 && allyCount != 0 && allyCount > enemyCount);
  float win = won ? 1. : 0.;
  reward = (enemyDamage + lives * 2 + kills * 4 + win * 8) / 16;
}

void DefilerFullGameCombatReward::logMetrics(State* state) {}

bool DefilerFullGameCombatReward::terminate(State* state) {
  if (state->unitsInfo().myUnits().empty() ||
      state->unitsInfo().enemyUnits().empty()) {
    return true;
  }
  updateMilitaryUnits(state);
  return myMilitaryUnits_.empty() || enemyMilitaryUnits_.empty();
}

} // namespace cherrypi
