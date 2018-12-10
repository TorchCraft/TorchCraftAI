/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "botscenario.h"

#include "cherrypi.h"
#include "fsutils.h"
#include "openbwprocess.h"
#include "playscript.h"

#include "common/rand.h"
#include "common/str.h"

#include <fmt/format.h>
#include <glog/logging.h>

namespace {
int constexpr kBotPlayTimeoutMs = 120000;

std::string makePlayId(size_t len = 32) {
  static std::mt19937 rng = std::mt19937(std::random_device()());
  static const char alphanum[] = "0123456789abcdef";
  std::uniform_int_distribution<int> dis(0, sizeof(alphanum) - 2);
  std::string s(len, 0);
  for (size_t i = 0; i < len; i++) {
    s[i] = alphanum[dis(rng)];
  }
  return s;
}
} // namespace

namespace cherrypi {

BotScenario::BotScenario(
    std::string const& map,
    tc::BW::Race myRace,
    std::string const& enemyBot,
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
          {"BWAPI_CONFIG_AUTO_MENU__RACE", myRace._to_string(), true},
          {"BWAPI_CONFIG_AUTO_MENU__SAVE_REPLAY", replayPath, true},
      });
  proc2_ = std::make_shared<OpenBwProcess>(
      enemyBot,
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
      });
#endif // WITHOUT_POSIX
}

std::shared_ptr<tc::Client> BotScenario::makeClient(tc::Client::Options opts) {
  return detail::makeClient(proc1_, std::move(opts), kBotPlayTimeoutMs);
}

PlayScriptScenario::PlayScriptScenario(
    std::vector<std::string> maps,
    std::string enemyBot,
    std::string outputPath,
    std::vector<EnvVar> vars)
    : enemyBot_(enemyBot) {
  vars.insert(vars.begin(), {"OPPONENT", enemyBot, true});

  auto playId = makePlayId();
  path_ = outputPath + "/" + playId;
  vars.insert(vars.begin(), {"OUTPUT", outputPath, true});
  vars.insert(vars.begin(), {"PLAYID", playId, true});

  // Sanity check: maps should be absolute paths and not contain duplicates --
  // otherwise, PlayScript will not be happy.
  std::unordered_set<std::string> mapSet;
  for (auto it = maps.begin(); it != maps.end(); ++it) {
    if (it->empty() || (*it)[0] != '/') {
      throw std::runtime_error(
          "Absolute map paths required, but found '" + *it + "'");
    }
    if (mapSet.find(*it) != mapSet.end()) {
      LOG(WARNING) << "Removing duplicate map '" << *it << "' from map pool";
      it = maps.erase(it);
    } else {
      mapSet.insert(*it);
    }
  }

  vars.insert(vars.begin(), {"MAPS", common::joinVector(maps, ','), true});
  proc_ = std::make_shared<PlayScript>(vars);
}

PlayScriptScenario::PlayScriptScenario(
    std::string map,
    std::string enemyBot,
    std::string outputPath,
    std::vector<EnvVar> vars)
    : PlayScriptScenario(
          std::vector<std::string>{std::move(map)},
          std::move(enemyBot),
          std::move(outputPath),
          std::move(vars)) {}

PlayScriptScenario::~PlayScriptScenario() {
  if (autoDelete_ && !path_.empty()) {
#ifdef WITHOUT_POSIX
    LOG(WARNING) << "Can't remove directory '" << path_
                 << "' on non-posix systems";
#else
    fsutils::rmrf(path_);
#endif
  }
}

void PlayScriptScenario::setAutoDelete(bool autoDelete) {
  autoDelete_ = autoDelete;
}

std::shared_ptr<tc::Client> PlayScriptScenario::makeClient(
    tc::Client::Options opts) {
  numGamesStarted_++;
  return detail::makeClient(proc_, std::move(opts), kBotPlayTimeoutMs);
}

} // namespace cherrypi
