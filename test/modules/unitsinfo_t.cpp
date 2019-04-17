/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "gameutils/game.h"
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
  auto scenario = GameSinglePlayerUMS("test/maps/eco-base-zerg.scm", "Zerg");
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

static auto createPlayer = [](auto client, auto hack) {
  auto bot = std::make_shared<Player>(client);
  for (auto name : utils::stringSplit(kDefaultModules, ',')) {
    if (!name.empty()) {
      bot->addModule(Module::make(name));
    }
  }
  bot->addModule(Module::make(kAutoBottomModule));

  bot->setMapHack(hack);

  bot->init();
  return bot;
};

SCENARIO("unitsinfo/maphack") {
  auto scenario = GameMultiPlayer(
      "maps/(4)Fighting Spirit.scx", tc::BW::Race::Zerg, tc::BW::Race::Zerg);

  std::shared_ptr<Player> p1 = createPlayer(scenario.makeClient1(), true);
  std::shared_ptr<Player> p2 = createPlayer(scenario.makeClient2(), true);

  auto s1 = p1->state();
  auto s2 = p2->state();
  int const kMaxFrames = 5000;
  // We make sure the number of mistakes are low. Because the player
  // execute not in sync, we're never 100% sure if the mapHacked state
  // perfectly corresponds to the player state we haven't seen.
  // In practice, there's very few frames of these misalignments
  auto nMistakes = 0;
  do {
    p1->step();
    p2->step();

    auto uHash = [&](Unit* u) {
      uint64_t result = 0;
      result = (result << 12) + u->x;
      result = (result << 12) + u->y;
      result = (result << 12) + u->type->unit;
      result = (result << 12) + u->unit.health;
      return result;
    };

    std::map<int64_t, int32_t> s1u;
    std::map<int64_t, int32_t> s2u;
    for (auto& u : s1->unitsInfo().myUnits()) {
      s1u[uHash(u)]++;
    }
    for (auto& u : s2->unitsInfo().myUnits()) {
      s2u[uHash(u)]++;
    }

    auto checkmhu = [&](auto& mhu) {
      std::map<int64_t, int32_t> p1mhu;
      std::map<int64_t, int32_t> p2mhu;
      for (auto& u : mhu) {
        if (u->playerId == s1->playerId()) {
          p1mhu[uHash(u)]++;
        }
        if (u->playerId == s2->playerId()) {
          p2mhu[uHash(u)]++;
        }
      }

      if (p1mhu != s1u) {
        nMistakes++;
      }
      if (p2mhu != s2u) {
        nMistakes++;
      }
    };

    auto s1mhu = s1->unitsInfo().mapHacked();
    auto s2mhu = s2->unitsInfo().mapHacked();
    checkmhu(s1mhu);
    checkmhu(s2mhu);

    if ((s1->currentFrame() > kMaxFrames) ||
        (s2->currentFrame() > kMaxFrames)) {
      break;
    }
    EXPECT(nMistakes < 10);
  } while (!(s1->gameEnded() || s2->gameEnded()));
}

SCENARIO("unitsinfo/throw_on_nomaphack") {
  auto scenario = GameMultiPlayer(
      "maps/(4)Fighting Spirit.scx", tc::BW::Race::Zerg, tc::BW::Race::Zerg);

  std::shared_ptr<Player> p1 = createPlayer(scenario.makeClient1(), false);
  std::shared_ptr<Player> p2 = createPlayer(scenario.makeClient2(), false);

  auto s1 = p1->state();
  auto s2 = p2->state();
  p1->step();
  p2->step();
  EXPECT_THROWS(s1->unitsInfo().mapHacked());
  EXPECT_THROWS(s2->unitsInfo().mapHacked());
}
