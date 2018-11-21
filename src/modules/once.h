/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "blackboard.h"
#include "modules/lambda.h"
#include "state.h"

namespace cherrypi {

/**
 * A simple utility module that runs a user-supplied function once per game.
 */
class OnceModule : public LambdaModule {
 public:
  struct SpawnInfo {
    /// The randomPositionSpread parameter corresponds to the randomization of
    /// the initial position. Leave to 0 for deterministic.
    SpawnInfo(
        int t,
        int x,
        int y,
        double randomPositionSpreadX = 0,
        double randomPositionSpreadY = 0)
        // Ok to throw here if type conversion fails
        : type(tc::BW::UnitType::_from_integral(t)),
          x(x),
          y(y),
          randomPositionSpreadX(randomPositionSpreadX),
          randomPositionSpreadY(randomPositionSpreadY){};
    tc::BW::UnitType type;
    // Note: x/y is in walktiles.
    int x;
    int y;
    double randomPositionSpreadX;
    double randomPositionSpreadY;
  };

 public:
  OnceModule(StepFunctionState func, std::string name = std::string());
  OnceModule(StepFunctionStateModule func, std::string name = std::string());
  virtual ~OnceModule() = default;

  static std::shared_ptr<Module> makeWithCommands(
      std::vector<tc::Client::Command> commands,
      std::string name = std::string());

  /// Spawns ally units.
  static std::shared_ptr<Module> makeWithSpawns(
      std::vector<SpawnInfo> spawns,
      std::string name = std::string());

  /// Similar function for enemy units
  static std::shared_ptr<Module> makeWithEnemySpawns(
      std::vector<SpawnInfo> spawns,
      std::string name = std::string());

  virtual void step(State* state) override;

  // convenience function that returns the list of commands to be executed in
  // order to actually make the spawn happen
  static std::vector<tc::Client::Command> makeSpawnCommands(
      std::vector<OnceModule::SpawnInfo> spawns,
      int playerId);

 protected:
  /// This is a convenience fonction that returns a lambda to spawn units
  /// If enemy is true, then the units are spwaned for the enemy
  static LambdaModule::StepFunctionState makeSpawnFn(
      std::vector<OnceModule::SpawnInfo> spawns,
      bool enemy);

 private:
  std::string key_;
};

} // namespace cherrypi
