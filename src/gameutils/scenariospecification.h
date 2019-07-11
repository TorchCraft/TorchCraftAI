/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "game.h"
#include "rewards.h"
#include "src/modules/lambda.h"
#include "state.h"

namespace cherrypi {

struct SpawnPosition {
  int count;
  tc::BW::UnitType type;
  int x;
  int y;
  double spreadX = 0.0;
  double spreadY = 0.0;
  int health = -1;
  int shields = -1;
  int energy = -1;
};

struct ScenarioUpgradeLevel {
  torchcraft::BW::UpgradeType upgradeType;
  int level = 1;
};

struct FixedScenarioPlayer {
  std::vector<torchcraft::BW::TechType> techs;
  std::vector<ScenarioUpgradeLevel> upgrades;
  std::vector<SpawnPosition> units;

  int getUpgradeLevel(int upgradeType) const {
    for (auto& upgrade : upgrades) {
      if (upgrade.upgradeType ==
          torchcraft::BW::UpgradeType::_from_integral_unchecked(upgradeType)) {
        return upgrade.level;
      }
    }
    return 0;
  }

  int hasTech(int techType) const {
    return techs.end() !=
        std::find(
               techs.begin(),
               techs.end(),
               torchcraft::BW::TechType::_from_integral_unchecked(techType));
  }
};

struct FixedScenario {
  std::string name;
  std::string map{"test/maps/micro-empty2.scm"};
  GameType gameType = GameType::UseMapSettings;
  std::function<std::unique_ptr<Reward>()> reward = []() {
    return combatReward();
  };
  std::vector<FixedScenarioPlayer> players = {{}, {}};
  std::vector<LambdaModule::StepFunctionState> stepFunctions;

  FixedScenario& addTech(int player, torchcraft::BW::TechType tech) {
    players[player].techs.push_back(tech);
    return *this;
  }

  FixedScenario&
  addUpgrade(int player, torchcraft::BW::UpgradeType upgrade, int level = 1) {
    players[player].upgrades.push_back({upgrade, level});
    return *this;
  }

  std::vector<SpawnPosition>& allies() {
    return players[0].units;
  }
  std::vector<SpawnPosition>& enemies() {
    return players[1].units;
  }
};

struct FixedScenarioGroup {
  std::string name;
  std::vector<FixedScenario> scenarios = std::vector<FixedScenario>();

  FixedScenario& add(std::string name) {
    scenarios.emplace_back(FixedScenario{name});
    return scenarios.back();
  };
};

std::vector<FixedScenario> allScenarios();
FixedScenario getScenario(const std::string& scenarioName);

} // namespace cherrypi
