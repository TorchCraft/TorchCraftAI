/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "scenarioprovider.h"

namespace cherrypi {

/// This provides access to a few scenario internals for easier logging from
/// outside spawnNextScenario()
class BuildingPlacerScenarioProvider : public ScenarioProvider {
 public:
  BuildingPlacerScenarioProvider(
      int maxFrame,
      std::string mapPool,
      bool gui = false)
      : ScenarioProvider(maxFrame, gui), mapPool_(std::move(mapPool)) {}

  void setReplayPath(std::string path) {
    replayPath_ = std::move(path);
  }
  int maxFrames() const {
    return maxFrame_;
  }
  std::string currentMap() const {
    return map_;
  }
  std::string currentBuild1() const {
    return build1_;
  }
  std::string currentBuild2() const {
    return build2_;
  }

  virtual void cleanScenario() override {
    scenario_ = nullptr;
    player1_ = nullptr;
    player2_ = nullptr;
  }

 protected:
  std::string map_;
  std::string build1_;
  std::string build2_;
  std::string mapPool_;
  std::string replayPath_;
};

// Supported scenarios:
// - "vsrules": melee game against rule-based version (with same build) for 30
// minutes. The build is randomly selected based on the "-build" CLI flag.
// - "sunkenplacement" melee game against rule-based version with fixed builds.
// good sunken colony placement is imporant for reliably winning the game.
std::unique_ptr<BuildingPlacerScenarioProvider> makeBPRLScenarioProvider(
    std::string const& name,
    std::string const& maps, // path to map or map folder
    bool gui = false);

} // namespace cherrypi
