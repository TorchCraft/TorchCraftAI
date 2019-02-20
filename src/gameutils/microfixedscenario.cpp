/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "microfixedscenario.h"

#include "initialconditions.h"
#include "utils.h"

namespace cherrypi {

extern ScenarioInfo getScenario(const std::string& scenarioName);
extern std::vector<ScenarioInfo> allScenarios();

MicroFixedScenario::MicroFixedScenario(
    int maxFrame,
    ScenarioInfo const& scenarioInfo,
    bool gui)
    : BaseMicroScenario(maxFrame, gui) {
  loadScenario(scenarioInfo);
}

MicroFixedScenario::MicroFixedScenario(
    int maxFrame,
    std::string const& scenarioName,
    bool gui)
    : BaseMicroScenario(maxFrame, gui) {
  loadScenario(scenarioName);
}

void MicroFixedScenario::loadScenario(std::string const& scenarioName) {
  loadScenario(getScenario(scenarioName));
}

void MicroFixedScenario::loadScenario(ScenarioInfo const& scenarioInfo) {
  // Hack:
  // We have to reset the map, and we do this by deleting the players,
  // and relying on the BaseMicroScenario's spawnNextScenario behavior.
  //
  if (scenarioInfo_.map != scenarioInfo.map) {
    player1_ = nullptr;
    player2_ = nullptr;
  }

  scenarioInfo_ = scenarioInfo;
  playersGotReward_.reset();
}

std::vector<std::string> MicroFixedScenario::listScenarios() {
  auto scenarios = allScenarios();
  std::vector<std::string> output;
  for (auto& scenario : scenarios) {
    output.emplace_back(scenario.name);
  }
  return output;
}

std::unique_ptr<Reward> MicroFixedScenario::getReward(PlayerId id) {
  if (id >= int(playersGotReward_.size())) {
    throw std::runtime_error(fmt::format(
        "MicroFixedScenario supports maximum {} players",
        playersGotReward_.size()));
  }
  if (playersGotReward_.test(id)) {
    throw std::runtime_error(
        fmt::format("Player {} already received its reward", id));
  }
  playersGotReward_.set(id, true);
  if (scenarioInfo_.reward == nullptr) {
    return combatReward();
  } else {
    return scenarioInfo_.reward();
  }
}

void MicroFixedScenario::setMapPathPrefix(std::string prefix) {
  mapPathPrefix_ = prefix;
}

ScenarioInfo MicroFixedScenario::getScenarioInfo() {
  return scenarioInfo_;
}

} // namespace cherrypi
