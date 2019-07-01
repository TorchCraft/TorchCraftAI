/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "cpid/synctrainer.h"
#include "model.h"

namespace cpid {

class GasTrainer : public SyncTrainer {
 public:
  GasTrainer(
      ag::Container model,
      ag::Optimizer optim,
      std::unique_ptr<BaseSampler> sampler,
      std::unique_ptr<AsyncBatcher> batcher,
      int returnsLength,
      int trainerBatchSize,
      float maxGradientNorm = -1,
      float discount = 0.99,
      bool overlappingUpdates = false,
      bool memoryEfficient = false);
  
  void updateTargetModel();

  void step(
      EpisodeHandle const& handle,
      std::shared_ptr<ReplayBufferFrame> v,
      bool isDone = false) override;

  float getEpsilon();
  float getLod();

  void updateBestMetric(float metric);
  float curLod_ = 0;
  float lastBestMetric_ = -999999.;
  float lastBestLod_ = 0;
  int lastBestUpdate_ = 0;

  template <class Archive>
  void save(Archive& ar) const;
  template <class Archive>
  void load(Archive& ar);

 protected:
  void doUpdate(
      const std::vector<std::shared_ptr<SyncFrame>>& seq,
      torch::Tensor terminal) override;
  std::unordered_map<GameUID, double> cumRewards_;
  float discount_ = 0.999;
  int lastUpdatedTargetT_ = 0;
  int nextThreadi_ = 0;
  std::map<std::thread::id, int> threadIdMap_;
  std::mutex threadIdNumMutex_;
  ag::Container targetModel_;

};

template <class Archive>
void GasTrainer::save(Archive& ar) const {
  ar(CEREAL_NVP(*model_));
  ar(CEREAL_NVP(optim_));
  ar(CEREAL_NVP(updateCount_));
  ar(CEREAL_NVP(curLod_));
  ar(CEREAL_NVP(lastBestMetric_));
  ar(CEREAL_NVP(lastBestUpdate_));
}

template <class Archive>
void GasTrainer::load(Archive& ar) {
  ar(CEREAL_NVP(*model_));
  auto o = std::static_pointer_cast<torch::optim::Adam>(optim_);
  VLOG(0) << "lr before load " << o->options.learning_rate();
  auto options = o->options;
  ar(CEREAL_NVP(optim_));
  o = std::static_pointer_cast<torch::optim::Adam>(optim_);
  o->options = options;
  VLOG(0) << "lr after load " << o->options.learning_rate();
  optim_->add_parameters(model_->parameters());
  ar(CEREAL_NVP(updateCount_));
  ar(CEREAL_NVP(curLod_));
  ar(CEREAL_NVP(lastBestMetric_));
  ar(CEREAL_NVP(lastBestUpdate_));
}
} // namespace cpid
