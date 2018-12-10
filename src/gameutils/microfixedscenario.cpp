/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "microfixedscenario.h"

#include "utils.h"

namespace cherrypi {

void MicroFixedScenario::Reward::begin(cherrypi::State* state) {}

bool MicroFixedScenario::Reward::terminate(cherrypi::State* state) {
  return state->unitsInfo().myUnits().empty() ||
      state->unitsInfo().enemyUnits().empty();
}

bool MicroFixedScenario::Reward::terminateOnPeace() {
  return true;
}

namespace {
using bt = torchcraft::BW::UnitType;
using Reward = MicroFixedScenario::Reward;
constexpr int mapMidpointX = 128;
constexpr int mapMidpointY = 128;
const double kMapDiagonal =
    2 * sqrt(mapMidpointX * mapMidpointX + mapMidpointY * mapMidpointY);
static std::string mapPathPrefix_ = "";

struct ScenarioInfo {
  std::string name;
  cherrypi::SpawnList allyList;
  cherrypi::SpawnList enemyList;
  std::string map{"test/maps/micro-empty2.scm"};
  std::function<std::unique_ptr<Reward>()> reward;
};

struct FixedScenarioGroup {
  std::string name;
  std::vector<ScenarioInfo> scenarios = std::vector<ScenarioInfo>();

  ScenarioInfo& add(std::string name) {
    scenarios.emplace_back(ScenarioInfo{name});
    return scenarios.back();
  };
};

std::tuple<float, float, float, float> getUnitCountsHealth(
    cherrypi::State* state) {
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

int rangeOn(int minInclusive, int maxInclusive) {
  return minInclusive + rand() % (maxInclusive - minInclusive);
};

struct UnitCount {
  bt unitType;
  int count;
  UnitCount(int unitTypeId, int count = rangeOn(2, 5))
      : unitType(bt::_from_integral(unitTypeId)), count(count) {}
};

void asymmetric(
    ScenarioInfo& scenario,
    const std::vector<UnitCount>&& unitsAlly,
    const std::vector<UnitCount>&& unitsEnemy,
    int distance = 40,
    float spread = 5) {
  std::for_each(unitsAlly.begin(), unitsAlly.end(), [&](const UnitCount& unit) {
    scenario.allyList.insert(
        {unit.unitType,
         {unit.count,
          mapMidpointX - distance / 2,
          mapMidpointY,
          spread,
          spread}});
  });

  std::for_each(
      unitsEnemy.begin(), unitsEnemy.end(), [&](const UnitCount& unit) {
        scenario.enemyList.insert(
            {unit.unitType,
             {unit.count,
              mapMidpointX + distance / 2,
              mapMidpointY,
              spread,
              spread}});
      });
};

void asymmetricRandom(
    ScenarioInfo& scenario,
    const std::vector<UnitCount>&& unitsAlly,
    const std::vector<UnitCount>&& unitsEnemy,
    int mapCenterOffset = 50,
    int distanceMax = 60,
    int distanceMin = 40,
    float spread = 5) {
  int xoffset = rand() % mapCenterOffset;
  int yoffset = rand() % mapCenterOffset;
  auto pOrN = []() { return rand() % 2 == 0 ? 1 : -1; };
  auto range = [](int val) { return std::max(std::min(val, 256), 0); };
  int centerX = mapMidpointX + pOrN() * xoffset;
  int centerY = mapMidpointY + pOrN() * yoffset;
  int distance = distanceMin;
  if (distanceMax > distanceMin) {
    distance = distanceMin + rand() % (distanceMax - distanceMin);
  }
  int leftOrRight = pOrN();
  std::for_each(unitsAlly.begin(), unitsAlly.end(), [&](const UnitCount& unit) {
    scenario.allyList.insert(
        {unit.unitType,
         {unit.count,
          range(centerX - leftOrRight * distance / 2),
          range(centerY),
          spread,
          spread}});
  });

  std::for_each(
      unitsEnemy.begin(), unitsEnemy.end(), [&](const UnitCount& unit) {
        scenario.enemyList.insert(
            {unit.unitType,
             {unit.count,
              range(centerX + leftOrRight * distance / 2),
              range(centerY),
              spread,
              spread}});
      });
};

void symmetric(
    ScenarioInfo& scenario,
    const std::vector<UnitCount>&& units,
    int distance = 40,
    float spread = 5) {
  std::for_each(units.begin(), units.end(), [&](const UnitCount& unit) {
    scenario.allyList.insert(
        {unit.unitType,
         {unit.count,
          mapMidpointX - distance / 2,
          mapMidpointY,
          spread,
          spread}});
    scenario.enemyList.insert(
        {unit.unitType,
         {unit.count,
          mapMidpointX + distance / 2,
          mapMidpointY,
          spread,
          spread}});
  });
};

/**
 * Scenarios that have been used in previous papers
 */
FixedScenarioGroup baselineScenarios() {
  FixedScenarioGroup group;
  /**
   * Scenarios from previous works:
   * EE: 5m,5m 15m,16m 2d+3z,2d+3z w15,w17
   * COMA: 3m,3m 5m,5m 5w,5w 2d+3z,2d+3z
   */
  symmetric(group.add("3mr_3mr"), {{bt::Terran_Marine, 3}});
  symmetric(group.add("5mr_5mr"), {{bt::Terran_Marine, 5}});
  asymmetric(
      group.add("15mr_16mr"),
      {{bt::Terran_Marine, 15}},
      {{bt::Terran_Marine, 16}});
  symmetric(group.add("5wr_5wr"), {{bt::Terran_Wraith, 5}});
  asymmetric(
      group.add("15wr_17wr"),
      {{bt::Terran_Wraith, 15}},
      {{bt::Terran_Wraith, 17}});
  symmetric(
      group.add("2dr+3zl_2dr+3zl"),
      {{bt::Protoss_Dragoon, 2}, {bt::Protoss_Zealot, 3}});
  asymmetric(
      group.add("10mr_13zg"),
      {{bt::Terran_Marine, 10}},
      {{bt::Zerg_Zergling, 13}});

  return group;
}

/**
 * Some easier scenarios.
 */
FixedScenarioGroup simpleScenarios() {
  FixedScenarioGroup group;

  auto& hugMiddle = group.add("hugmiddle");
  hugMiddle.reward = []() {
    return proximityToReward(mapMidpointY, mapMidpointX);
  };
  asymmetric(
      hugMiddle, {{bt::Terran_Vulture, 1}}, {{bt::Zerg_Overlord, 1}}, 0, 15);

  auto& hugMiddleEasy = group.add("hugmiddleeasy");
  hugMiddleEasy.reward = []() {
    return proximityToReward(mapMidpointY, mapMidpointX);
  };
  hugMiddleEasy.allyList.insert(
      {bt::Terran_Vulture, {1, mapMidpointX, mapMidpointY, 15, 15}});
  hugMiddleEasy.enemyList.insert(
      {bt::Zerg_Overlord, {1, mapMidpointX, mapMidpointY, 0, 0}});

  auto& hugOverlords = group.add("hugoverlords");
  hugOverlords.reward = proximityToEnemyReward;
  asymmetric(
      hugOverlords, {{bt::Terran_Vulture, 2}}, {{bt::Zerg_Overlord, 2}}, 0, 15);

  auto& popOverlords = group.add("popoverlords");
  popOverlords.reward = killSpeedReward;
  asymmetric(
      popOverlords, {{bt::Terran_Wraith, 2}}, {{bt::Zerg_Overlord, 8}}, 0, 15);

  auto& ignoreCivilians = group.add("ignorecivilians");
  ignoreCivilians.reward = protectCiviliansReward;
  ignoreCivilians.allyList.insert(
      {bt::Zerg_Zergling, {1, mapMidpointX, mapMidpointY, 0, 12}});
  ignoreCivilians.enemyList.insert(
      {bt::Terran_Civilian, {4, mapMidpointX, mapMidpointY, 0, 12}});
  ignoreCivilians.enemyList.insert(
      {bt::Protoss_High_Templar, {1, mapMidpointX, mapMidpointY, 0, 12}});

  return group;
}

/**
 * Scenarios involving a symmetric fight between units of a single type
 */
FixedScenarioGroup symmetricSingleUnitScenarios() {
  FixedScenarioGroup group;
  auto make = [&](std::string name, bt unit) {
    symmetric(group.add(name), {{unit, rangeOn(3, 6)}});
  };
  make("sv", bt::Terran_SCV);
  make("mr", bt::Terran_Marine);
  make("fi", bt::Terran_Firebat);
  make("vu", bt::Terran_Vulture);
  make("go", bt::Terran_Goliath);
  make("st", bt::Terran_Siege_Tank_Tank_Mode);
  make("wr", bt::Terran_Wraith);
  make("bc", bt::Terran_Battlecruiser);
  make("pr", bt::Protoss_Probe);
  make("zl", bt::Protoss_Zealot);
  make("dr", bt::Protoss_Dragoon);
  make("ar", bt::Protoss_Archon);
  make("co", bt::Protoss_Corsair);
  make("sc", bt::Protoss_Scout);
  make("dn", bt::Zerg_Drone);
  make("zg", bt::Zerg_Zergling);
  make("hy", bt::Zerg_Hydralisk);
  make("ul", bt::Zerg_Ultralisk);
  make("mu", bt::Zerg_Mutalisk);
  make("de", bt::Zerg_Devourer);
  make("it", bt::Zerg_Infested_Terran);
  return group;
}

/**
 * Scenarios involving a symmetric fight between mixed air/ground units
 */
FixedScenarioGroup symmetricAirGroundScenarios() {
  FixedScenarioGroup group;
  auto make = [&](
      std::string name,
      bt unit0,
      bt unit1,
      int unit0count = rangeOn(2, 5),
      int unit1count = rangeOn(2, 5)) {
    symmetric(group.add(name), {{unit0, unit0count}, {unit1, unit1count}});
  };
  make("mr+wr", bt::Terran_Marine, bt::Terran_Wraith);
  make(
      "go+wr",
      bt::Terran_Goliath,
      bt::Terran_Wraith,
      rangeOn(2, 5),
      rangeOn(4, 7));
  make("go+bc", bt::Terran_Goliath, bt::Terran_Battlecruiser);
  make("dr+sc", bt::Protoss_Dragoon, bt::Protoss_Scout);
  make(
      "ar+sc",
      bt::Protoss_Archon,
      bt::Protoss_Scout,
      rangeOn(2, 5),
      rangeOn(6, 10));
  make("hy+mu", bt::Zerg_Hydralisk, bt::Zerg_Mutalisk);
  return group;
}

/**
 * Scenarios involving a symmetric fight between large numbers of units.
 */
FixedScenarioGroup symmetricBigScenarios() {
  FixedScenarioGroup group;
  auto make = [&](std::string name, bt unit) {
    symmetric(group.add(name), {{unit, 30}});
  };
  make("big_sv", bt::Terran_SCV);
  make("big_mr", bt::Terran_Marine);
  make("big_fb", bt::Terran_Firebat);
  make("big_gh", bt::Terran_Ghost);
  make("big_vu", bt::Terran_Vulture);
  make("big_go", bt::Terran_Goliath);
  make("big_st", bt::Terran_Siege_Tank_Tank_Mode);
  make("big_wr", bt::Terran_Wraith);
  make("big_bc", bt::Terran_Battlecruiser);
  make("big_pr", bt::Protoss_Probe);
  make("big_zl", bt::Protoss_Zealot);
  make("big_dr", bt::Protoss_Dragoon);
  make("big_ar", bt::Protoss_Archon);
  make("big_sc", bt::Protoss_Scout);
  make("big_dr", bt::Zerg_Drone);
  make("big_zg", bt::Zerg_Zergling);
  make("big_hy", bt::Zerg_Hydralisk);
  make("big_ul", bt::Zerg_Ultralisk);
  make("big_mu", bt::Zerg_Mutalisk);
  make("big_it", bt::Zerg_Infested_Terran);
  return group;
}

/**
 * Scenarios requiring regrouping before fighting
 */
FixedScenarioGroup regroupingScenarios() {
  FixedScenarioGroup group{"regrouping"};
  auto makeSurround = [&](std::string name, bt unit) {
    auto& scenario = group.add(name);
    int unitCount = 6;
    int enemyOffset = 12;
    int allyDistance = enemyOffset + 20;
    for (int i = 0; i < unitCount; ++i) {
      auto radians = 2.0 * M_PI * i / unitCount;
      auto allyX = mapMidpointX + allyDistance * cos(radians);
      auto allyY = mapMidpointY + allyDistance * sin(radians);
      scenario.allyList.insert({unit, {1, (int)allyX, (int)allyY, 0, 0}});
    }
    auto enemyR =
        (double)rand(); // This is likely not a good distribution of angles
    auto enemyX = mapMidpointX + enemyOffset * cos(enemyR);
    auto enemyY = mapMidpointY + enemyOffset * sin(enemyR);
    scenario.enemyList.insert(
        {unit, {unitCount, (int)enemyX, (int)enemyY, 0, 0}});
  };
  auto makeConga = [&](std::string name, bt unit) {
    group.add(name);
    auto& scenario = group.scenarios.back();
    int count = 5;
    int distance = 24;
    int spreadX = 20;
    int spreadY = 10;
    for (int i = 0; i < count; ++i) {
      int allyX = mapMidpointX - spreadX * i;
      int allyY = mapMidpointY;
      int enemyX = mapMidpointX + distance;
      int enemyY =
          mapMidpointY + spreadY * ((i + 1) / 2) * (i % 2 == 0 ? -1 : 1);
      scenario.allyList.insert({unit, {1, allyX, allyY, 0, 0}});
      scenario.enemyList.insert({unit, {1, enemyX, enemyY, 0, 0}});
    }
  };
  makeSurround("surround_sv", bt::Terran_SCV);
  makeSurround("surround_fb", bt::Terran_Firebat);
  makeSurround("surround_pr", bt::Protoss_Probe);
  makeSurround("surround_zl", bt::Protoss_Zealot);
  makeSurround("surround_ar", bt::Protoss_Archon);
  makeSurround("surround_dr", bt::Zerg_Drone);
  makeSurround("surround_zg", bt::Zerg_Zergling);
  makeSurround("surround_mu", bt::Zerg_Mutalisk);
  makeSurround("surround_ul", bt::Zerg_Ultralisk);
  makeConga("conga_sv", bt::Terran_SCV);
  makeConga("conga_fb", bt::Terran_Firebat);
  makeConga("conga_mr", bt::Terran_Marine);
  makeConga("conga_pr", bt::Protoss_Probe);
  makeConga("conga_zl", bt::Protoss_Zealot);
  makeConga("conga_dr", bt::Protoss_Dragoon);
  makeConga("conga_ar", bt::Protoss_Archon);
  makeConga("conga_dr", bt::Zerg_Drone);
  makeConga("conga_zg", bt::Zerg_Zergling);
  makeConga("conga_mu", bt::Zerg_Mutalisk);
  makeConga("conga_ul", bt::Zerg_Ultralisk);
  return group;
}

/**
 * Scenarios requiring alternating attack/move actions
 */
FixedScenarioGroup kitingScenarios() {
  FixedScenarioGroup group{"kiting"};

  // Scenarios where one side can perfectly kite the other
  asymmetric(
      group.add("1dr_1zl"),
      {{bt::Protoss_Dragoon, 1}},
      {{bt::Protoss_Zealot, 1}});
  asymmetric(
      group.add("2dr_3zl"),
      {{bt::Protoss_Dragoon, 2}},
      {{bt::Protoss_Zealot, 3}});
  asymmetric(
      group.add("1vu_3zg"),
      {{bt::Terran_Vulture, 1}},
      {{bt::Zerg_Zergling, 3}});
  asymmetric(
      group.add("2vu_7zg"),
      {{bt::Terran_Vulture, 2}},
      {{bt::Zerg_Zergling, 7}});
  asymmetric(
      group.add("3vu_11zg"),
      {{bt::Terran_Vulture, 3}},
      {{bt::Zerg_Zergling, 11}});
  asymmetric(
      group.add("1go_2zl"),
      {{bt::Terran_Goliath, 1}},
      {{bt::Protoss_Zealot, 2}});
  asymmetric(
      group.add("3go_8zl"),
      {{bt::Terran_Goliath, 3}},
      {{bt::Protoss_Zealot, 8}});
  asymmetric(
      group.add("1vu_1zl"),
      {{bt::Terran_Vulture, 1}},
      {{bt::Protoss_Zealot, 1}});
  auto& vulturesZealots = group.add("5vu_10zl");
  for (int i = 0; i < 5; i++) {
    vulturesZealots.allyList.insert(
        {bt::Terran_Vulture, {1, 140, 120 + 2 * i, 5}});
  }
  for (int i = 0; i < 10; i++) {
    vulturesZealots.enemyList.insert(
        {bt::Protoss_Zealot, {1, 120, 120 + 2 * i, 5}});
  }
  auto& vultureZealot = group.add("vu_zl");
  {
    const double pi = std::acos(-1);
    int x = 0, y = -1;
    while (!(x <= y && y <= 2 * x)) {
      x = rand() % 4 + 1;
      y = rand() % 9 + 1;
    }
    double deg = (rand() % 360) * pi / 180.;
    auto center = 130;
    auto radius = 10;
    int ctrx = radius * std::cos(deg);
    int ctry = radius * std::sin(deg);
    vultureZealot.allyList.insert(
        {bt::Terran_Vulture, {x, center + ctrx, center + ctry, 8, 8}});
    vultureZealot.enemyList.insert(
        {bt::Protoss_Zealot, {y, center - ctrx, center - ctry, 8, 8}});
  }

  // Siege tanks and Zealots have the same speed, but Siege Tanks shoot and
  // accelerate instantly
  // So Siege Tanks, controlled correctly, get a ton of free shots on Zealots
  // before the Zealots close the gap
  asymmetric(
      group.add("1st_2zl"),
      {{bt::Terran_Siege_Tank_Tank_Mode, 2}},
      {{bt::Protoss_Zealot, 3}},
      60,
      10);
  asymmetric(
      group.add("3st_7zl"),
      {{bt::Terran_Siege_Tank_Tank_Mode, 3}},
      {{bt::Protoss_Zealot, 5}},
      60,
      10);

  // Marines and Zealots have same speed
  // So the closest Marine needs to bait the Zealot while the other shoots, then
  // alternate
  asymmetric(
      group.add("2mr_1zl"),
      {{bt::Terran_Marine, 2}},
      {{bt::Protoss_Zealot, 1}});
  auto& marinesZealots = group.add("6mr_4zl");
  for (int i = 0; i < 6; ++i) {
    marinesZealots.allyList.insert(
        {bt::Terran_Marine, {1, 140, 120 + i * 2, 5}});
  }
  for (int i = 0; i < 4; ++i) {
    marinesZealots.enemyList.insert(
        {bt::Protoss_Zealot, {1, 120, 130 + i * 2, 5}});
  }

  // Scenarios where we can't kite the opponent
  // but we can trade more favorably by backing off between shots
  asymmetric(
      group.add("1vu_1hy"),
      {{bt::Terran_Vulture, 1}},
      {{bt::Zerg_Hydralisk, 1}});
  asymmetric(
      group.add("3vu_3hy"),
      {{bt::Terran_Vulture, 3}},
      {{bt::Zerg_Hydralisk, 3}});
  asymmetric(
      group.add("1dr_3zg"),
      {{bt::Protoss_Dragoon, 1}},
      {{bt::Zerg_Zergling, 3}});
  asymmetric(
      group.add("3dr_10zg"),
      {{bt::Protoss_Dragoon, 3}},
      {{bt::Zerg_Zergling, 10}});
  asymmetric(
      group.add("1mu_3mr"), {{bt::Zerg_Mutalisk, 2}}, {{bt::Terran_Marine, 5}});
  asymmetric(
      group.add("3mu_9m3"),
      {{bt::Zerg_Mutalisk, 4}},
      {{bt::Terran_Marine, 10}});

  return group;
}

/**
 * All other scenarios
 */
FixedScenarioGroup miscellaneousScenarios() {
  FixedScenarioGroup group{"miscellaneous"};

  // Mutalisks need to spread out against Corsair splash damage
  auto& mutalisk10Corsair5 = group.add("10mu_5co");
  for (int i = 0; i < 10; i++) {
    mutalisk10Corsair5.allyList.insert(
        {bt::Zerg_Mutalisk, {1, 140 + i * 2, 110, 5}});
  }
  for (int i = 0; i < 5; i++) {
    mutalisk10Corsair5.enemyList.insert(
        {bt::Protoss_Corsair, {1, 110, 120 + i * 3, 5}});
  }

  auto& mutalisk8Corsair5 = group.add("8mu_5co");
  for (int i = 0; i < 8; i++) {
    mutalisk8Corsair5.allyList.insert(
        {bt::Zerg_Mutalisk, {1, 140 + i * 2, 110, 5}});
  }
  for (int i = 0; i < 5; i++) {
    mutalisk8Corsair5.enemyList.insert(
        {bt::Protoss_Corsair, {1, 110, 120 + i * 3, 5}});
  }

  // Hydralisks vs. dragoons on high ground - need to go up ramp
  auto& hydralisksDragoonsRamp = group.add("3hy_2dr");
  hydralisksDragoonsRamp.allyList.insert(
      {bt::Zerg_Hydralisk, {3, 145, 145, 3}});
  hydralisksDragoonsRamp.enemyList.insert(
      {bt::Protoss_Dragoon, {2, 125, 128, 3}});
  hydralisksDragoonsRamp.map = "test/maps/micro/ramp_2wt.scx";

  // Hydralisks vs. siege-mode siege tank - need to get inside tank's min range
  auto hydralisksTank = group.add("2hy_1sst");
  hydralisksTank.allyList.insert({bt::Zerg_Hydralisk, {2, 140, 140, 5}});
  hydralisksTank.enemyList.insert(
      {bt::Terran_Siege_Tank_Siege_Mode, {1, 125, 110, 5}});

  // Tank min range scenario - note siege-mode adds friendly fire
  auto& hydralisksTanks = group.add("4hy_2sst");
  hydralisksTanks.allyList.insert({bt::Zerg_Hydralisk, {4, 90, 120, 8}});
  hydralisksTanks.enemyList.insert(
      {bt::Terran_Siege_Tank_Siege_Mode, {1, 155, 110, 5}});
  hydralisksTanks.enemyList.insert(
      {bt::Terran_Siege_Tank_Siege_Mode, {1, 165, 140, 5}});

  // Zerglings must surround zealots instead of attacking asap
  auto zerglingsZealots = group.add("30zg_10zl");
  for (int i = 0; i < 30; ++i) {
    zerglingsZealots.allyList.insert({bt::Zerg_Zergling, {1, 130 + i, 130, 3}});
  }
  for (int i = 0; i < 10; ++i) {
    zerglingsZealots.enemyList.insert(
        {bt::Protoss_Zealot, {1, 110, 120 + i * 2, 3}});
  }

  // Goliaths must stand ground and focus fire
  asymmetric(
      group.add("7zg_2gl"),
      {{bt::Terran_Goliath, 2}},
      {{bt::Zerg_Zergling, 7}},
      15,
      5);

  // Killing the Goliaths first ensures that the Mutalisks will win
  asymmetric(
      group.add("5mu+20zg_5gl+5vu"),
      {{bt::Zerg_Mutalisk, 5}, {bt::Zerg_Zergling, 20}},
      {{bt::Terran_Goliath, 5}, {bt::Terran_Vulture, 5}},
      40,
      8);

  // Random mirror match
  {
    int x = rand() % 8;
    int offset = (x == 0); // ensure non-zero
    int y = rand() % (8 - offset) + offset;
    symmetric(
        group.add("xzl+ydr_xzl+ydr"),
        {{bt::Protoss_Zealot, x}, {bt::Protoss_Dragoon, y}},
        40);
  }

  return group;
}

FixedScenarioGroup defilerScenarios() {
  FixedScenarioGroup group{"defiler"};
  asymmetric(
      group.add("6zg+1df_3dr"),
      {{bt::Zerg_Zergling, 6}, {bt::Zerg_Defiler, 1}},
      {{bt::Protoss_Dragoon, 3}});
  asymmetricRandom(
      group.add("6zg+1df_3dr_random"),
      {{bt::Zerg_Zergling, 6}, {bt::Zerg_Defiler, 1}},
      {{bt::Protoss_Dragoon, 3}});
  asymmetric(
      group.add("6zg+1df_8mr"),
      {{bt::Zerg_Zergling, 6}, {bt::Zerg_Defiler, 1}},
      {{bt::Terran_Marine, 8}});
  asymmetricRandom(
      group.add("6zg+1df_8mr_random"),
      {{bt::Zerg_Zergling, 6}, {bt::Zerg_Defiler, 1}},
      {{bt::Terran_Marine, 8}});

  return group;
}

FixedScenarioGroup defilerTankScenarios() {
  FixedScenarioGroup group{"defilerTank"};
  asymmetricRandom(
      group.add("6zg+1df_3tk+1mr_random"),
      {{bt::Zerg_Zergling, 6}, {bt::Zerg_Defiler, 1}},
      {{bt::Terran_Siege_Tank_Siege_Mode, 3}, {bt::Terran_Marine, 1}},
      50,
      60,
      40);
  asymmetric(
      group.add("6zg+1df_3tk+1mr"),
      {{bt::Zerg_Zergling, 6}, {bt::Zerg_Defiler, 1}},
      {{bt::Terran_Siege_Tank_Siege_Mode, 3}, {bt::Terran_Marine, 1}},
      60,
      5);
  return group;
}

std::vector<FixedScenarioGroup> allScenarioGroups() {
  std::vector<FixedScenarioGroup> scenarioGroups;
  scenarioGroups.emplace_back(baselineScenarios());
  scenarioGroups.emplace_back(simpleScenarios());
  scenarioGroups.emplace_back(symmetricSingleUnitScenarios());
  scenarioGroups.emplace_back(symmetricAirGroundScenarios());
  scenarioGroups.emplace_back(symmetricBigScenarios());
  scenarioGroups.emplace_back(regroupingScenarios());
  scenarioGroups.emplace_back(kitingScenarios());
  scenarioGroups.emplace_back(miscellaneousScenarios());
  scenarioGroups.emplace_back(defilerScenarios());
  scenarioGroups.emplace_back(defilerTankScenarios());
  return scenarioGroups;
}

std::vector<ScenarioInfo> allScenarios() {
  auto groups = allScenarioGroups();
  std::vector<ScenarioInfo> scenarios;
  for (auto& group : groups) {
    for (auto& scenario : group.scenarios) {
      scenarios.emplace_back(scenario);
    }
  }
  return scenarios;
}

ScenarioInfo getScenario(const std::string& scenarioName) {
  auto doRandomScenario = [&](FixedScenarioGroup&& group) {
    return group.scenarios[rand() % group.scenarios.size()];
  };

  // Predefined groups of similar scenarios
  if (scenarioName == "shuffleMirror") {
    return doRandomScenario(symmetricSingleUnitScenarios());
  } else if (scenarioName == "shuffleDefiler") {
    return doRandomScenario(defilerScenarios());
  } else if (scenarioName == "shuffleDefilerTank") {
    return doRandomScenario(defilerTankScenarios());
  } else if (scenarioName == "shuffleBig") {
    return doRandomScenario(symmetricBigScenarios());
  } else if (scenarioName == "shuffleAirGround") {
    return doRandomScenario(symmetricAirGroundScenarios());
  } else if (scenarioName == "shuffleRegroup") {
    return doRandomScenario(regroupingScenarios());
  } else if (scenarioName == "shuffleKiting") {
    return doRandomScenario(kitingScenarios());
  } else if (scenarioName == "jengaTrain" || scenarioName == "jengaTest") {
    // Experiment: Can we train on a random batch of single unit-type
    // scenarios and use that model to beat other single unit-type scenarios?

    // A reproducibly random way of selecting the test/training set for this
    // experiment..
    int seed = 7051992; // ...seeded with Flash's birthday
    std::mt19937 rng;
    rng.seed(seed);
    FixedScenarioGroup train = symmetricSingleUnitScenarios();
    FixedScenarioGroup test;
    while (test.scenarios.size() < 4) {
      auto index = rng() % train.scenarios.size();
      test.scenarios.emplace_back(train.scenarios[index]);
      train.scenarios.erase(train.scenarios.begin() + index);
    }
    if (scenarioName == "jengaTrain") {
      return doRandomScenario(std::move(train));
    } else {
      return doRandomScenario(std::move(test));
    }
  }
  // Use a specific named scenario
  auto scenarios = allScenarios();
  for (auto& scenario : scenarios) {
    if (scenario.name == scenarioName) {
      return scenario;
    }
  }
  throw std::runtime_error("No such scenario: " + scenarioName);
}
} // namespace

MicroFixedScenario::MicroFixedScenario(
    int maxFrame,
    SpawnList spawnPlayer1,
    SpawnList spawnPlayer2,
    std::string map,
    bool gui)
    : BaseMicroScenario(
          maxFrame,
          mapPathPrefix_.empty() ? map : mapPathPrefix_ + "/" + map,
          gui),
      spawnPlayer1_(std::move(spawnPlayer1)),
      spawnPlayer2_(std::move(spawnPlayer2)) {}

void MicroFixedScenario::setSpawns(
    SpawnList spawnPlayer1,
    SpawnList spawnPlayer2) {
  spawnPlayer1_ = std::move(spawnPlayer1);
  spawnPlayer2_ = std::move(spawnPlayer2);
}

MicroFixedScenario::MicroFixedScenario(
    int maxFrame,
    ScenarioInfo const& info,
    bool gui)
    : BaseMicroScenario(
          maxFrame,
          mapPathPrefix_.empty() ? info.map : mapPathPrefix_ + "/" + info.map,
          gui),
      spawnPlayer1_(std::move(info.allyList)),
      spawnPlayer2_(std::move(info.enemyList)),
      reward_(info.reward == nullptr ? combatReward() : info.reward()) {}

MicroFixedScenario::MicroFixedScenario(
    int maxFrame,
    std::string const& scenarioName,
    bool gui)
    : MicroFixedScenario(maxFrame, getScenario(scenarioName), gui) {}

void MicroFixedScenario::setSpawns(std::string const& scenarioName) {
  auto info = getScenario(scenarioName);
  // We have to reset the map, and we do this by deleting the players,
  // and relying on the BaseMicroScenario's spawnNextScenario behavior.
  if (map_ != info.map) {
    player1_ = nullptr;
    player2_ = nullptr;
  }
  spawnPlayer1_ = std::move(info.allyList);
  spawnPlayer2_ = std::move(info.enemyList);
  if (info.reward == nullptr) {
    reward_ = combatReward();
  } else {
    reward_ = info.reward();
  }
}

std::vector<std::string> MicroFixedScenario::listScenarios() {
  auto scenarios = allScenarios();
  std::vector<std::string> output;
  for (auto& scenario : scenarios) {
    output.emplace_back(scenario.name);
  }
  return output;
}

std::unique_ptr<Reward> MicroFixedScenario::getReward() {
  if (reward_ == nullptr) {
    throw std::runtime_error(
        "No suggest reward, did you already get the reward, or did not "
        "initialize the scenario from a predefined string?");
  }
  return std::move(reward_);
}

std::pair<std::vector<OnceModule::SpawnInfo>,
          std::vector<OnceModule::SpawnInfo>>
MicroFixedScenario::getSpawnInfo() {
  // spawn units of player1
  std::vector<OnceModule::SpawnInfo> ally_spawns;
  for (const auto& pair : spawnPlayer1_) {
    for (int i = 0; i < pair.second.count; i++) {
      ally_spawns.emplace_back(
          pair.first,
          pair.second.x,
          pair.second.y,
          pair.second.spreadX,
          pair.second.spreadY);
    }
  }

  // spawn units of player2
  std::vector<OnceModule::SpawnInfo> enemy_spawns;
  for (const auto& pair : spawnPlayer2_) {
    for (int i = 0; i < pair.second.count; i++) {
      enemy_spawns.emplace_back(
          pair.first,
          pair.second.x,
          pair.second.y,
          pair.second.spreadX,
          pair.second.spreadY);
    }
  }
  return {ally_spawns, enemy_spawns};
}

void MicroFixedScenario::setMapPathPrefix(std::string prefix) {
  mapPathPrefix_ = prefix;
}

} // namespace cherrypi
