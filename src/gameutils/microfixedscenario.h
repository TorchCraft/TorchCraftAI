/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "scenarioprovider.h"
#include <bitset>

namespace cherrypi {

class MicroFixedScenario : public BaseMicroScenario {
 public:
  MicroFixedScenario(int maxFrame, ScenarioInfo const&, bool gui);
  MicroFixedScenario(
      int maxFrame,
      std::string const& scenarioName,
      bool gui = false);

  void loadScenario(std::string const& scenarioName);
  void loadScenario(ScenarioInfo const&);

  std::unique_ptr<Reward> getReward(PlayerId id = 0);

  // List the names of all available scenarios
  static std::vector<std::string> listScenarios();

  // It's possible to run this from not the rootdir of the repository,
  // in which case you can set the mapPathPrefix to where the maps should be
  // found. This is just the path to your cherrypi directory
  void setMapPathPrefix(std::string);

 protected:
  ScenarioInfo getScenarioInfo() override;
  ScenarioInfo scenarioInfo_;
  std::bitset<2> playersGotReward_;
};

} // namespace cherrypi
