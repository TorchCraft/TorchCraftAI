/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "selfplayscenario.h"

#include "cherrypi.h"
#include "openbwprocess.h"
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
int constexpr kSelfPlayTimeoutMs = 10000;
} // namespace

SelfPlayScenario::SelfPlayScenario(
    std::string const& map,
    tc::BW::Race race1,
    tc::BW::Race race2,
    GameType gameType,
    std::string const& replayPath,
    bool forceGui) {
#ifdef WITHOUT_POSIX
  throw std::runtime_error("Not available for windows");
#else
  proc1_ = std::make_shared<OpenBwProcess>(std::vector<cherrypi::EnvVar>{
      {"OPENBW_ENABLE_UI", forceGui ? "1" : "0", forceGui},
      {"OPENBW_LAN_MODE", "FILE", true},
      {"OPENBW_FILE_READ", pipes_.pipe1, true},
      {"OPENBW_FILE_WRITE", pipes_.pipe2, true},
      {"BWAPI_CONFIG_AUTO_MENU__AUTO_MENU", "LAN", true},
      {"BWAPI_CONFIG_AUTO_MENU__GAME_TYPE",
       gameTypeName(gameType),
       true},
      {"BWAPI_CONFIG_AUTO_MENU__MAP", map, true},
      {"BWAPI_CONFIG_AUTO_MENU__RACE", race1._to_string(), true},
      {"BWAPI_CONFIG_AUTO_MENU__CHARACTER_NAME",
       fmt::format("BWEnv1_{}", race1._to_string()),
       true},
      {"BWAPI_CONFIG_AUTO_MENU__SAVE_REPLAY", replayPath, true},
  });
  proc2_ = std::make_shared<OpenBwProcess>(std::vector<cherrypi::EnvVar>{
      {"OPENBW_ENABLE_UI", "0", true},
      {"OPENBW_LAN_MODE", "FILE", true},
      {"OPENBW_FILE_READ", pipes_.pipe2, true},
      {"OPENBW_FILE_WRITE", pipes_.pipe1, true},
      {"BWAPI_CONFIG_AUTO_MENU__AUTO_MENU", "LAN", true},
      {"BWAPI_CONFIG_AUTO_MENU__GAME_TYPE",
       gameTypeName(gameType),
       true},
      {"BWAPI_CONFIG_AUTO_MENU__MAP", map, true},
      {"BWAPI_CONFIG_AUTO_MENU__RACE", race2._to_string(), true},
      {"BWAPI_CONFIG_AUTO_MENU__CHARACTER_NAME",
       fmt::format("BWEnv2_{}", race2._to_string()),
       true},
  });
#endif // WITHOUT_POSIX
}

std::shared_ptr<tc::Client> SelfPlayScenario::makeClient1(
    tc::Client::Options opts) {
  return cherrypi::makeClient(proc1_, std::move(opts), kSelfPlayTimeoutMs);
}

std::shared_ptr<tc::Client> SelfPlayScenario::makeClient2(
    tc::Client::Options opts) {
  return cherrypi::makeClient(proc2_, std::move(opts), kSelfPlayTimeoutMs);
}

} // namespace cherrypi
