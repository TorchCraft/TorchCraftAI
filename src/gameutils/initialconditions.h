/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "src/modules/lambda.h"
#include "state.h"

namespace cherrypi {
struct Reward {
  virtual ~Reward() = default;
  virtual void begin(cherrypi::State* state);
  virtual void stepReward(cherrypi::State* state) = 0;
  virtual void stepDrawReward(cherrypi::State* state);
  virtual bool terminate(cherrypi::State* state);
  virtual bool terminateOnPeace();
  double reward = -1e10;
};

struct SpawnPosition {
  int count;
  tc::BW::UnitType type;
  int x;
  int y;
  double spreadX = 0.0f;
  double spreadY = 0.0f;

  SpawnPosition(
      int count,
      tc::BW::UnitType type,
      int x,
      int y,
      double spreadX,
      double spreadY)
      : count(count),
        type(type),
        x(x),
        y(y),
        spreadX(spreadX),
        spreadY(spreadY){};

  SpawnPosition(int count, tc::BW::UnitType type, int x, int y)
      : count(count), type(type), x(x), y(y){};
};
using SpawnList = std::vector<SpawnPosition>;

struct ScenarioPlayer {
  std::vector<torchcraft::BW::TechType> techs;
  std::vector<torchcraft::BW::UpgradeType> upgrades;
};

struct ScenarioInfo {
  std::string name;
  SpawnList allyList;
  SpawnList enemyList;
  std::string map{"test/maps/micro-empty2.scm"};
  std::function<std::unique_ptr<Reward>()> reward;
  std::vector<ScenarioPlayer> players = {{}, {}};
  std::vector<LambdaModule::StepFunctionState> stepFunctions;

  ScenarioInfo& addTech(int player, torchcraft::BW::TechType tech) {
    players[player].techs.push_back(tech);
    return *this;
  }

  ScenarioInfo& addUpgrade(int player, torchcraft::BW::UpgradeType upgrade) {
    players[player].upgrades.push_back(upgrade);
    return *this;
  }
  
  ScenarioInfo& changeMap(std::string map_file) {
    map = map_file;
    return *this;
  }
};

struct FixedScenarioGroup {
  std::string name;
  std::vector<ScenarioInfo> scenarios = std::vector<ScenarioInfo>();

  ScenarioInfo& add(std::string name) {
    scenarios.emplace_back(ScenarioInfo{name});
    return scenarios.back();
  };
};

std::unique_ptr<Reward> combatReward();
std::unique_ptr<Reward> combatDeltaReward(
    float dmgScale,
    float dmgTakenScale,
    float deathScale,
    float killScale,
    float winScale);
std::unique_ptr<Reward> killSpeedReward();
std::unique_ptr<Reward> proximityToReward(int y, int x);
std::unique_ptr<Reward> proximityToEnemyReward();
std::unique_ptr<Reward> protectCiviliansReward();
std::unique_ptr<Reward> defilerProtectZerglingsReward();
std::unique_ptr<Reward> defilerWinLossReward();

} // namespace cherrypi
