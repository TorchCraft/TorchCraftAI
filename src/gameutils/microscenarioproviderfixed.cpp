/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "microscenarioproviderfixed.h"

#include "scenariospecification.h"
#include "utils.h"

namespace cherrypi {

extern FixedScenario getScenario(const std::string& scenarioName);
extern std::vector<FixedScenario> allScenarios();

MicroScenarioProviderFixed::MicroScenarioProviderFixed(
    FixedScenario const& scenarioInfo)
    : MicroScenarioProvider() {
  loadScenario(scenarioInfo);
}

MicroScenarioProviderFixed::MicroScenarioProviderFixed(
    std::string const& scenarioName)
    : MicroScenarioProvider() {
  loadScenario(scenarioName);
}

void MicroScenarioProviderFixed::loadScenario(std::string const& scenarioName) {
  loadScenario(getScenario(scenarioName));
}

void MicroScenarioProviderFixed::loadScenario(
    FixedScenario const& scenarioInfo) {
  scenarioNow_ = scenarioInfo;
}

std::vector<std::string> MicroScenarioProviderFixed::listScenarios() {
  auto scenarios = allScenarios();
  std::vector<std::string> output;
  for (auto& scenario : scenarios) {
    output.emplace_back(scenario.name);
  }
  return output;
}

FixedScenario MicroScenarioProviderFixed::getFixedScenario() {
  return scenarioNow_;
}

} // namespace cherrypi
