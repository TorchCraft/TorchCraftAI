/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "gameutils/scenario.h"
#include "test.h"

#include <glog/logging.h>

#include "fivepool.h"
#include "modules.h"
#include "player.h"

using namespace cherrypi;

namespace {
class MockFivePoolModule : public FivePoolModule {
 public:
  MockFivePoolModule() : FivePoolModule() {
    // Don't build lots of Zerglings at the end
    builds_.emplace_back(buildtypes::Zerg_Drone);
    builds_.emplace_back(buildtypes::Zerg_Spawning_Pool);
    builds_.emplace_back(buildtypes::Zerg_Drone);
    builds_.emplace_back(buildtypes::Zerg_Drone);
    builds_.emplace_back(buildtypes::Zerg_Zergling);
    builds_.emplace_back(buildtypes::Zerg_Zergling);
    builds_.emplace_back(buildtypes::Zerg_Zergling);
  }
};

void fix_start_locations(tc::State* state) {
  // This is super hacky but UMS maps in openbw have weird start locations
  // because they won't put in 4 players (I think)
  state->start_locations.clear();
  state->start_locations.emplace_back(468, 28);
  state->start_locations.emplace_back(468, 468);
  state->start_locations.emplace_back(28, 24);
  state->start_locations.emplace_back(28, 468);
}

void addScoutingModules(Player& bot) {
  bot.addModule(Module::make<GathererModule>());
  bot.addModule(Module::make<CreateGatherAttackModule>());
  bot.addModule(Module::make<StrategyModule>(StrategyModule::Duty::Scouting));
  bot.addModule(Module::make<MockFivePoolModule>());
  bot.addModule(Module::make<BuildingPlacerModule>());
  bot.addModule(Module::make<BuilderModule>());
  bot.addModule(Module::make<TacticsModule>());
  bot.addModule(Module::make<CombatModule>());
  bot.addModule(Module::make<CombatMicroModule>());
  bot.addModule(Module::make<ScoutingModule>());
  bot.addModule(Module::make<UPCToCommandModule>());
}
} // namespace

SCENARIO("scouting/search_and_destroy") {
  auto scenario =
      MeleeScenario("test/maps/fighting_spirit_fow.scm", "Zerg", "Protoss");
  Player bot(scenario.makeClient());

  // Scenario setup: we should be able to find an unseen building
  using namespace tc::BW;
  bot.addModule(Module::make<TopModule>());
  bot.addModule(OnceModule::makeWithEnemySpawns(
      {
          {1, UnitType::Protoss_Probe, 50, 290},
          {1, UnitType::Protoss_Pylon, 40, 290},
      },
      "EnemySpawns"));
  addScoutingModules(bot);

  bot.init();
  auto state = bot.state();
  int const kMaxFrames = 13000;

  do {
    bot.step();
    // TODO: Check that we scouted one nexus + one pylon and also destroyed
    // one nexus and one pylon.
  } while (!state->gameEnded() && state->currentFrame() < kMaxFrames);

  EXPECT(state->unitsInfo().myUnits().empty() == false);
  EXPECT(state->unitsInfo().enemyUnits().empty() == true);
}

// TODO: Sometimes the Zerglings won't be able to kill the marines in time and
// the test will fail.
SCENARIO("scouting/blocked_ramp_above[.dev]") {
  auto scenario = Scenario("test/maps/fighting_spirit_fow_static.scm", "Zerg");
  Player bot(scenario.makeClient());

  // Scenario setup: we should be able to find the enemy if they blocked their
  // ramp.
  // TODO: The drone will always try to go up the ramp, sometimes you won't
  // see any enemy units at all, the solution is to order the drone to move to
  // the ramp first before going into the base.
  using namespace tc::BW;
  bot.addModule(Module::make<TopModule>());
  bot.addModule(OnceModule::makeWithEnemySpawns(
      {
          {1, UnitType::Terran_Medic, 45, 117},
          {1, UnitType::Terran_Medic, 38, 120},
          {1, UnitType::Terran_Medic, 41, 118},
      },
      "EnemySpawns"));

  addScoutingModules(bot);

  bot.init();
  auto state = bot.state();
  fix_start_locations(state->tcstate());
  int const kMaxFrames = 5000;

  do {
    bot.step();
  } while (!state->gameEnded() && state->currentFrame() < kMaxFrames);

  EXPECT(state->areaInfo().foundEnemyStartLocation());
}

SCENARIO("scouting/blocked_ramp_below") {
  auto scenario = Scenario("test/maps/fighting_spirit_fow_static.scm", "Zerg");
  Player bot(scenario.makeClient());

  // Scenario setup: we should be able to find the enemy if they blocked their
  // ramp.
  using namespace tc::BW;
  bot.addModule(Module::make<TopModule>());
  bot.addModule(OnceModule::makeWithEnemySpawns(
      {
          {1, UnitType::Terran_Medic, 48, 127},
          {1, UnitType::Terran_Medic, 51, 125},
      },
      "EnemySpawns"));

  addScoutingModules(bot);

  bot.init();
  auto state = bot.state();
  fix_start_locations(state->tcstate());
  int const kMaxFrames = 5000;

  do {
    bot.step();
  } while (!state->gameEnded() && state->currentFrame() < kMaxFrames);

  EXPECT(state->areaInfo().foundEnemyStartLocation());
}

SCENARIO("scouting/second_base") {
  auto scenario = Scenario("test/maps/fighting_spirit_fow_static.scm", "Zerg");
  Player bot(scenario.makeClient());

  // Scenario setup: we should be able to infer the base location from
  // the second base
  using namespace tc::BW;
  bot.addModule(Module::make<TopModule>());
  bot.addModule(OnceModule::makeWithEnemySpawns(
      {
          {1, UnitType::Protoss_Probe, 65, 145},
          {1, UnitType::Protoss_Nexus, 65, 160},
      },
      "EnemySpawns"));

  addScoutingModules(bot);

  bot.init();
  auto state = bot.state();
  fix_start_locations(state->tcstate());
  int const kMaxFrames = 5000;

  do {
    bot.step();
    if (state->areaInfo().foundEnemyStartLocation()) {
      // TODO Fail if scout is already in enemy main base
      break;
    }
  } while (!state->gameEnded() && state->currentFrame() < kMaxFrames);

  // TODO Check that this is the right starting location, I think (28, 24)
  EXPECT(state->areaInfo().foundEnemyStartLocation());
}

SCENARIO("scouting/blocked_natural") {
  auto scenario = Scenario("test/maps/fighting_spirit_fow_static.scm", "Zerg");
  Player bot(scenario.makeClient());

  // Scenario setup: we should be able to find an unseen building
  using namespace tc::BW;
  bot.addModule(Module::make<TopModule>());
  bot.addModule(OnceModule::makeWithEnemySpawns(
      {
          {1, UnitType::Terran_Medic, 90, 138},
          {1, UnitType::Terran_Medic, 90, 140},
          {1, UnitType::Terran_Medic, 90, 143},
          {1, UnitType::Terran_Medic, 90, 146},
          {1, UnitType::Terran_Medic, 90, 149},
          {1, UnitType::Terran_Medic, 90, 152},
          {1, UnitType::Terran_Medic, 90, 155},
          {1, UnitType::Terran_Medic, 90, 158},
          {1, UnitType::Terran_Medic, 90, 161},
          {1, UnitType::Terran_Medic, 90, 164},
          {1, UnitType::Terran_Medic, 90, 167},
      },
      "EnemySpawns"));

  addScoutingModules(bot);

  bot.init();
  auto state = bot.state();
  fix_start_locations(state->tcstate());
  int const kMaxFrames = 5000;

  do {
    bot.step();
  } while (!state->gameEnded() && state->currentFrame() < kMaxFrames);

  EXPECT(state->areaInfo().foundEnemyStartLocation());
}
