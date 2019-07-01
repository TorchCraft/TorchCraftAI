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
#include "gameutils/microfixedscenario.h"
#include "modules.h"
#include "trainingsetup.h"
#include "upc.h"
#include "utils.h"

namespace cp = cherrypi;

namespace microbattles {
  
DECLARE_double(draw_penalty);

class MicroModule : public cp::Module, cp::MicroModel {
 public:
  bool illustrate_{false};
  bool generateHeatmaps_{false};
  long currentFrame_ = 0;
  float frameReward_ = 0.;
  float totalReward_ = 0.;
  bool started_ = false;
  float lastAllyCount_, lastEnemyCount_, lastAllyHp_, lastEnemyHp_;
  float firstAllyCount_, firstEnemyCount_, firstAllyHp_, firstEnemyHp_;
  std::shared_ptr<TrainingSetup> setup_;
  std::shared_ptr<cpid::Trainer> trainer_;
  std::shared_ptr<cp::Reward> reward_;
  cpid::EpisodeHandle handle_;
  // Keep track of unit attacks to avoid repetition
  std::unordered_map<cp::Unit*, cp::Unit*> attacks_;
  // The metrics that we want to track during training and testing
  std::map<std::string, float> numericMetrics_;
  std::map<std::string, std::map<cherrypi::UnitId, float>>
      numericMetricsByUnit_;
  std::map<std::string, std::vector<float>> vectorMetrics_;

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
  struct Circle {
    cp::Unit const* unit;
    cp::Position p;
    float r;
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
  void addCircle(cp::Unit const* unit, float r, cp::tc::BW::Color color) {
    if (illustrate_) {
      circles_.push_back({unit, {}, r, color});
    }
  }

  void plotHeatmaps(cp::State* state, ag::Variant output, int downsample=1);
  void trainerStep(cp::State* state, bool isFinal);
  void illustrate(cp::State* state);
  void updateHeatMapToVisdom();
  std::shared_ptr<cp::UPCTuple> actionToUPC(
      PFMicroActionModel::PFMicroAction& action);
  std::shared_ptr<MicroFeaturizer> featurizer_;

  std::vector<Line> lines_;
  std::vector<Circle> circles_;
  std::map<std::string, torch::Tensor> heatmap_;

  std::optional<ag::Variant> lastFeatures_;
  std::optional<ag::Variant> lastModelOut_;
  std::map<cp::Unit*, cp::MicroAction> actionPerUnit_;
};

} // namespace microbattles
