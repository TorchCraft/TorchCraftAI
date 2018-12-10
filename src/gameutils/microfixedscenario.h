/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "scenarioprovider.h"

namespace cherrypi {

namespace {
struct ScenarioInfo;
} // namespace

class MicroFixedScenario : public BaseMicroScenario {
 public:
  MicroFixedScenario(
      int maxFrame,
      SpawnList spawnPlayer1,
      SpawnList spawnPlayer2,
      std::string map = "test/maps/micro-empty2.scm",
      bool gui = false);

  void setSpawns(SpawnList spawnPlayer1, SpawnList spawnPlayer2);

  // These functions spawns a scenario from a list of fixed built in scenarios
  MicroFixedScenario(
      int maxFrame,
      std::string const& scenarioName,
      bool gui = false);

  void setSpawns(std::string const& scenarioName);

  // A suggested reward for the list of fixed scenarios.
  // It creates a reward object that you must manually remember to step through.
  struct Reward {
    virtual ~Reward() = default;
    virtual void begin(cherrypi::State* state);
    virtual void stepReward(cherrypi::State* state) = 0;
    virtual bool terminate(cherrypi::State* state);
    virtual bool terminateOnPeace();
    double reward = -1e10;
  };

  std::unique_ptr<Reward> getReward();

  // List the names of all available scenarios
  static std::vector<std::string> listScenarios();

  // It's possible to run this from not the rootdir of the repository,
  // in which case you can set the mapPathPrefix to where the maps should be
  // found. This is just the path to your cherrypi directory
  static void setMapPathPrefix(std::string);

 protected:
  std::pair<std::vector<OnceModule::SpawnInfo>,
            std::vector<OnceModule::SpawnInfo>>
  getSpawnInfo() override;
  SpawnList spawnPlayer1_;
  SpawnList spawnPlayer2_;
  std::unique_ptr<Reward> reward_;

  // A delegating constructor for the constructor implementation above
  MicroFixedScenario(int maxFrame, ScenarioInfo const&, bool gui);
};

} // namespace cherrypi
