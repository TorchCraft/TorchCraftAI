/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "microplayer.h"
#include "modules.h"
#include "scenarioprovider.h"

using namespace cherrypi;

CASE("scenarioprovider/simple_fixed_micro") {
  auto provider = std::make_shared<MicroFixedScenario>(
      200000,
      SpawnList({{tc::BW::UnitType::Zerg_Mutalisk, {1, 100, 140}}}),
      SpawnList({{tc::BW::UnitType::Zerg_Hydralisk, {2, 115, 142}}}),
      "test/maps/micro-empty2.scm",
      false);

  // not needed, but shouldn't crash
  provider->cleanScenario();

  // a dummy setup that adds no useful modules
  auto setup = [](BasePlayer* bot) {
    bot->addModule(Module::make<TopModule>());
    bot->addModule(Module::make<UPCToCommandModule>());
    bot->setRealtimeFactor(-1);
  };

  for (int i = 0; i < 10; i++) {
    auto players = provider->spawnNextScenario(setup, setup);

    // Check that we have all the units that we wanted
    auto& ui1 = players.first->state()->unitsInfo();
    auto& ui2 = players.second->state()->unitsInfo();
    EXPECT(ui1.myUnits().size() == 1u);
    EXPECT(ui1.myUnitsOfType(buildtypes::Zerg_Mutalisk).size() == 1u);
    EXPECT(ui2.myUnits().size() == 2u);
    EXPECT(ui2.myUnitsOfType(buildtypes::Zerg_Hydralisk).size() == 2u);

    // some dummy steps
    for (int j = 0; j < 20; j++) {
      players.first->step();
      players.second->step();
    }

    provider->cleanScenario();
    EXPECT(ui1.myUnits().size() == 0u);
    EXPECT(ui2.myUnits().size() == 0u);
  }
  // ask for a different scenario
  provider->setSpawns(
      SpawnList(
          {{tc::BW::UnitType::Protoss_Zealot, {3, 100, 140}},
           {tc::BW::UnitType::Protoss_Dragoon, {1, 100, 140}}}),
      SpawnList(
          {{tc::BW::UnitType::Terran_Marine, {2, 120, 140}},
           {tc::BW::UnitType::Terran_Medic, {3, 120, 140}}}));

  for (int i = 0; i < 10; i++) {
    auto players = provider->spawnNextScenario(setup, setup);

    // Check that we have all the units that we wanted
    auto& ui1 = players.first->state()->unitsInfo();
    auto& ui2 = players.second->state()->unitsInfo();
    EXPECT(ui1.myUnits().size() == 4u);
    EXPECT(ui1.myUnitsOfType(buildtypes::Protoss_Zealot).size() == 3u);
    EXPECT(ui1.myUnitsOfType(buildtypes::Protoss_Dragoon).size() == 1u);
    EXPECT(ui2.myUnits().size() == 5u);
    EXPECT(ui2.myUnitsOfType(buildtypes::Terran_Marine).size() == 2u);
    EXPECT(ui2.myUnitsOfType(buildtypes::Terran_Medic).size() == 3u);

    // some dummy steps
    for (int j = 0; j < 20; j++) {
      players.first->step();
      players.second->step();
    }

    provider->cleanScenario();
    EXPECT(ui1.myUnits().size() == 0u);
    EXPECT(ui2.myUnits().size() == 0u);
  }
}
