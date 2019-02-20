/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "gameutils/scenario.h"
#include "test.h"

#include <glog/logging.h>

#include "buildorderfixed.h"
#include "common/rand.h"
#include "modules.h"
#include "player.h"
#include "utils.h"

using namespace cherrypi;

namespace {
typedef std::shared_ptr<cherrypi::test::BuildOrderFixedModule> SpBoModule;

class MoveZerglingsModule : public Module {
 public:
  MoveZerglingsModule() : Module() {}
  void step(State* state) override {
    auto board = state->board();

    int x = common::Rand::rand() % state->mapWidth();
    int y = common::Rand::rand() % state->mapHeight();
    // Post UPC for attacking enemy start location with all units
    auto units = utils::filterUnits(
        state->unitsInfo().myUnits(),
        [](Unit const* u) { return u->type == buildtypes::Zerg_Zergling; });
    for (auto u : units) {
      if (!u->moving()) {
        auto upc = utils::makeSharpUPC(u, {x, y}, Command::Move);
        board->postUPC(std::move(upc), 1, this);
      }
    }
  }
};
} // namespace

SCENARIO("unitsinfo/topspeed") {
  auto scenario = Scenario("test/maps/eco-base-zerg.scm", "Zerg");
  Player bot(scenario.makeClient());

  namespace bt = buildtypes;
  std::list<const BuildType*> buildOrder = {bt::Zerg_Drone,
                                            bt::Zerg_Drone,
                                            bt::Zerg_Drone,
                                            bt::Zerg_Extractor,
                                            bt::Zerg_Overlord,
                                            bt::Zerg_Drone,
                                            bt::Zerg_Drone,
                                            bt::Zerg_Drone,
                                            bt::Zerg_Drone,
                                            bt::Zerg_Drone,
                                            bt::Zerg_Drone,
                                            bt::Zerg_Overlord,
                                            bt::Zerg_Drone,
                                            bt::Zerg_Drone,
                                            bt::Zerg_Drone,
                                            bt::Zerg_Spawning_Pool,
                                            bt::Zerg_Zergling,
                                            bt::Metabolic_Boost};
  bot.addModule(Module::make<CreateGatherAttackModule>());
  SpBoModule buildOrderModule =
      std::make_shared<cherrypi::test::BuildOrderFixedModule>(
          lest_env, buildOrder);
  bot.addModule(buildOrderModule);
  bot.addModule(Module::make<BuildingPlacerModule>());
  bot.addModule(Module::make<BuilderModule>());
  bot.addModule(Module::make<GathererModule>());
  bot.addModule(Module::make<MoveZerglingsModule>());
  bot.addModule(Module::make<UPCToCommandModule>());

  bot.init();
  auto state = bot.state();
  constexpr int kMaxFrames = 12000;
  auto found = false;
  do {
    bot.step();
    if (state->currentFrame() > kMaxFrames) {
      break;
    }
    auto units = utils::filterUnits(
        state->unitsInfo().myUnits(),
        [](Unit const* u) { return u->type == buildtypes::Zerg_Zergling; });
    for (auto u : units) {
      EXPECT(u->topSpeed > 0);
      auto minTopSpeed =
          tc::BW::data::TopSpeed[u->type->unit] / tc::BW::XYPixelsPerWalktile;
      if (u->topSpeed > minTopSpeed) {
        found = true;
      }
    }
  } while (!state->gameEnded() && !found);
  VLOG(0) << "Done after " << state->currentFrame() << " frames";
  EXPECT(found);
}
