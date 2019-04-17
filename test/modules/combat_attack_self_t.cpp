/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <cstdlib>
#include <fstream>

#include "gameutils/game.h"
#include "test.h"

#include <glog/logging.h>

#include "cherrypi.h"
#include "modules.h"
#include "player.h"
#include "utils.h"

using namespace cherrypi;
DECLARE_double(rtfactor);

namespace {

class MockTacticsModule : public Module {
 public:
  MockTacticsModule() : Module() {}
  void step(State* state) override {
    auto board = state->board();
    if (state->currentFrame() > 10 &&
        state->unitsInfo().myUnits().size() == 0) {
      state->board()->postCommand(
          tc::Client::Command(tc::BW::Command::Quit), 0);
      return;
    }
    if (board->hasKey("target_posted") && board->get<bool>("target_posted")) {
      return;
    }

    auto units = utils::filterUnits(
        state->unitsInfo().myUnits(),
        [](Unit const* u) { return u->active() && !u->type->isBuilding; });
    if (units.size() == 0) {
      return;
    }

    UPCTuple::UnitMap map;
    for (Unit* e : state->unitsInfo().enemyUnits()) {
      map[e] = 1;
    }

    postUpc(state, 1, units, map);
    board->post("target_posted", true);
  }

  void postUpc(
      State* state,
      int srcUpcId,
      std::vector<Unit*> const& units,
      UPCTuple::UnitMap targets) {
    auto upc = std::make_shared<UPCTuple>();
    for (Unit* u : units) {
      upc->unit[u] = 1.0f / units.size();
    }
    upc->position = targets;
    upc->command[Command::Delete] = 0.5;
    upc->command[Command::Move] = 0.5;

    state->board()->postUPC(std::move(upc), srcUpcId, this);
  }
};

void run_game(Player& bot1, Player& bot2, int const kMaxFrames) {
  auto state1 = bot1.state();
  auto state2 = bot2.state();
  do {
    bot1.step();
    bot2.step();
    if ((state1->currentFrame() > kMaxFrames) ||
        (state2->currentFrame() > kMaxFrames) || (state1->gameEnded()) ||
        (state2->gameEnded())) {
      break;
    }
  } while (!(state1->gameEnded() || state2->gameEnded()));
}

void microScenario(
    lest::env& lest_env,
    std::string map,
    void moduleFunc(Player&),
    int kMaxFrames = 100000,
    int prevMyAvg = -1,
    int prevTheirAvg = -1) {
  auto scenario = GameMultiPlayer(
      map, tc::BW::Race::Zerg, tc::BW::Race::Zerg, GameType::UseMapSettings);
  Player bot1(scenario.makeClient1());
  Player bot2(scenario.makeClient2());
  bot1.setRealtimeFactor(FLAGS_rtfactor);
  bot2.setRealtimeFactor(FLAGS_rtfactor);
  moduleFunc(bot1);

  bot1.addModule(Module::make<TopModule>());
  bot1.addModule(Module::make<MockTacticsModule>());
  bot1.addModule(Module::make<SquadCombatModule>());
  bot1.addModule(Module::make<UPCToCommandModule>());

  bot2.addModule(Module::make<TopModule>());
  bot2.addModule(Module::make<MockTacticsModule>());
  bot2.addModule(Module::make<SquadCombatModule>());
  bot2.addModule(Module::make<UPCToCommandModule>());

  bot1.init();
  bot2.init();
  auto state = bot1.state();

  run_game(bot1, bot2, 6000);

  // I'm using stderr here because VLOG(0) is skipped if the test fails...
  if (prevMyAvg >= 0 && prevTheirAvg >= 0) {
    std::cerr << lest_env.testing << " >> "
              << "My/Their units left: " << state->unitsInfo().myUnits().size()
              << "/" << state->unitsInfo().enemyUnits().size()
              << ", should be approx " << prevMyAvg << "/" << prevTheirAvg
              << "\n";
  } else {
    std::cerr << lest_env.testing << " >> "
              << "My/Their units left: " << state->unitsInfo().myUnits().size()
              << "/" << state->unitsInfo().enemyUnits().size() << "\n";
  }
  EXPECT(state->currentFrame() <= kMaxFrames);
}

} // namespace

// Scenario setup: we should be able to beat the built-in AI with the
// attack-weakest heuristics
SCENARIO("self_play_UMS") {
  microScenario(
      lest_env,
      "test/maps/micro-big.scm",
      [](Player& bot) {
        using namespace tc::BW;
        bot.addModule(OnceModule::makeWithSpawns(
            {
                {5, UnitType::Zerg_Mutalisk, 504, 532},
            },
            "MySpawns"));
        bot.addModule(OnceModule::makeWithEnemySpawns(
            {
                {5, UnitType::Zerg_Mutalisk, 536, 532},
            },
            "EnemySpawns"));
      },
      5000);
}
