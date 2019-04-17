/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "blackboard.h"
#include "gameutils/scenariospecification.h"
#include "modules/lambda.h"
#include "state.h"

namespace cherrypi {

/**
 * A simple utility module that runs a user-supplied function once per game.
 */
class OnceModule : public LambdaModule {
 public:
  OnceModule(StepFunctionState func, std::string name = std::string());
  OnceModule(StepFunctionStateModule func, std::string name = std::string());
  virtual ~OnceModule() = default;

  /// Spawns ally units.
  static std::shared_ptr<Module> makeWithSpawns(
      const std::vector<SpawnPosition>& spawns,
      std::string name = std::string());

  /// Similar function for enemy units
  static std::shared_ptr<Module> makeWithEnemySpawns(
      const std::vector<SpawnPosition>& spawns,
      std::string name = std::string());

  virtual void step(State* state) override;

  /// Returns a list of commands which spawn units
  static std::vector<tc::Client::Command> makeSpawnCommands(
      const std::vector<SpawnPosition>& spawns,
      State* state,
      int playerId);

 protected:
  /// Convenience function that returns a lambda to spawn units
  /// If enemy is true, then the units are spawned for the enemy
  static LambdaModule::StepFunctionState makeSpawnFn(
      const std::vector<SpawnPosition>& spawns,
      bool enemy);

 private:
  std::string key_;
};

} // namespace cherrypi
