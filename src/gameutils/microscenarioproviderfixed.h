/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "microscenarioprovider.h"

namespace cherrypi {

class MicroScenarioProviderFixed : public MicroScenarioProvider {
 public:
  MicroScenarioProviderFixed(){};
  MicroScenarioProviderFixed(FixedScenario const&);
  MicroScenarioProviderFixed(std::string const& scenarioName);

  void loadScenario(std::string const& scenarioName);
  void loadScenario(FixedScenario const&);

  // List the names of all available scenarios
  static std::vector<std::string> listScenarios();

 protected:
  FixedScenario getFixedScenario() override;
};

} // namespace cherrypi
