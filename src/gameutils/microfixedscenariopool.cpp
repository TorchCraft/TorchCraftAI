/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "initialconditions.h"
#include "state.h"
#include "utils.h"
#include "microfixedscenariopool.h"

namespace cherrypi {

/////////////////////////////
// Scenario-building tools //
/////////////////////////////
DEFINE_string(scenario_map, "", "Map to use for scenario");

using bt = torchcraft::BW::UnitType;

// TODO: Map sizes can VARY and these definitions are duplicated across
// rewards.cpp / microfixedscenariopool.cpp
constexpr int mapMidpointX = 128;
constexpr int mapMidpointY = 128;
const double kMapDiagonal =
    2 * sqrt(mapMidpointX * mapMidpointX + mapMidpointY * mapMidpointY);

int rangeOn(int minInclusive, int maxInclusive) {
  int delta = maxInclusive - minInclusive;
  if (delta <= 0)
    return minInclusive;
  return minInclusive + rand() % (maxInclusive - minInclusive);
};

struct UnitCount {
  bt unitType;
  int count;
  UnitCount(int unitTypeId, int count = rangeOn(2, 5))
      : unitType(bt::_from_integral(unitTypeId)), count(count) {}
};

ScenarioInfo& grouped(
    ScenarioInfo& scenario,
    const std::vector<UnitCount>& unitsAlly,
    const std::vector<UnitCount>& unitsEnemy,
    float unitSpread = 5,
    int teamSeparationFloor = 80,
    int groupsSeparationFloor = 40,
    int teamSeparationSpread = 20,
    int centerSpread = 10,
    int groupSpread = 10,
    bool teamRotate = true) {
  auto centerX = mapMidpointX + rangeOn(0, centerSpread);
  auto centerY = mapMidpointY + rangeOn(0, centerSpread);
  auto angle = rangeOn(0, 360);
  auto teamRadians = teamRotate ? 2.0 * M_PI * angle / 360 : 0.0;
  auto teamSin = teamRotate ? sin(teamRadians) : 0.0;
  auto teamCos = teamRotate ? cos(teamRadians) : 1.0;
  auto separation =
      (teamSeparationFloor + rangeOn(0, teamSeparationSpread))/2.0;
  auto groupSeparation = groupsSeparationFloor + rangeOn(0, groupSpread);
  auto separationX = separation;


  auto bottomY = -1.0*((unitsAlly.size()-1)/2.0)*groupSeparation;
  for (const UnitCount& unit : unitsAlly) {
    auto x = -1.0*separationX;
    auto y = bottomY;
    auto new_x = teamCos*x + teamSin*y;
    auto new_y = teamCos*y - x*teamSin;
    scenario.allyList.emplace_back(
        unit.count,
        unit.unitType,
        centerX + new_x,
        centerY + new_y,
        unitSpread,
        unitSpread);
    bottomY += groupSeparation;
  }
  
  bottomY = -1.0*((unitsEnemy.size()-1)/2.0)*groupSeparation;
  for (const UnitCount& unit : unitsEnemy) {
    auto x = 1.0*separationX;
    auto y = bottomY;
    auto new_x = teamCos*x + teamSin*y;
    auto new_y = teamCos*y - x*teamSin;
    scenario.enemyList.emplace_back(
        unit.count,
        unit.unitType,
        centerX + new_x,
        centerY + new_y,
        unitSpread,
        unitSpread);
    bottomY += groupSeparation;
  }
  return scenario;
};

ScenarioInfo& opposing(
    ScenarioInfo& scenario,
    const std::vector<UnitCount>& unitsAlly,
    const std::vector<UnitCount>& unitsEnemyL,
    const std::vector<UnitCount>& unitsEnemyR,
    float unitSpread = 5,
    int teamSeparationFloor = 40,
    
    int teamSeparationSpread = 0,
    int centerSpread = 0,
    bool teamRotate = false) {
  auto centerX = mapMidpointX + rangeOn(0, centerSpread);
  auto centerY = mapMidpointY + rangeOn(0, centerSpread);
  auto teamRadians = teamRotate ? 2.0 * M_PI * rangeOn(0, 360) / 360 : 0.0;
  auto teamSin = teamRotate ? sin(teamRadians) : 0.0;
  auto teamCos = teamRotate ? cos(teamRadians) : 1.0;
  auto separation =
      (teamSeparationFloor + rangeOn(0, teamSeparationSpread)) / 2;
  auto separationX = int(round(separation * teamCos));
  auto separationY = int(round(separation * teamSin));
  for (const UnitCount& unit : unitsAlly) {
    scenario.allyList.emplace_back(
        unit.count,
        unit.unitType,
        centerX,
        centerY,
        unitSpread,
        unitSpread);
  }
  auto left = (rand()%2 == 0) ? -1. : 1.;
  for (const UnitCount& unit : unitsEnemyL) {
    scenario.enemyList.emplace_back(
        unit.count,
        unit.unitType,
        centerX - (left*separationX),
        centerY - (left*separationY),
        unitSpread,
        unitSpread);
  }
  for (const UnitCount& unit : unitsEnemyR) {
    scenario.enemyList.emplace_back(
        unit.count,
        unit.unitType,
        centerX + (left*separationX),
        centerY + (left*separationY),
        unitSpread,
        unitSpread);
  }
  return scenario;
};

ScenarioInfo& checkerboard(
    ScenarioInfo& scenario,
    const UnitCount unitAlly,
    const UnitCount unitEnemy,
    int numTiles = 4,
    float unitSpread = 5,
    int groupSep = 60) {
  auto leftX = mapMidpointX - ((numTiles-1)/2.0)*groupSep;
  auto botY = mapMidpointY - ((numTiles-1)/2.0)*groupSep;
  
  std::set<int> enemyPos;
  int totalPos = numTiles*numTiles;
  while (enemyPos.size() < totalPos/2.0) {
    int rand_n = rand()%totalPos;
    enemyPos.insert(rand_n);
  }
  for (int i=0; i < totalPos; i++) {
    int x = i % numTiles;
    int y = i / numTiles;
    if (enemyPos.find(i) == enemyPos.end()) {
      // add enemy units
      scenario.allyList.emplace_back(
          unitAlly.count,
          unitAlly.unitType,
          leftX + (x*groupSep),
          botY + (y*groupSep),
          unitSpread,
          unitSpread);
    }
    else {
      // add our units
      scenario.enemyList.emplace_back(
          unitEnemy.count,
          unitEnemy.unitType,
          leftX + (x*groupSep),
          botY + (y*groupSep),
          unitSpread,
          unitSpread);
    }
  }
  return scenario;
};

ScenarioInfo& asymmetric(
    ScenarioInfo& scenario,
    const std::vector<UnitCount>& unitsAlly,
    const std::vector<UnitCount>& unitsEnemy,
    float unitSpread = 5,
    int teamSeparationFloor = 40,
    
    int teamSeparationSpread = 0,
    int centerSpread = 0,
    bool teamRotate = false,
    bool varyAngleBetween = false) {
  auto centerX = mapMidpointX + rangeOn(0, centerSpread);
  auto centerY = mapMidpointY + rangeOn(0, centerSpread);
  auto angle = rangeOn(0, 360);
  auto teamRadians = teamRotate ? 2.0 * M_PI * angle / 360 : 0.0;
  auto teamSin = teamRotate ? sin(teamRadians) : 0.0;
  auto teamCos = teamRotate ? cos(teamRadians) : 1.0;
  auto separation =
      (teamSeparationFloor + rangeOn(0, teamSeparationSpread)) / 2;
  auto separationX = int(round(separation * teamCos));
  auto separationY = int(round(separation * teamSin));

  for (const UnitCount& unit : unitsAlly) {
    scenario.allyList.emplace_back(
        unit.count,
        unit.unitType,
        centerX - separationX,
        centerY - separationY,
        unitSpread,
        unitSpread);
  }
  
  if (varyAngleBetween && teamRotate) {
    auto opposeAngle = rangeOn(-90, 90);
    teamRadians = 2.0 * M_PI * (angle + opposeAngle)/ 360;
    teamSin = sin(teamRadians);
    teamCos = cos(teamRadians);
    separationX = int(round(separation * teamCos));
    separationY = int(round(separation * teamSin));
  }
  
  for (const UnitCount& unit : unitsEnemy) {
    scenario.enemyList.emplace_back(
        unit.count,
        unit.unitType,
        centerX + separationX,
        centerY + separationY,
        unitSpread,
        unitSpread);
  }
  return scenario;
};

ScenarioInfo& symmetric(
    ScenarioInfo& scenario,
    const std::vector<UnitCount>& units) {
  return asymmetric(scenario, units, units);
};

//////////////////////////
// Scenario definitions //
//////////////////////////

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
  hugMiddleEasy.allyList.emplace_back(
      1, bt::Terran_Vulture, mapMidpointX, mapMidpointY, 15, 15);
  hugMiddleEasy.enemyList.emplace_back(
      1, bt::Zerg_Overlord, mapMidpointX, mapMidpointY, 0, 0);

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
  ignoreCivilians.allyList.emplace_back(
      1, bt::Zerg_Zergling, mapMidpointX, mapMidpointY, 0, 12);
  ignoreCivilians.enemyList.emplace_back(
      4, bt::Terran_Civilian, mapMidpointX, mapMidpointY, 0, 12);
  ignoreCivilians.enemyList.emplace_back(
      1, bt::Protoss_High_Templar, mapMidpointX, mapMidpointY, 0, 12);

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
  auto make = [&](std::string name,
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
      scenario.allyList.emplace_back(1, unit, (int)allyX, (int)allyY, 0, 0);
    }
    auto enemyR =
        (double)rand(); // This is likely not a good distribution of angles
    auto enemyX = mapMidpointX + enemyOffset * cos(enemyR);
    auto enemyY = mapMidpointY + enemyOffset * sin(enemyR);
    scenario.enemyList.emplace_back(
        unitCount, unit, (int)enemyX, (int)enemyY, 0, 0);
  };
  auto makeConga = [&](std::string name, bt unit) {
    group.add(name);
    auto& scenario = group.scenarios.back();
    int count = 12;
    int distance = 50;
    int unitSpreadX = 5;
    int unitSpreadY = 5;
    auto left = (rand()%2 == 0) ? -1 : 1;
    for (int i = 0; i < count; ++i) {
      int allyX = mapMidpointX - (left * unitSpreadX * i);
      int allyY = mapMidpointY;
      int enemyX = mapMidpointX + (left * distance);
      int enemyY =
          mapMidpointY + unitSpreadY * ((i + 1) / 2) * (i % 2 == 0 ? -1 : 1);
      scenario.allyList.emplace_back(1, unit, allyX, allyY, 0, 0);
      scenario.enemyList.emplace_back(1, unit, enemyX, enemyY, 0, 0);
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
  for (int i = 0; i < 5; ++i) {
    vulturesZealots.allyList.emplace_back(
        1, bt::Terran_Vulture, 140, 120 + 2 * i, 5.0, 5.0);
  }
  for (int i = 0; i < 10; ++i) {
    vulturesZealots.enemyList.emplace_back(
        1, bt::Protoss_Zealot, 120, 120 + 2 * i, 5.0, 5.0);
  }
  auto& vultureZealot = group.add("vu_zl");
  {
    const double pi = std::acos(-1);
    int vultures = 0, zealots = -1;
    while (vultures > zealots || zealots > 2 * vultures) {
      vultures = rand() % 4 + 1;
      zealots = rand() % 9 + 1;
    }
    double deg = (rand() % 360) * pi / 180.;
    auto center = 130;
    auto radius = 10;
    int ctrx = radius * std::cos(deg);
    int ctry = radius * std::sin(deg);
    vultureZealot.allyList.emplace_back(
        vultures, bt::Terran_Vulture, center + ctrx, center + ctry, 8, 8);
    vultureZealot.enemyList.emplace_back(
        zealots, bt::Protoss_Zealot, center - ctrx, center - ctry, 8, 8);
  }

  // Siege tanks and Zealots have the same speed, but Siege Tanks shoot and
  // accelerate instantly
  // So Siege Tanks, controlled correctly, get a ton of free shots on Zealots
  // before the Zealots close the gap
  asymmetric(
      group.add("1st_2zl"),
      {{bt::Terran_Siege_Tank_Tank_Mode, 2}},
      {{bt::Protoss_Zealot, 3}},
      10,
      60);
  asymmetric(
      group.add("3st_7zl"),
      {{bt::Terran_Siege_Tank_Tank_Mode, 3}},
      {{bt::Protoss_Zealot, 5}},
      10,
      60);

  // Marines and Zealots have same speed
  // So the closest Marine needs to bait the Zealot while the other shoots, then
  // alternate
  asymmetric(
      group.add("2mr_1zl"),
      {{bt::Terran_Marine, 2}},
      {{bt::Protoss_Zealot, 1}});
  auto& marinesZealots = group.add("6mr_4zl");
  for (int i = 0; i < 6; ++i) {
    marinesZealots.allyList.emplace_back(
        1, bt::Terran_Marine, 140, 120 + i * 2, 5.0, 5.0);
  }
  for (int i = 0; i < 4; ++i) {
    marinesZealots.enemyList.emplace_back(
        1, bt::Protoss_Zealot, 120, 130 + i * 2, 5.0, 5.0);
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
  for (int i = 0; i < 10; ++i) {
    mutalisk10Corsair5.allyList.emplace_back(
        1, bt::Zerg_Mutalisk, 140 + i * 2, 110, 5.0, 5.0);
  }
  for (int i = 0; i < 5; ++i) {
    mutalisk10Corsair5.enemyList.emplace_back(
        1, bt::Protoss_Corsair, 110, 120 + i * 3, 5.0, 5.0);
  }

  auto& mutalisk8Corsair5 = group.add("8mu_5co");
  for (int i = 0; i < 8; ++i) {
    mutalisk8Corsair5.allyList.emplace_back(
        1, bt::Zerg_Mutalisk, 140 + i * 2, 110, 5.0, 5.0);
  }
  for (int i = 0; i < 5; ++i) {
    mutalisk8Corsair5.enemyList.emplace_back(
        1, bt::Protoss_Corsair, 110, 120 + i * 3, 5.0, 5.0);
  }

  // Hydralisks vs. dragoons on high ground - need to go up ramp
  auto& hydralisksDragoonsRamp = group.add("3hy_2dr");
  hydralisksDragoonsRamp.allyList.emplace_back(
      3, bt::Zerg_Hydralisk, 145, 145, 3.0, 3.0);
  hydralisksDragoonsRamp.enemyList.emplace_back(
      2, bt::Protoss_Dragoon, 125, 128, 3.0, 3.0);
  hydralisksDragoonsRamp.map = "test/maps/micro/ramp_2wt.scx";

  // Hydralisks vs. siege-mode siege tank - need to get inside tank's min range
  auto hydralisksTank = group.add("2hy_1sst");
  hydralisksTank.allyList.emplace_back(
      2, bt::Zerg_Hydralisk, 140, 140, 5.0, 5.0);
  hydralisksTank.enemyList.emplace_back(
      1, bt::Terran_Siege_Tank_Siege_Mode, 125, 110, 5.0, 5.0);

  // Tank min range scenario - note siege-mode adds friendly fire
  auto& hydralisksTanks = group.add("4hy_2sst");
  hydralisksTanks.allyList.emplace_back(
      4, bt::Zerg_Hydralisk, 90, 120, 8.0, 8.0);
  hydralisksTanks.enemyList.emplace_back(
      1, bt::Terran_Siege_Tank_Siege_Mode, 155, 110, 5.0, 5.0);
  hydralisksTanks.enemyList.emplace_back(
      1, bt::Terran_Siege_Tank_Siege_Mode, 165, 140, 5.0, 5.0);

  // Zerglings must surround zealots instead of attacking asap
  auto zerglingsZealots = group.add("30zg_10zl");
  for (int i = 0; i < 30; ++i) {
    zerglingsZealots.allyList.emplace_back(
        1, bt::Zerg_Zergling, 130 + i, 130, 3.0, 3.0);
  }
  for (int i = 0; i < 10; ++i) {
    zerglingsZealots.enemyList.emplace_back(
        1, bt::Protoss_Zealot, 110, 120 + i * 2, 3.0, 3.0);
  }

  // Goliaths must stand ground and focus fire
  asymmetric(
      group.add("7zg_2gl"),
      {{bt::Terran_Goliath, 2}},
      {{bt::Zerg_Zergling, 7}},
      5,
      15);

  // Killing the Goliaths first ensures that the Mutalisks will win
  asymmetric(
      group.add("5mu+20zg_5gl+5vu"),
      {{bt::Zerg_Mutalisk, 5}, {bt::Zerg_Zergling, 20}},
      {{bt::Terran_Goliath, 5}, {bt::Terran_Vulture, 5}},
      8,
      40);

  // Random mirror match
  {
    int x = rand() % 8;
    int offset = (x == 0); // ensure non-zero
    int y = rand() % (8 - offset) + offset;
    symmetric(
        group.add("xzl+ydr_xzl+ydr"),
        {{bt::Protoss_Zealot, x}, {bt::Protoss_Dragoon, y}});
  }

  return group;
}

auto eachFrameRechargeMyEnergy = [](State* state) {
  for (auto& unit : state->unitsInfo().myUnits()) {
    // (Note that type->maxEnergy doesn't count the +50 energy cap granted by
    // upgrades
    if (unit->unit.energy < unit->type->maxEnergy) {
      state->board()->postCommand(
          torchcraft::Client::Command(
              torchcraft::BW::Command::CommandOpenbw,
              torchcraft::BW::OpenBWCommandType::SetUnitEnergy,
              unit->id,
              200),
          kRootUpcId);
    }
  }
};

FixedScenarioGroup defilerScenarios() {
  // Rotates/realigns the scenario
  auto defilerShuffle = [](ScenarioInfo& scenario,
                           const std::vector<UnitCount>& unitsAlly,
                           const std::vector<UnitCount>& unitsEnemy) {
    return asymmetric(scenario, unitsAlly, unitsEnemy, 5, 40, 20, 50, true);
  };

  FixedScenarioGroup group{"defiler"};
  asymmetric(
      group.add("6zg+1df_3dr"),
      {{bt::Zerg_Zergling, 6}, {bt::Zerg_Defiler, 1}},
      {{bt::Protoss_Dragoon, 3}});
  defilerShuffle(
      group.add("6zg+1df_3dr_random"),
      {{bt::Zerg_Zergling, 6}, {bt::Zerg_Defiler, 1}},
      {{bt::Protoss_Dragoon, 3}});
  asymmetric(
      group.add("6zg+1df_8mr"),
      {{bt::Zerg_Zergling, 6}, {bt::Zerg_Defiler, 1}},
      {{bt::Terran_Marine, 8}});
  defilerShuffle(
      group.add("6zg+1df_8mr_random"),
      {{bt::Zerg_Zergling, 6}, {bt::Zerg_Defiler, 1}},
      {{bt::Terran_Marine, 8}});
  for (auto& scenario : group.scenarios) {
    scenario.addTech(0, torchcraft::BW::TechType::Plague);
    scenario.addTech(0, torchcraft::BW::TechType::Consume);
    scenario.stepFunctions.push_back(eachFrameRechargeMyEnergy);
  }

  return group;
}

FixedScenarioGroup defilerTankScenarios() {
  FixedScenarioGroup group{"defilerTank"};

  for (int i = 4; i <= 8; ++i) {
    asymmetric(
        group.add(std::to_string(i) + "zg+2df_3tk_ptr"),
        {{bt::Zerg_Zergling, i}, {bt::Zerg_Defiler, 2}},
        {{bt::Terran_Siege_Tank_Siege_Mode, 3}},
        5,
        45);
    group.scenarios.back().reward = []() {
      return defilerProtectZerglingsReward();
    };
  }

  for (int i = 4; i <= 8; ++i) {
    asymmetric(
        group.add(std::to_string(i) + "zg+1df_3tk_ptr"),
        {{bt::Zerg_Zergling, i}, {bt::Zerg_Defiler, 1}},
        {{bt::Terran_Siege_Tank_Siege_Mode, 3}},
        5,
        45);
    group.scenarios.back().reward = []() {
      return defilerProtectZerglingsReward();
    };
  }

  for (int i = 4; i <= 8; ++i) {
    asymmetric(
        group.add(std::to_string(i) + "zg+1df_3tk_wr"),
        {{bt::Zerg_Zergling, i}, {bt::Zerg_Defiler, 1}},
        {{bt::Terran_Siege_Tank_Siege_Mode, 3}},
        5,
        45);
    group.scenarios.back().reward = []() { return defilerWinLossReward(); };
  }

  for (auto& scenario : group.scenarios) {
    scenario.addTech(0, torchcraft::BW::TechType::Plague);
    scenario.addTech(0, torchcraft::BW::TechType::Consume);
  }
  return group;
}

// We have a numbers advantage -- for debugging
FixedScenarioGroup outnumberScenarios() {
  FixedScenarioGroup group{"outnumber"};
  auto make = [&](std::string name, bt unit) {
    asymmetric(group.add("adv_" + name), {{unit, 15}}, {{unit, 10}}, 5, 50);
  };
  make("mr", bt::Terran_Marine);
  make("zg", bt::Zerg_Zergling);

  auto makeBig = [&](std::string name, bt unit) {
    asymmetric(
        group.add("adv_big_" + name), {{unit, 60}}, {{unit, 50}}, 5, 100);
  };
  makeBig("mr", bt::Terran_Marine);
  makeBig("zg", bt::Zerg_Zergling);
  return group;
}

ScenarioInfo customHeterogenousScenario(int base, bool varyStart) {
  int teamSeperationSpread = varyStart ? 20 : 0;
  int centerSpread = varyStart ? 40 : 0;
  int teamSeparation = varyStart ? 100 : 130;
  FixedScenarioGroup group{"heterogenous"};
  auto scenario = opposing(
      group.add(std::to_string(base) + "hyzg_dgzl"),
      {{bt::Zerg_Mutalisk, base/4}, {bt::Zerg_Zergling, base}},
      {{bt::Protoss_Dragoon, base/2}},
      {{bt::Protoss_Zealot, base/3}},
      5,
      teamSeparation,
      centerSpread,
      teamSeperationSpread,
      varyStart);
  scenario.addUpgrade(0, tc::BW::UpgradeType::Singularity_Charge);
  return group.scenarios.back();
}



// We have a custom advantage (or disadvantage) -- for GAS
ScenarioInfo customAdvantageScenario(const std::string& unit,
    const std::string& enemyUnit, int base, int advantage, bool varyStart,
    bool varyAngle,
    int separation) {
  FixedScenarioGroup group{"customOutnumber"};  
  auto make = [&](bt unit, bt enemy, int base, int advantage, bool varyStart, bool varyAngle,
      int separation = 0) {
    int teamSeperationSpread = varyStart ? 20 : 0;
    int centerSpread = varyStart ? 40 : 0;
    int teamSeparation = separation;
    if (separation == 0) {
      teamSeparation = varyStart ? 90 : 110;
    }
    asymmetric(
        group.add("adv"),
          {{unit, base}}, {{enemy, base+advantage}},
          5,
          teamSeparation,
          centerSpread,
          teamSeperationSpread,
          varyStart,
          varyAngle);
  };
  auto getUnit = [](const std::string& uName) {
    if (uName == "mr") return bt::Terran_Marine;
    if (uName == "zg") return bt::Zerg_Zergling;
    if (uName == "hy") return bt::Zerg_Hydralisk;
    if (uName == "zl") return bt::Protoss_Zealot;
    return bt::Terran_Marine;
  };
  auto u = getUnit(unit);
  auto enemy = getUnit(enemyUnit);
  make(u, enemy, base, advantage, varyStart, varyAngle, separation);
  auto scenario = group.scenarios.back();
  if (FLAGS_scenario_map != "") {
    scenario.changeMap(FLAGS_scenario_map);
  }
  return scenario;
}


// We have a numbers advantage -- for GAS
ScenarioInfo customGroupedScenario(const std::string& unit, int base, int additional, int advantage, bool varyStart) {
  FixedScenarioGroup group{"customGrouped"};
  auto make = [&](bt unit, int base, int additional, bool varyStart) {
    int teamSeperationSpread = varyStart ? 20 : 0;
    int centerSpread = varyStart ? 10 : 0;
    int groupSeperationSpread = varyStart ? 10 : 0;
    grouped(
        group.add("adv"),
        {{unit, base}, {unit, base+additional}}, {{unit, base + additional+advantage}, {unit, base}},
        5,
        50,
        50,
        teamSeperationSpread,
        centerSpread,
        groupSeperationSpread,
        varyStart
    );
  };
  auto u = unit == "mr" ?  bt::Terran_Marine : bt::Zerg_Zergling;
  make(u, base, additional, varyStart);
  return group.scenarios.back();
}

// We have a numbers advantage -- for GAS
ScenarioInfo customGroupedScenarioFar(const std::string& unit, int base, int additional, int advantage, bool varyStart) {
  FixedScenarioGroup group{"customGrouped"};
  auto make = [&](bt unit, int base, int additional, bool varyStart) {
    int teamSeperationSpread = varyStart ? 0 : 0;
    int centerSpread = varyStart ? 0 : 0;
    int groupSeperationSpread = varyStart ? 0 : 0;
    grouped(
        group.add("adv"),
        {{unit, base}, {unit, base+additional}}, {{unit, base + additional+advantage}, {unit, base}},
        20,
        30,
        30,
        teamSeperationSpread,
        centerSpread,
        groupSeperationSpread,
        varyStart
    );
  };
  auto u = unit == "mr" ?  bt::Terran_Marine : bt::Zerg_Zergling;
  make(u, base, additional, varyStart);
  return group.scenarios.back();
}


// We have a numbers advantage -- for debugging
FixedScenarioGroup tankScenarios() {
  FixedScenarioGroup group{"tank"};
  auto make = [&]() {
    int teamSeperationSpread = 40;
    int centerSpread = 20;
    int groupSeperationSpread = 40;
    grouped(
        group.add("zg_mut_tank_turret"),
        {{bt::Zerg_Zergling, 1}, {bt::Zerg_Mutalisk, 1}}, {{bt::Terran_Siege_Tank_Siege_Mode, 1}, {bt::Terran_Missile_Turret, 1}},
        5,
        65,
        65,
        teamSeperationSpread,
        centerSpread,
        groupSeperationSpread,
        true
    );
  };
  make();
  return group;
}

// We have a numbers advantage -- for debugging
ScenarioInfo customCheckerboardScenario(const std::string& unit, int num_units, int num_groups) {
  FixedScenarioGroup group{"checkerboard"};
  auto make = [&](bt unit, int num_units, int num_groups) {
    checkerboard(
        group.add("checkerboard_mr"),
        {bt::Terran_Marine, num_units},
        {bt::Terran_Marine, num_units},
        num_groups
      );
  };
  auto u = unit == "mr" ?  bt::Terran_Marine : bt::Zerg_Zergling;
  make(u, num_units, num_groups);
  return group.scenarios.back();
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
  scenarioGroups.emplace_back(outnumberScenarios());
  scenarioGroups.emplace_back(tankScenarios());
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
} // namespace cherrypi
