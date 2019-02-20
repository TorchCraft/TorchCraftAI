/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "initialconditions.h"
#include "modules/once.h"
#include "selfplayscenario.h"
#include "state.h"

namespace cherrypi {

class MicroPlayer;

/**
 * Base class for providing scenarios.
 * Returns a pair of players to be used by the training code.
 *
 * Detects game end and cleans up after the scenario.
 */
class ScenarioProvider {
 public:
  /// \arg maxFrame Frame limit of the scenario
  ScenarioProvider(int maxFrame, bool gui = false)
      : maxFrame_(maxFrame), gui_(gui) {}
  virtual ~ScenarioProvider() {}

  /// Spawns the scenario. It takes as parameters the setup functions for both
  /// players (this should take care of adding modules), and returns the
  /// pointers
  /// to the created players
  virtual std::pair<std::shared_ptr<BasePlayer>, std::shared_ptr<BasePlayer>>
  spawnNextScenario(
      const std::function<void(BasePlayer*)>& setup1,
      const std::function<void(BasePlayer*)>& setup2) = 0;

  /// Check whether the scenario is finished.
  /// By default, return true whenever the number of frames is exceeded or one
  /// of
  /// the players don't have any units left
  /// If checkAttack is true, it will also check that at least one unit in one
  /// army is able to attack at least one unit in the opponent't army.
  virtual bool isFinished(int currentStep, bool checkAttack = true);

  /// Clean the possible left-overs of the last scenario. Must be called before
  /// spawnNextScenario
  virtual void cleanScenario() {}

 protected:
  template <typename T>
  void loadMap(
      std::string const& map,
      tc::BW::Race race1,
      tc::BW::Race race2,
      GameType gameType,
      std::string const& replayPath = std::string()) {
    scenario_ = std::make_shared<SelfPlayScenario>(
        map, race1, race2, gameType, replayPath, gui_);
    player1_ = std::make_shared<T>(scenario_->makeClient1());
    player2_ = std::make_shared<T>(scenario_->makeClient2());
  }

  int maxFrame_;
  bool gui_;
  std::shared_ptr<BasePlayer> player1_;
  std::shared_ptr<BasePlayer> player2_;
  std::shared_ptr<SelfPlayScenario> scenario_;

  int lastPossibleAttack_ = -1;
};

class BaseMicroScenario : public ScenarioProvider {
 public:
  BaseMicroScenario(int maxFrame, bool gui = false);

  void setReplay(std::string const& path) {
    replay_ = path;
  }

  std::pair<std::shared_ptr<BasePlayer>, std::shared_ptr<BasePlayer>>
  spawnNextScenario(
      const std::function<void(BasePlayer*)>& setup1,
      const std::function<void(BasePlayer*)>& setup2) override;

  void cleanScenario() override;

 protected:
  virtual ScenarioInfo getScenarioInfo() = 0;

  void sendKillCmds();

  std::shared_ptr<tc::Client> client1_;
  std::shared_ptr<tc::Client> client2_;
  std::string replay_;
  bool launchedWithReplay_{false};

  int gameCount_ = 0;

  std::string mapPathPrefix_;
};
} // namespace cherrypi
