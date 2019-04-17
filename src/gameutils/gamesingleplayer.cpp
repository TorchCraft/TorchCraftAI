/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "game.h"
#include <common/assert.h>

namespace cherrypi {

GameSinglePlayer::GameSinglePlayer(
    GameOptions const& gameOptions,
    GamePlayerOptions const& player1,
    GamePlayerOptions const& player2) {
  auto envVars = std::vector<EnvVar>{
      {"BWAPI_CONFIG_AUTO_MENU__GAME_TYPE",
       gameTypeName(gameOptions.gameType()),
       true},
      {"BWAPI_CONFIG_AUTO_MENU__SAVE_REPLAY", gameOptions.replayPath(), true},
      {"BWAPI_CONFIG_AUTO_MENU__MAP", gameOptions.map(), true},
      {"BWAPI_CONFIG_AUTO_MENU__RACE", player1.race()._to_string(), true},
      {"OPENBW_ENABLE_UI",
       gameOptions.forceGui() ? "1" : "0",
       gameOptions.forceGui()}};
  if (!player1.name().empty()) {
    envVars.push_back({"BWAPI_CONFIG_AUTO_MENU__CHARACTER_NAME",
                       player1.name().c_str(),
                       true});
  }
  ASSERT(
      player2.name().empty(), "Cant specify enemy name in single player game");
  if (player2.race()._value != torchcraft::BW::Race::None) {
    envVars.push_back({"BWAPI_CONFIG_AUTO_MENU__ENEMY_RACE",
                       player2.race()._to_string(),
                       true});
  }
  proc_ = std::make_unique<OpenBwProcess>(std::move(envVars));
}

std::shared_ptr<tc::Client> GameSinglePlayer::makeClient(
    tc::Client::Options opts) const {
  auto client = std::make_shared<tc::Client>();
  if (!proc_->connect(client.get(), 10000)) {
    throw std::runtime_error(
        std::string("Error establishing connection: ") + client->error());
  }

  // Perform handshake
  std::vector<std::string> upd;
  if (!client->init(upd, opts)) {
    throw std::runtime_error(
        std::string("Error initializing connection: ") + client->error());
  }

  return client;
}

GameSinglePlayer GameSinglePlayerUMS(
    std::string const& map,
    std::string const& race,
    bool forceGui) {
  return GameSinglePlayer(
      GameOptions(map).forceGui(forceGui).gameType(GameType::UseMapSettings),
      GamePlayerOptions(tc::BW::Race::_from_string(race.c_str())));
}
GameSinglePlayer GameSinglePlayerMelee(
    std::string map,
    std::string myRace,
    std::string enemyRace,
    bool forceGui) {
  auto enemyRaceTc = tc::BW::Race::None;
  if (!enemyRace.empty()) {
    enemyRaceTc = tc::BW::Race::_from_string(enemyRace.c_str());
  }
  return GameSinglePlayer(
      GameOptions(map).forceGui(forceGui).gameType(GameType::Melee),
      GamePlayerOptions(tc::BW::Race::_from_string(myRace.c_str())),
      GamePlayerOptions(enemyRaceTc));
}
} // namespace cherrypi
