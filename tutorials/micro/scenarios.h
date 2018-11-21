/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <memory>
#include <string>

#include "reward.h"
#include "scenarioprovider.h"

namespace microbattles {

struct Scenario {
  std::string name;
  cherrypi::SpawnList allyList;
  cherrypi::SpawnList enemyList;
  std::string map{"test/maps/micro-empty2.scm"};
  std::function<std::unique_ptr<Reward>()> reward = combatReward;
};

struct ScenarioGroup {
  std::string name;
  std::vector<Scenario> scenarios = std::vector<Scenario>();

  Scenario& add(std::string name) {
    scenarios.emplace_back(Scenario{name});
    return scenarios.back();
  };
};

/**
 * Retrieves all Scenarios
 */
std::vector<Scenario> allScenarios();

/**
 * Retrieves all groups of Scenarios
 */
std::vector<ScenarioGroup> allScenarioGroups();

/**
 * Lists the names of all available Scenarios
 */
std::vector<std::string> listScenarios();

/**
 * Retrieves a Scenario by name
 */
Scenario getScenario(const std::string& scenarioName);

} // namespace microbattles
