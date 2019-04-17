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

namespace {

class MockTacticsModule : public Module {
 public:
  MockTacticsModule() : Module() {}
  void step(State* state) override {
    auto board = state->board();
    if (board->hasKey("target_posted") && board->get<bool>("target_posted")) {
      return;
    }

    // Post UPC for attacking enemy start location with all units
    auto loc = state->tcstate()->start_locations[1 - state->playerId()];
    auto units = utils::filterUnits(
        state->unitsInfo().myUnits(),
        [](Unit const* u) { return u->active() && !u->type->isBuilding; });
    if (units.size() == 0) {
      return;
    }

    postUpc(state, 1, units, loc.x, loc.y);
    board->post("target_posted", true);
  }

  void postUpc(
      State* state,
      int srcUpcId,
      std::vector<Unit*> const& units,
      int targetX,
      int targetY) {
    auto upc = std::make_shared<UPCTuple>();
    for (Unit* u : units) {
      upc->unit[u] = 1.0f / units.size();
    }
    upc->position = Position(targetX, targetY);
    upc->command[Command::Delete] = 0.9;
    upc->command[Command::Move] = 0.1;

    state->board()->postUPC(std::move(upc), srcUpcId, this);
  }
};

/*
 * checks the overall workflow of cancelation
 */
class MockCombatModule : public CombatModule {
 public:
  MockCombatModule() : CombatModule() {}
  void step(State* state) override {
    CombatModule::step(state);
    if (deletedTasks_) {
      state->board()->post(
          "tasks properly deleted",
          state->board()->tasksOfModule(this).empty());
    }
    for (auto& task : state->board()->tasksOfModule(this)) {
      if (task->status() == TaskStatus::Unknown) {
        continue;
      }
      if (!cancelledTasks_) {
        VLOG(1) << "canceling combat tasks";
        task->cancel(state);
        cancelledTasks_ = true;
      } else {
        if (task->status() != TaskStatus::Cancelled) {
          LOG(ERROR) << "incorrect status for task " << task->upcId()
                     << ", expected " << (int)TaskStatus::Cancelled << ", got "
                     << (int)task->status();
          state->board()->post("task properly cancelled", false);
          cancelledTasks_ = false;
        } else {
          VLOG(1) << "task cancelled";
          state->board()->post("task properly cancelled", true);
          deletedTasks_ = true;
        }
      }
    }
  }

 private:
  bool cancelledTasks_ = false;
  bool deletedTasks_ = false;
};

} // namespace

SCENARIO("task_cancel/12_marines_vs_base") {
  auto scenario =
      GameSinglePlayerUMS("test/maps/12-marines-vs-base.scm", "Terran");
  Player bot(scenario.makeClient());

  bot.addModule(Module::make<TopModule>());
  bot.addModule(Module::make<MockTacticsModule>());
  auto combat = Module::make<MockCombatModule>();
  bot.addModule(combat);
  auto combatMicro = Module::make<CombatMicroModule>();
  bot.addModule(combatMicro);
  bot.addModule(Module::make<UPCToCommandModule>());

  bot.init();
  auto state = bot.state();
  do {
    bot.step();
  } while (!state->gameEnded() && bot.steps() <= 2000);

  EXPECT(state->unitsInfo().myUnits().empty() == false);
  EXPECT(state->board()->hasKey("task properly cancelled"));
  EXPECT(state->board()->get<bool>("task properly cancelled"));
  EXPECT(state->board()->hasKey("tasks properly deleted"));
  EXPECT(state->board()->get<bool>("tasks properly deleted"));
  for (auto& u : state->unitsInfo().myUnits()) {
    EXPECT(u->idle());
  }
}
