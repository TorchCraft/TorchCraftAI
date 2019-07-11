/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "gameutils/game.h"
#include "gameutils/microscenarioproviderfixed.h"
#include "microplayer.h"
#include "player.h"

using namespace cherrypi;

namespace {

constexpr auto kMapWithUnitsForBothPlayers = "test/maps/micro-empty2.scm";
constexpr auto kMapWithUnitsForPlayer2Only =
    "test/maps/micro-empty-64-1fog-2revealed.scm";
constexpr auto kMapWithUnitsForNobody = "test/maps/micro-empty-64-fog.scm";

auto makeSinglePlayerGame = [](const std::string& map) {
  return std::make_unique<GameSinglePlayer>(
      cherrypi::GameOptions(map).gameType(GameType::UseMapSettings),
      GamePlayerOptions(tc::BW::Race::Terran),
      GamePlayerOptions(tc::BW::Race::Protoss));
};

auto makeSinglePlayerBot =
    [](const std::shared_ptr<torchcraft::Client>& client) {
      auto bot = std::make_unique<Player>(client);
      bot->setWarnIfSlow(false);
      bot->init();
      bot->step();
      return bot;
    };

auto makeScenarioProvider = [](const std::string& map) {
  FixedScenario scenario;
  scenario.map = map;

  MicroScenarioProviderFixed provider;
  provider.loadScenario(scenario);
  return provider;
};

auto dummyPlayerSetup = [](BasePlayer*) {};

auto sawEnemy = [](std::shared_ptr<BasePlayer>& player) {
  return player->state()->board()->hasKey(Blackboard::kEnemyRaceKey);
};

auto checkFirstOpponentStrictly = [](std::shared_ptr<BasePlayer>& player) {
  return player->state()->firstOpponent(true);
};

CASE("state/firstopponent_singleplayer_unitsboth") {
  // If both players have units, we should find an opponent using strict
  // criteria.
  auto game = makeSinglePlayerGame(kMapWithUnitsForBothPlayers);
  auto bot = makeSinglePlayerBot(game->makeClient());
  EXPECT_NO_THROW(bot->state()->firstOpponent(false));
  EXPECT_NO_THROW(bot->state()->firstOpponent(true));
}

CASE("state/firstopponent_singleplayer_units2only") {
  // If either player lacks units, we should fail to find an opponent using
  // strict criteria.
  auto game = makeSinglePlayerGame(kMapWithUnitsForPlayer2Only);
  auto bot = makeSinglePlayerBot(game->makeClient());
  EXPECT_NO_THROW(bot->state()->firstOpponent(false));
  EXPECT_THROWS(bot->state()->firstOpponent(true));
}

CASE("state/firstopponent_singleplayer_unitsnone") {
  // If neither player has units, we should fail to find an opponent using
  // strict criteria.
  auto game = makeSinglePlayerGame(kMapWithUnitsForNobody);
  auto bot = makeSinglePlayerBot(game->makeClient());
  EXPECT_NO_THROW(bot->state()->firstOpponent(false));
  EXPECT_THROWS(bot->state()->firstOpponent(true));
}

CASE("state/firstopponent_multiplayer_unitsboth") {
  // sawEnemy() should always be true for MicroPlayers, who apply non-strict
  // criteria for identifying enemies. If both players have units, firstOpponent
  // should succeed with strict criteria.
  auto provider = makeScenarioProvider(kMapWithUnitsForBothPlayers);
  auto [p0, p1] = provider.startNewScenario(dummyPlayerSetup, dummyPlayerSetup);
  EXPECT(sawEnemy(p0) == true);
  EXPECT(sawEnemy(p1) == true);
  EXPECT_NO_THROW(checkFirstOpponentStrictly(p0));
  EXPECT_NO_THROW(checkFirstOpponentStrictly(p1));
}

CASE("state/firstopponent_multiplayer_units2only") {
  // sawEnemy() should always be true for MicroPlayers, who apply non-strict
  // criteria for identifying enemies. If only the second player has units,
  // firstOpponent should throw for each player.
  auto provider = makeScenarioProvider(kMapWithUnitsForPlayer2Only);
  auto [p0, p1] = provider.startNewScenario(dummyPlayerSetup, dummyPlayerSetup);
  EXPECT(sawEnemy(p0) == true);
  EXPECT(sawEnemy(p1) == true);
  EXPECT_THROWS(checkFirstOpponentStrictly(p0));
  EXPECT_THROWS(checkFirstOpponentStrictly(p1));
}

CASE("state/firstopponent_multiplayer_unitsnone") {
  // sawEnemy() should always be true for MicroPlayers, who apply non-strict
  // criteria for identifying enemies. If nobody has units, firstOpponent should
  // throw for each player.
  auto provider = makeScenarioProvider(kMapWithUnitsForNobody);
  auto [p0, p1] = provider.startNewScenario(dummyPlayerSetup, dummyPlayerSetup);
  EXPECT(sawEnemy(p0) == true);
  EXPECT(sawEnemy(p1) == true);
  EXPECT_THROWS(checkFirstOpponentStrictly(p0));
  EXPECT_THROWS(checkFirstOpponentStrictly(p1));
}

} // namespace