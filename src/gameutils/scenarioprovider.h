/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "game.h"
#include "modules/once.h"
#include "scenariospecification.h"
#include "state.h"

namespace cherrypi {

/**
 * Base class for providing scenarios.
 * Returns a pair of players to be used by the training code.
 *
 * Detects game end and cleans up after the scenario.
 */
class ScenarioProvider {
 public:
  virtual ~ScenarioProvider() = default;

  /// The maximum number of steps to play before considering a scenario finished
  /// Defaults to -1, which indicates no maximum.
  ScenarioProvider& setMaxFrames(int value) {
    maxFrame_ = value;
    return *this;
  }

  /// Specifies whether to run OpenBW headfully.
  /// Takes effect only when spawning a new OpenBWProcess; so generally you want
  /// to invoke this before using the ScenarioProvider.
  ScenarioProvider& setGui(bool value) {
    gui_ = value;
    return *this;
  }

  /// Spawns a scenario. It takes as parameters the setup functions for both
  /// players (this should take care of adding modules), and returns the
  /// pointers to the created players
  virtual std::pair<std::shared_ptr<BasePlayer>, std::shared_ptr<BasePlayer>>
  startNewScenario(
      const std::function<void(BasePlayer*)>& setup1,
      const std::function<void(BasePlayer*)>& setup2) = 0;

  /// Check whether the scenario is finished.
  /// By default, return true whenever the number of frames is exceeded or one
  /// of the players don't have any units left
  virtual bool isFinished(int currentStep);

 protected:
  template <typename TPlayer>
  void loadMap(
      std::string const& map,
      tc::BW::Race race1,
      tc::BW::Race race2,
      GameType gameType,
      std::string const& replayPath = std::string()) {
    game_ = std::make_shared<GameMultiPlayer>(
        map, race1, race2, gameType, replayPath, gui_);
    player1_ = std::make_shared<TPlayer>(game_->makeClient1());
    player2_ = std::make_shared<TPlayer>(game_->makeClient2());
  };

  // Two hours, which is longer than any reasonable game but short enough to
  // finish in reasonable time if a scenario goes off the rails.
  int maxFrame_ = 24 * 60 * 60 * 2;
  bool gui_ = false;

  std::shared_ptr<BasePlayer> player1_;
  std::shared_ptr<BasePlayer> player2_;
  std::shared_ptr<GameMultiPlayer> game_;
};

} // namespace cherrypi
