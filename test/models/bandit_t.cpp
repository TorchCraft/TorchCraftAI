/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "fsutils.h"
#include "modules.h"
#include "gameutils/selfplayscenario.h"
#include "test.h"

using namespace cherrypi;
using namespace autobuild;

namespace {
auto constexpr kTerran = +tc::BW::Race::Terran;
auto constexpr kProtoss = +tc::BW::Race::Protoss;
auto constexpr kZerg = +tc::BW::Race::Zerg;
auto constexpr kUnknown = +tc::BW::Race::Unknown;
} // namespace

CASE("models/bandit/acceptableBuildOrders/per_own_race") {
  std::unordered_map<std::string, model::BuildOrderConfig> configs;
  configs["asZergYes"].validOpening(true);
  configs["asZergNo"].validOpening(true).ourRaces(
      {kTerran, kProtoss, kUnknown});

  auto expected = std::vector<std::string>{"asZergYes"};
  EXPECT(acceptableBuildOrders(configs, kZerg, kUnknown) == expected);
}

CASE("models/bandit/acceptableBuildOrders/per_enemy_race") {
  std::unordered_map<std::string, model::BuildOrderConfig> configs;
  configs["enemyTerranYes"].validOpening(true).enemyRaces({kTerran, kProtoss});
  configs["enemyTerranNo"].validOpening(true).enemyRaces({kZerg, kProtoss});

  auto expected = std::vector<std::string>{"enemyTerranYes"};
  EXPECT(acceptableBuildOrders(configs, kZerg, kTerran) == expected);
}

CASE("models/bandit/buildOrdersForTraining") {
  auto configs = model::buildOrdersForTraining();
  std::set<tc::BW::Race> hasBuildOrders;
  for (auto config : configs) {
    for (auto race : config.second.enemyRaces()) {
      hasBuildOrders.insert(race);
    }
  }
  EXPECT(hasBuildOrders.size() == 4u); // need all 3 races and kUnknown
}

CASE("models/bandit/BuildOrderCount") {
  model::BuildOrderCount count;
  // make sure it works with no data
  EXPECT(count.winRate() == 0);

  for (bool won : {false, true, false, false}) {
    count.addGame(won);
  }
  EXPECT(count.numWins() == 1);
  EXPECT(count.numGames() == 4);
  EXPECT(count.numLosses() == 3);
  EXPECT(round(100 * count.winRate()) == 25);
  EXPECT(count.statusString() == "1/4");
}

CASE("models/bandit/BuildOrderCount/updateLastGame") {
  model::BuildOrderCount count;
  count.addGame(false);
  EXPECT(count.statusString() == "0/1");
  count.updateLastGame(true);
  EXPECT(count.statusString() == "1/1");
}

CASE("models/bandit/ucb1score") {
  model::BuildOrderCount count;
  count.config.priority(12);
  EXPECT(model::score::ucb1Score(count, 5, 2.0) == 120000.);
  for (bool won : {false, true, false, false}) {
    count.addGame(won);
  }
  EXPECT(round(100 * model::score::ucb1Score(count, 5, 2.0)) == 115);
  count.config.priority(0);
  EXPECT(model::score::ucb1Score(count, 5, 2.0) == -1.0f);
}

CASE("models/bandit/maxExploitScore") {
  model::BuildOrderCount count;
  EXPECT(model::score::maxExploitScore(count, 5, 2.0) == 10000.);
  for (bool won : {true, true}) {
    count.addGame(won);
  }
  EXPECT(model::score::maxExploitScore(count, 5, 2.0) == kfInfty);
}

CASE("models/bandit/chooseBuildOrder") {
  std::map<std::string, model::BuildOrderCount> buildCounts;
  buildCounts["unexplored"] = model::BuildOrderCount();
  buildCounts["winner"] = model::BuildOrderCount();
  buildCounts["winner"].addGame(true);
  EXPECT(
      model::score::chooseBuildOrder(
          buildCounts, kBanditUCB1, 2.0, 0.95, 1.0, 1.0, 6.0) == "unexplored");
  EXPECT(
      model::score::chooseBuildOrder(
          buildCounts, kBanditUCB1Exploit, 2.0, 0.95, 1.0, 1.0, 6.0) ==
      "winner");
}

CASE("models/bandit/chooseBuildOrder/empty") {
  // When no build orders are available, it should default to 5pool.
  std::map<std::string, model::BuildOrderCount> buildCounts;
  EXPECT(
      model::score::chooseBuildOrder(
          buildCounts, kBanditUCB1, 2.0, 0.95, 1.0, 1.0, 6.0) == "5pool");
}

CASE("models/bandit/EnemyHistory") {
  // settings
  std::string buildOrder = "5pool";
  std::string dir = fsutils::mktempd();
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(dir); });

  // testing recording
  model::EnemyHistory history("TestHistoryOpponent", dir, dir);
  EXPECT(
      static_cast<int>(history.buildOrderCounts.size()) ==
      0); // loaded without history
  history.addStartingGame(buildOrder);
  EXPECT(static_cast<int>(history.buildOrderCounts.size()) == 1); // updated
  model::EnemyHistory history2("TestHistoryOpponent", dir, dir);
  EXPECT(
      static_cast<int>(history2.buildOrderCounts.size()) ==
      1); // loaded with history
  // testing after match update
  EXPECT(history.buildOrderCounts[buildOrder].statusString() == "0/1");
  history.updateLastGameToVictory(buildOrder);
  model::EnemyHistory history3("TestHistoryOpponent", dir, dir);
  EXPECT(history.buildOrderCounts[buildOrder].statusString() == "1/1");
}

SCENARIO("strategy/onGameStart/onGameEnd") {
  std::string dir = fsutils::mktempd();
  auto owd = fsutils::pwd();
  auto cleanup = utils::makeGuard([&]() {
    fsutils::cd(owd);
    fsutils::rmrf(dir);
  });
  fsutils::cd(dir);
  fsutils::mkdir("bwapi-data/read");
  fsutils::mkdir("bwapi-data/write");

  // Assumes it is run from CherryPi root directory
  // set up
  State state(std::make_shared<tc::Client>());
  Blackboard* board = state.board();
  std::string fakeBuildOrder = "fake_build_order";
  std::string enemyName = "__test_enemy__";
  std::string movedEnemyName = "__test_enemy__";
  StrategyModule module;
  // make sure nothing breaks if no opening bandit provided
  module.onGameEnd(&state);

  // prepare board
  model::EnemyHistory history(enemyName);
  board->post(Blackboard::kEnemyRaceKey, kZerg._to_integral());
  board->post(Blackboard::kEnemyNameKey, std::move(movedEnemyName));

  // before starting
  EXPECT(board->hasKey(Blackboard::kBuildOrderKey) == false);
  EXPECT(board->hasKey(Blackboard::kOpeningBuildOrderKey) == false);

  // after starting
  module.onGameStart(&state);
  EXPECT(board->hasKey(Blackboard::kBuildOrderKey) == true);
  std::string buildOrder = board->get<std::string>(Blackboard::kBuildOrderKey);
  EXPECT(
      board->get<std::string>(Blackboard::kOpeningBuildOrderKey) == buildOrder);
  history = model::EnemyHistory(enemyName, "bwapi-data/write/");
  EXPECT(history.buildOrderCounts[buildOrder].statusString() == "0/1");

  // after finishing
  // only kOpeningBuildOrderKey should be used
  board->post(Blackboard::kBuildOrderKey, std::move(fakeBuildOrder));
  // mock a victory
  board->post("__mock_won_game__", true);
  module.onGameEnd(&state);
  history = model::EnemyHistory(enemyName, "bwapi-data/write/");
  EXPECT(history.buildOrderCounts[buildOrder].statusString() == "1/1");
}
