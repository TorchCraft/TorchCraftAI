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

#include "common.h"
#include "common/autograd.h"
#include "gameutils/microscenarioproviderfixed.h"
#include "modules.h"
#include "trainingsetup.h"
#include "upc.h"
#include "utils.h"

namespace cp = cherrypi;

namespace microbattles {

class MicroModule : public cp::Module, public cp::MicroModel {
 public:
  std::string scenarioName;
  float frameReward = 0.;
  unsigned long episodeEndFrame = 0;
  bool won = false;
  bool test = false;
  bool inFullGame = false;
  float lastAllyCount, lastEnemyCount, lastAllyHp, lastEnemyHp;
  float firstAllyCount, firstEnemyCount, firstAllyHp, firstEnemyHp;
  std::shared_ptr<TrainingSetup> setup;
  std::shared_ptr<cpid::Trainer> trainer;
  std::vector<float> frameRewards;
  cpid::EpisodeHandle handle;
  // Keep track of unit attacks to avoid repetition
  // The metrics that we want to track during training and testing
  std::map<std::string, float> numericMetrics;
  std::map<std::string, std::map<cherrypi::UnitId, float>> numericMetricsByUnit;
  std::map<std::string, std::vector<float>> vectorMetrics;
  unsigned long episodeCurrentFrame(cp::State* state) {
    return state->currentFrame() - episodeStartFrame_;
  }

  MicroModule(
      std::shared_ptr<TrainingSetup>,
      std::shared_ptr<cpid::Trainer>,
      std::unique_ptr<cp::Reward>&&);
  void setIllustrate(bool on) {
    illustrate_ = on;
  }
  void setGenerateHeatmaps(bool on) {
    generateHeatmaps_ = on;
  }
  virtual void onGameStart(cp::State* state) override;
  virtual void step(cp::State* state) override;
  virtual void onGameEnd(cp::State* state) override;
  // In order to be used as a src/model/micromodel by squatcombat
  virtual void forward(cp::State* state) override;
  virtual cp::MicroAction decode(cp::Unit* unit) override;

 protected:
  struct Line {
    cp::Unit const* unit;
    cp::Position p1;
    cp::Position p2;
    cp::tc::BW::Color color;
  };

  std::shared_ptr<cp::UPCTuple> convertAction(
      cp::State* state,
      cp::Unit* ally,
      std::vector<cp::Unit*> enemies,
      unsigned int action,
      cp::Vec2 actionVec);
  virtual void act(cp::State* state);
  void addLine(
      cp::Unit const* unit,
      cp::Position const& p2,
      cp::tc::BW::Color color) {
    if (illustrate_) {
      lines_.push_back({unit, {}, p2, color});
    }
  }
  void addLine(
      cp::Position const& p1,
      cp::Position const& p2,
      cp::tc::BW::Color color) {
    if (illustrate_) {
      lines_.push_back({nullptr, p1, p2, color});
    }
  }

  void plotHeatmaps(cp::State* state, ag::Variant output);
  void trainerStep(cp::State* state, bool isFinal);
  void illustrate(cp::State* state);
  void updateHeatMapToVisdom();
  std::shared_ptr<cp::UPCTuple> actionToUPC(
      PFMicroActionModel::PFMicroAction& action,
      cp::State* state);
  std::shared_ptr<MicroFeaturizer> featurizer_;

  std::vector<Line> lines_;
  std::map<std::string, torch::Tensor> heatmap_;

  std::optional<ag::Variant> lastFeatures_;
  std::optional<ag::Variant> lastModelOut_;
  std::unordered_map<cp::Unit*, cp::MicroAction> actionPerUnit_;
  std::unordered_map<cp::Unit*, unsigned long> unitActionValidUntil_;
  std::uniform_int_distribution<int> actionLastingTimeDist_ =
      std::uniform_int_distribution<int>(3, 7);

 protected:
  bool illustrate_{false};
  bool generateHeatmaps_{false};
  unsigned long episodeStartFrame_ = 0;
  unsigned long lastForwardFrame_ = 0;
  bool started_ = false;
  unsigned long idxFrames_ = 0;
  std::shared_ptr<cp::Reward> reward_;
  std::unordered_map<cp::Unit*, cp::Unit*> attacks_;
};

std::shared_ptr<MicroModule> findMicroModule(
    std::shared_ptr<cp::BasePlayer> bot);

} // namespace microbattles
