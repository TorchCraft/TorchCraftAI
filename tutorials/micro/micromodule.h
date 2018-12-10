/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <map>
#include <memory>
#include <tuple>
#include <vector>

#include "modules.h"
#include "trainingsetup.h"
#include "gameutils/microfixedscenario.h"

namespace cp = cherrypi;

namespace microbattles {

class MicroModule : public cp::Module {
 public:
  unsigned int threadId_;
  long currentFrame_ = 0;
  float frameReward_ = 0.;
  bool started_ = false;
  bool aborted_ = false;
  float lastAllyCount_, lastEnemyCount_, lastAllyHp_, lastEnemyHp_;
  float firstAllyCount_, firstEnemyCount_, firstAllyHp_, firstEnemyHp_;
  std::shared_ptr<TrainingSetup> training_;
  std::shared_ptr<cpid::Trainer> trainer_;
  std::shared_ptr<cp::MicroFixedScenario::Reward> reward_;
  std::unique_ptr<cpid::EpisodeHandle> episode_;
  cpid::GameUID gameUID_;
  // Keep track of unit attacks to avoid repetition
  std::unordered_map<cp::Unit*, cp::Unit*> attacks_;

 public:
  MicroModule(
      unsigned int threadId,
      std::shared_ptr<TrainingSetup>,
      std::shared_ptr<cpid::Trainer>,
      std::unique_ptr<cp::MicroFixedScenario::Reward>&&);
  void onGameStart(cp::State* state) override;
  void step(cp::State* state) override;
  void onGameEnd(cp::State* state) override;

 protected:
  std::shared_ptr<cp::UPCTuple> convertAction(
      cp::State* state,
      cp::Unit* ally,
      std::vector<cp::Unit*> enemies,
      unsigned int action,
      cp::Vec2 actionVec);
  void act(cp::State* state);
  float stepReward(cp::State* state);
  void doLastFrame(cp::State* state);

  std::shared_ptr<MicroFeaturizer> featurizer_;
};

} // namespace microbattles
