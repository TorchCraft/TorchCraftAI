/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "game.h"
#include "utils.h"

#ifndef WITHOUT_POSIX
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <common/fsutils.h>

#include <fmt/format.h>
#include <glog/logging.h>

namespace fsutils = common::fsutils;

namespace cherrypi {

namespace {
int constexpr kSelfPlayTimeoutMs = 60000;
} // namespace

GameMultiPlayer::GameMultiPlayer(
    GameOptions const& gameOptions,
    GamePlayerOptions const& player1,
    GamePlayerOptions const& player2) {
#ifdef WITHOUT_POSIX
  throw std::runtime_error("Not available for windows");
#else
  proc1_ = std::make_shared<OpenBwProcess>(std::vector<EnvVar>{
      {"OPENBW_ENABLE_UI",
       gameOptions.forceGui() ? "1" : "0",
       gameOptions.forceGui()},
      {"OPENBW_LAN_MODE", "FILE", true},
      {"OPENBW_FILE_READ", pipes_.pipe1, true},
      {"OPENBW_FILE_WRITE", pipes_.pipe2, true},
      {"BWAPI_CONFIG_AUTO_MENU__AUTO_MENU", "LAN", true},
      {"BWAPI_CONFIG_AUTO_MENU__GAME_TYPE",
       gameTypeName(gameOptions.gameType()),
       true},
      {"BWAPI_CONFIG_AUTO_MENU__MAP", gameOptions.map(), true},
      {"BWAPI_CONFIG_AUTO_MENU__RACE", player1.race()._to_string(), true},
      {"BWAPI_CONFIG_AUTO_MENU__CHARACTER_NAME",
       player1.name().empty()
           ? fmt::format("p1_BWEnv_{}", player1.race()._to_string())
           : player1.name(),
       true},
      {"BWAPI_CONFIG_AUTO_MENU__SAVE_REPLAY", gameOptions.replayPath(), true},
  });
  proc2_ = std::make_shared<OpenBwProcess>(std::vector<EnvVar>{
      {"OPENBW_ENABLE_UI", "0", true},
      {"OPENBW_LAN_MODE", "FILE", true},
      {"OPENBW_FILE_READ", pipes_.pipe2, true},
      {"OPENBW_FILE_WRITE", pipes_.pipe1, true},
      {"BWAPI_CONFIG_AUTO_MENU__AUTO_MENU", "LAN", true},
      {"BWAPI_CONFIG_AUTO_MENU__GAME_TYPE",
       gameTypeName(gameOptions.gameType()),
       true},
      {"BWAPI_CONFIG_AUTO_MENU__MAP", gameOptions.map(), true},
      {"BWAPI_CONFIG_AUTO_MENU__RACE", player2.race()._to_string(), true},
      {"BWAPI_CONFIG_AUTO_MENU__CHARACTER_NAME",
       player2.name().empty()
           ? fmt::format("p2_BWEnv_{}", player2.race()._to_string())
           : player2.name(),
       true},
  });
#endif // WITHOUT_POSIX
}

GameMultiPlayer::GameMultiPlayer(
    std::string const& map,
    tc::BW::Race race1,
    tc::BW::Race race2,
    GameType gameType,
    std::string const& replayPath,
    bool forceGui)
    : GameMultiPlayer(
          GameOptions(map).gameType(gameType).forceGui(forceGui).replayPath(
              replayPath),
          GamePlayerOptions(race1),
          GamePlayerOptions(race2)) {}

std::shared_ptr<tc::Client> GameMultiPlayer::makeClient1(
    tc::Client::Options opts) {
  return makeTorchCraftClient(proc1_, std::move(opts), kSelfPlayTimeoutMs);
}

std::shared_ptr<tc::Client> GameMultiPlayer::makeClient2(
    tc::Client::Options opts) {
  return makeTorchCraftClient(proc2_, std::move(opts), kSelfPlayTimeoutMs);
}

} // namespace cherrypi
