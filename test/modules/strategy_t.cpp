/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "fivepool.h"
#include "gameutils/game.h"
#include "modules.h"
#include "player.h"
#include "test.h"
#include <glog/logging.h>

DECLARE_string(build);
DECLARE_double(rtfactor);
using namespace cherrypi;
using namespace autobuild;

auto createMyPlayer(GameMultiPlayer* scenario) {
  auto bot = std::make_shared<Player>(scenario->makeClient1());
  bot->setRealtimeFactor(FLAGS_rtfactor);
  bot->addModule(Module::make<CreateGatherAttackModule>());
  bot->addModule(Module::make<StrategyModule>());
  bot->addModule(Module::make<GenericAutoBuildModule>());
  bot->addModule(Module::make<BuildingPlacerModule>());
  bot->addModule(Module::make<BuilderModule>());
  bot->addModule(Module::make<GathererModule>());
  bot->addModule(Module::make<UPCToCommandModule>());
  bot->init();
  return bot;
}

auto createEnemyPlayer(GameMultiPlayer* scenario, const std::string& race) {
  auto bot = std::make_shared<Player>(scenario->makeClient2());
  bot->setRealtimeFactor(FLAGS_rtfactor);
  bot->addModule(Module::make<CreateGatherAttackModule>());
  bot->addModule(Module::make<FivePoolModule>());
  bot->addModule(Module::make<GenericAutoBuildModule>());
  bot->addModule(Module::make<BuildingPlacerModule>());
  bot->addModule(Module::make<BuilderModule>());
  bot->addModule(Module::make<GathererModule>());
  bot->addModule(Module::make<UPCToCommandModule>());
  bot->init();
  return bot;
}

SCENARIO("strategy/5pool") {
  auto scenario = GameMultiPlayer(
      "maps/(4)Fighting Spirit.scx", tc::BW::Race::Zerg, tc::BW::Race::Zerg);

  FLAGS_build = "5pool";
  auto ourBot = createMyPlayer(&scenario);
  auto theirBot = createEnemyPlayer(&scenario, "Zerg");
  auto ourState = ourBot->state();

  int const kMaxFrames = 10000;
  do {
    ourBot->step();
    theirBot->step();
    if (ourState->currentFrame() > kMaxFrames) {
      break;
    }
    if (ourState->unitsInfo()
            .myCompletedUnitsOfType(buildtypes::Zerg_Zergling)
            .size() >= 6) {
      break;
    }
  } while (!ourState->gameEnded());
  VLOG(0) << "Done after " << ourState->currentFrame() << " frames";

  // Check that we have all the units that we wanted
  auto& ui = ourState->unitsInfo();
  EXPECT(
      ui.myCompletedUnitsOfType(buildtypes::Zerg_Zergling).size() >= size_t(6));
}

SCENARIO("strategy/2hatchhydras") {
  auto scenario = GameMultiPlayer(
      "maps/(4)Fighting Spirit.scx", tc::BW::Race::Zerg, tc::BW::Race::Protoss);
  FLAGS_build = "12hatchhydras";

  auto ourBot = createMyPlayer(&scenario);
  auto theirBot = createEnemyPlayer(&scenario, "Protoss");
  auto ourState = ourBot->state();

  int const kMaxFrames = 15000;
  do {
    ourBot->step();
    theirBot->step();
    if (ourState->currentFrame() > kMaxFrames) {
      break;
    }
    if (ourState->unitsInfo()
            .myCompletedUnitsOfType(buildtypes::Zerg_Hydralisk)
            .size() >= 6) {
      break;
    }
  } while (!ourState->gameEnded());
  VLOG(0) << "Done after " << ourState->currentFrame() << " frames";

  auto& ui = ourState->unitsInfo();
  ourBot->leave();
  theirBot->leave();
  for (int i = 0; i < 10; ++i) {
    ourBot->step();
    theirBot->step();
  }
  EXPECT(
      ui.myCompletedUnitsOfType(buildtypes::Zerg_Hydralisk).size() >=
      size_t(6));
}
