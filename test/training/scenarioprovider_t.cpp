/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "gameutils/microscenarioproviderfixed.h"
#include "microplayer.h"
#include "modules.h"

using namespace cherrypi;

namespace {

auto dummyPlayerSetup = [](BasePlayer* bot) {};

bool doesGameReset(std::vector<FixedScenario>& scenarios) {
  MicroScenarioProviderFixed provider;

  // 2 * 256/4 = 128 units per episode
  // So we should cross BWAPI's 10k unit limit within 100 episodes
  int maxFrameSeen = 0;
  bool haveReset = false;
  for (auto i = 0u; i < scenarios.size() && !haveReset; ++i) {
    provider.loadScenario(scenarios[i]);
    auto players =
        provider.startNewScenario(dummyPlayerSetup, dummyPlayerSetup);
    int frame = players.first->state()->currentFrame();
    if (i > 0 && frame <= maxFrameSeen) {
      return true;
    }
    maxFrameSeen = frame;
  }

  return false;
}

} // namespace

// Verify that we re-use the OpenBW game when possible
CASE("scenarioprovider/reuse_game__TSANUnsafe") {
  MicroScenarioProviderFixed provider;
  std::vector<FixedScenario> scenarios = {{}, {}, {}};
  EXPECT(doesGameReset(scenarios) == false);
}

// Verify that we reset the game when required due to BWAPI's 10,000 unit limit
CASE("scenarioprovider/reset_game_unit_limit__TSANUnsafe") {
  MicroScenarioProviderFixed provider;

  // We create 2 * 256 / 4 = 128 units per episode
  // So we should cross BWAPI's 10k unit limit within 78 episodes
  std::vector<FixedScenario> scenarios(80);
  for (int x = 0; x < 256; x += 4) {
    for (auto i = 0u; i < scenarios.size(); ++i) {
      // Add lots of Overlords to the scenarios, such that we can play the
      // scenario so many times that we hit 10,000 units
      scenarios[i].allies().push_back(
          {1, tc::BW::UnitType::Zerg_Overlord, x, 10});
      scenarios[i].enemies().push_back(
          {1, tc::BW::UnitType::Zerg_Overlord, x, 20});
    }
  }

  EXPECT(doesGameReset(scenarios) == true);
}

// Verify that we reset the game when changing maps
CASE("scenarioprovider/reset_game_map__TSANUnsafe") {
  MicroScenarioProviderFixed provider;
  std::vector<FixedScenario> scenarios = {{}, {}};
  scenarios[0].map = "test/maps/micro-empty-128.scm";
  scenarios[1].map = "test/maps/micro-empty2.scm";
  EXPECT(doesGameReset(scenarios) == true);
}

// Verify that we correctly load a scenario's upgrades and tech
CASE("scenarioprovider/fixed_micro_tech__TSANUnsafe") {
  MicroScenarioProviderFixed provider;
  FixedScenario scenarioTech;
  FixedScenario scenarioStoneAge;
  scenarioStoneAge.enemies() = {{1, tc::BW::UnitType::Zerg_Overlord, 5, 5}};
  scenarioStoneAge.allies() = {{1, tc::BW::UnitType::Zerg_Overlord, 5, 5}};
  scenarioTech.enemies() = {{1, tc::BW::UnitType::Zerg_Overlord, 5, 5}};
  scenarioTech.allies() = {{1, tc::BW::UnitType::Zerg_Overlord, 5, 5}};

  scenarioTech.addUpgrade(0, tc::BW::UpgradeType::Zerg_Melee_Attacks, 3);
  scenarioTech.addUpgrade(0, tc::BW::UpgradeType::Zerg_Missile_Attacks, 2);
  scenarioTech.addUpgrade(0, tc::BW::UpgradeType::Zerg_Carapace, 1);
  scenarioTech.addTech(0, tc::BW::TechType::Lurker_Aspect);
  scenarioTech.addUpgrade(1, tc::BW::UpgradeType::Protoss_Ground_Weapons, 3);
  scenarioTech.addUpgrade(1, tc::BW::UpgradeType::Protoss_Ground_Armor, 2);
  scenarioTech.addUpgrade(1, tc::BW::UpgradeType::Protoss_Plasma_Shields, 1);
  scenarioTech.addTech(1, tc::BW::TechType::Psionic_Storm);

  // Verify that we get upgrades and tech
  provider.loadScenario(scenarioTech);
  auto players = provider.startNewScenario(dummyPlayerSetup, dummyPlayerSetup);

  auto p0 = [&]() { return players.first->state(); };
  auto p1 = [&]() { return players.second->state(); };

  EXPECT(p0()->getUpgradeLevel(buildtypes::Zerg_Melee_Attacks_3) == 3);
  EXPECT(p0()->getUpgradeLevel(buildtypes::Zerg_Missile_Attacks_2) == 2);
  EXPECT(p0()->getUpgradeLevel(buildtypes::Zerg_Carapace_1) == 1);
  EXPECT(p0()->hasResearched(buildtypes::Lurker_Aspect) == true);
  EXPECT(p1()->getUpgradeLevel(buildtypes::Protoss_Ground_Weapons_3) == 3);
  EXPECT(p1()->getUpgradeLevel(buildtypes::Protoss_Ground_Armor_2) == 2);
  EXPECT(p1()->getUpgradeLevel(buildtypes::Protoss_Plasma_Shields_1) == 1);
  EXPECT(p1()->hasResearched(buildtypes::Psionic_Storm) == true);

  // Verify that we lose upgrades and tech
  provider.loadScenario(scenarioStoneAge);
  players = provider.startNewScenario(dummyPlayerSetup, dummyPlayerSetup);
  EXPECT(p0()->getUpgradeLevel(buildtypes::Zerg_Melee_Attacks_3) == 0);
  EXPECT(p0()->getUpgradeLevel(buildtypes::Zerg_Missile_Attacks_2) == 0);
  EXPECT(p0()->getUpgradeLevel(buildtypes::Zerg_Carapace_1) == 0);
  EXPECT(p0()->hasResearched(buildtypes::Lurker_Aspect) == false);
  EXPECT(p1()->getUpgradeLevel(buildtypes::Protoss_Ground_Weapons_3) == 0);
  EXPECT(p1()->getUpgradeLevel(buildtypes::Protoss_Ground_Armor_2) == 0);
  EXPECT(p1()->getUpgradeLevel(buildtypes::Protoss_Plasma_Shields_1) == 0);
  EXPECT(p1()->hasResearched(buildtypes::Psionic_Storm) == false);
}

// Verify that we correctly spawn a scenario's units
CASE("scenarioprovider/simple_fixed_micro__TSANUnsafe") {
  FixedScenario scenario;
  scenario.allies() = {{1, tc::BW::UnitType::Zerg_Mutalisk, 100, 140}};
  scenario.enemies() = {{2, tc::BW::UnitType::Zerg_Hydralisk, 115, 142}};
  auto provider = std::make_shared<MicroScenarioProviderFixed>(scenario);

  // Spawn scenarios multiple times and ensure that the state is correct each
  // time This implies correct cleanup of the previous scenario
  for (int i = 0; i < 10; ++i) {
    auto players =
        provider->startNewScenario(dummyPlayerSetup, dummyPlayerSetup);

    // Check that we have all the units that we wanted
    auto& ui1 = players.first->state()->unitsInfo();
    auto& ui2 = players.second->state()->unitsInfo();
    EXPECT(ui1.myUnits().size() == 1u);
    EXPECT(ui1.myUnitsOfType(buildtypes::Zerg_Mutalisk).size() == 1u);
    EXPECT(ui2.myUnits().size() == 2u);
    EXPECT(ui2.myUnitsOfType(buildtypes::Zerg_Hydralisk).size() == 2u);
  }

  scenario = {};
  scenario.allies() = {{3, tc::BW::UnitType::Protoss_Zealot, 100, 140},
                       {1, tc::BW::UnitType::Protoss_Dragoon, 100, 140}};
  scenario.enemies() = {{2, tc::BW::UnitType::Terran_Marine, 120, 140},
                        {3, tc::BW::UnitType::Terran_Medic, 120, 140}};

  for (int i = 0; i < 10; ++i) {
    provider->loadScenario(scenario);
    auto players =
        provider->startNewScenario(dummyPlayerSetup, dummyPlayerSetup);

    // Check that we have all the units that we wanted
    auto& ui1 = players.first->state()->unitsInfo();
    auto& ui2 = players.second->state()->unitsInfo();
    EXPECT(ui1.myUnits().size() == 4u);
    EXPECT(ui1.myUnitsOfType(buildtypes::Protoss_Zealot).size() == 3u);
    EXPECT(ui1.myUnitsOfType(buildtypes::Protoss_Dragoon).size() == 1u);
    EXPECT(ui2.myUnits().size() == 5u);
    EXPECT(ui2.myUnitsOfType(buildtypes::Terran_Marine).size() == 2u);
    EXPECT(ui2.myUnitsOfType(buildtypes::Terran_Medic).size() == 3u);

    provider->loadScenario(FixedScenario());
    auto newPlayers =
        provider->startNewScenario(dummyPlayerSetup, dummyPlayerSetup);
    EXPECT(ui1.myUnits().size() == 0u);
    EXPECT(ui2.myUnits().size() == 0u);
    EXPECT(newPlayers.first->state()->unitsInfo().myUnits().size() == 0u);
    EXPECT(newPlayers.first->state()->unitsInfo().myUnits().size() == 0u);
  }
}