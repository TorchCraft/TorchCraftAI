/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "scenario.h"

namespace cherrypi {

Scenario::Scenario(std::string map, std::string race, bool forceGui)
    : forceGui_(forceGui), map_(std::move(map)), race_(std::move(race)) {
  proc_ = startProcess();
}

std::shared_ptr<tc::Client> Scenario::makeClient(
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

std::unique_ptr<OpenBwProcess> Scenario::startProcess() const {
  auto envVars = std::vector<OpenBwProcess::EnvVar>{
      {"BWAPI_CONFIG_AUTO_MENU__GAME_TYPE", "USE_MAP_SETTINGS", true},
      {"BWAPI_CONFIG_AUTO_MENU__MAP", map_.c_str(), true},
      {"BWAPI_CONFIG_AUTO_MENU__RACE", race_.c_str(), true},
      {"OPENBW_ENABLE_UI", forceGui_ ? "1" : "0", forceGui_}};
  return std::make_unique<OpenBwProcess>(std::move(envVars));
}

MeleeScenario::MeleeScenario(
    std::string map,
    std::string myRace,
    std::string enemyRace,
    bool forceGui)
    : Scenario(),
      forceGui_(forceGui),
      map_(std::move(map)),
      myRace_(std::move(myRace)),
      enemyRace_(std::move(enemyRace)) {
  proc_ = startProcess();
}

std::unique_ptr<OpenBwProcess> MeleeScenario::startProcess() const {
  auto envVars = std::vector<OpenBwProcess::EnvVar>{
      {"BWAPI_CONFIG_AUTO_MENU__GAME_TYPE", "MELEE", true},
      {"BWAPI_CONFIG_AUTO_MENU__MAP", map_.c_str(), true},
      {"BWAPI_CONFIG_AUTO_MENU__RACE", myRace_.c_str(), true},
      {"BWAPI_CONFIG_AUTO_MENU__ENEMY_RACE", enemyRace_.c_str(), true},
      {"OPENBW_ENABLE_UI", forceGui_ ? "1" : "0", forceGui_}};
  return std::make_unique<OpenBwProcess>(std::move(envVars));
}

} // namespace cherrypi
