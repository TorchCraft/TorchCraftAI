/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "game.h"
#include "microplayer.h"
#include "modules/once.h"
#include "scenarioprovider.h"
#include "state.h"

namespace cherrypi {

class MicroScenarioProvider : public ScenarioProvider {
 public:
  virtual ~MicroScenarioProvider() = default;
  void setReplay(std::string const& path) {
    replay_ = path;
  }

  std::pair<std::shared_ptr<BasePlayer>, std::shared_ptr<BasePlayer>>
  startNewScenario(
      const std::function<void(BasePlayer*)>& setup1,
      const std::function<void(BasePlayer*)>& setup2) override;
  void endScenario();

  std::unique_ptr<Reward> getReward() const;

  /// It's possible to run this from not the rootdir of the repository,
  /// in which case you can set the mapPathPrefix to where the maps should be
  /// found. This is just the path to your cherrypi directory
  void setMapPathPrefix(const std::string&);

 protected:
  virtual FixedScenario getFixedScenario() = 0;

  std::shared_ptr<tc::Client> client1_;
  std::shared_ptr<tc::Client> client2_;
  std::string replay_;
  std::string mapPathPrefix_;

  FixedScenario scenarioNow_;

  int unitsThisGame_ = 0;
  int unitsTotal_ = 0;
  int unitsSeenTotal_ = 0;
  int scenarioCount_ = 0;
  int resetCount_ = 0;

  void endGame();
  void killAllUnits();
  void createNewGame();
  void createNewPlayers();
  void setupScenario();

  bool launchedWithReplay() {
    return !replay_.empty();
  }
  MicroPlayer& microPlayer1() {
    return *std::static_pointer_cast<MicroPlayer>(player1_);
  }
  MicroPlayer& microPlayer2() {
    return *std::static_pointer_cast<MicroPlayer>(player2_);
  }
};
} // namespace cherrypi
