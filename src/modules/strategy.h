/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <functional>
#include <list>

#include "autobuild.h"
#include "cherrypi.h"
#include "models/bandit.h"
#include "module.h"
#include "state.h"

#ifdef HAVE_TORCH
#include "models/bos/runner.h"
#endif // HAVE_TORCH

namespace cherrypi {

class StrategyModule : public Module {
 public:
  enum Duty {
    None = 0,
    BuildOrder = 1 << 0,
    Scouting = 1 << 1,
    Harassment = 1 << 2,
    All = 0xFFFF,
  };

  StrategyModule(Duty duties = Duty::All);
  virtual ~StrategyModule() = default;

  virtual void step(State* state) override;
  virtual void onGameStart(State* s) override;
  virtual void onGameEnd(State* s) override;

 protected:
  /**
   * Chooses the build order.
   */
  virtual void stepBuildOrder(State* state);
  virtual void stepScouting(State* state);
  virtual void stepHarassment(State* state);

  std::shared_ptr<ProxyTask> getProxyTaskWithCommand(
      State* state,
      Command command);

  std::string currentBuildOrder_;

  std::string getOpeningBuildOrder(State* s);
  void spawnBuildOrderTask(
      State* state,
      UpcId originUpcId,
      std::string const& buildorder);

 private:
  /**
   * Selects which initial build order to use,
   * using either the default set of builds, or FLAGS_build.
   * Uses multi-armed bandit selection to pick the build we think most likely
   * to beat our current opponent.
   */
  std::string selectBO(
      State* state,
      tc::BW::Race ourRace,
      tc::BW::Race enemyRace,
      const std::string& mapName,
      const std::string& enemyName);
#ifdef HAVE_TORCH
  std::unique_ptr<bos::ModelRunner> makeBosRunner(State* state);
  std::string stepBos(State* state);
  bool shouldListenToBos(State* state);
#endif // HAVE_TORCH

  Duty duties_;
  int nbScoutingOverlords_ = 0;
  int nbScoutingExplorers_ = 0;
  int nbScoutingWorkers_ = 0;

#ifdef HAVE_TORCH
  std::unique_ptr<bos::ModelRunner> bosRunner_ = nullptr;
  FrameNum nextBosForwardFrame_ = 0;
  float bosStartTime_ = 0;
  bool bosMapVerified_ = false;
#endif // HAVE_TORCH
};

DEFINE_FLAG_OPERATORS(StrategyModule::Duty);

} // namespace cherrypi
