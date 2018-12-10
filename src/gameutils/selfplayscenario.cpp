/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "selfplayscenario.h"

#include "cherrypi.h"
#include "fsutils.h"
#include "openbwprocess.h"
#include "utils.h"

#ifndef WITHOUT_POSIX
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <fmt/format.h>
#include <glog/logging.h>

namespace cherrypi {

namespace {
int constexpr kSelfPlayTimeoutMs = 10000;
} // namespace

namespace detail {

FifoPipes::FifoPipes() {
#ifndef WITHOUT_POSIX
  root_ = fsutils::mktempd();
  pipe1 = root_ + "/1";
  pipe2 = root_ + "/2";
  if (mkfifo(pipe1.c_str(), 0666) != 0) {
    LOG(ERROR) << "Cannot create named pipe at " << pipe1;
    fsutils::rmrf(root_);
    throw std::system_error(errno, std::system_category());
  }
  if (mkfifo(pipe2.c_str(), 0666) != 0) {
    LOG(ERROR) << "Cannot create named pipe at " << pipe2;
    fsutils::rmrf(root_);
    throw std::system_error(errno, std::system_category());
  }
#else
  throw std::runtime_error("Not available for windows");
#endif // !WITHOUT_POSIX
}

FifoPipes::~FifoPipes() {
#ifndef WITHOUT_POSIX
  fsutils::rmrf(root_);
#endif // !WITHOUT_POSIX
}

} // namespace detail

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
  proc1_ = std::make_shared<OpenBwProcess>(
      std::vector<OpenBwProcess::EnvVar>{
          {"OPENBW_ENABLE_UI", forceGui ? "1" : "0", forceGui},
          {"OPENBW_LAN_MODE", "FILE", true},
          {"OPENBW_FILE_READ", pipes_.pipe1, true},
          {"OPENBW_FILE_WRITE", pipes_.pipe2, true},
          {"BWAPI_CONFIG_AUTO_MENU__AUTO_MENU", "LAN", true},
          {"BWAPI_CONFIG_AUTO_MENU__GAME_TYPE",
           detail::gameTypeName(gameType),
           true},
          {"BWAPI_CONFIG_AUTO_MENU__MAP", map, true},
          {"BWAPI_CONFIG_AUTO_MENU__RACE", race1._to_string(), true},
          {"BWAPI_CONFIG_AUTO_MENU__CHARACTER_NAME",
           fmt::format("BWEnv1_{}", race1._to_string()),
           true},
          {"BWAPI_CONFIG_AUTO_MENU__SAVE_REPLAY", replayPath, true},
      });
  proc2_ = std::make_shared<OpenBwProcess>(
      std::vector<OpenBwProcess::EnvVar>{
          {"OPENBW_ENABLE_UI", "0", true},
          {"OPENBW_LAN_MODE", "FILE", true},
          {"OPENBW_FILE_READ", pipes_.pipe2, true},
          {"OPENBW_FILE_WRITE", pipes_.pipe1, true},
          {"BWAPI_CONFIG_AUTO_MENU__AUTO_MENU", "LAN", true},
          {"BWAPI_CONFIG_AUTO_MENU__GAME_TYPE",
           detail::gameTypeName(gameType),
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
  return detail::makeClient(proc1_, std::move(opts), kSelfPlayTimeoutMs);
}

std::shared_ptr<tc::Client> SelfPlayScenario::makeClient2(
    tc::Client::Options opts) {
  return detail::makeClient(proc2_, std::move(opts), kSelfPlayTimeoutMs);
}

} // namespace cherrypi
