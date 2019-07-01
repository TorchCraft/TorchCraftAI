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

class GasTrainerA2C : public SyncTrainer {
 public:
  GasTrainerA2C(
      ag::Container model,
      ag::Optimizer optim,
      std::unique_ptr<BaseSampler> sampler,
      std::unique_ptr<AsyncBatcher> batcher,
      int returnsLength,
      int trainerBatchSize,
      float maxGradientNorm = -1,
      float discount = 0.99,
      float valueLossCoef = 0.5,
      float entropyLossCoef = 0.01,
      bool overlappingUpdates = false,
      bool memoryEfficient = false);
  
  void step(
      EpisodeHandle const& handle,
      std::shared_ptr<ReplayBufferFrame> v,
      bool isDone = false) override;

  float getEpsilon();
  float getLod();

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

  float valueLossCoef_ = 0.5;
  float entropyLossCoef_ = 0.01;
  std::vector<torch::Tensor> lodIndices_;

  int lastUpdatedTargetT_ = 0;
  int nextThreadi_ = 0;
  std::map<std::thread::id, int> threadIdMap_;
  std::mutex threadIdNumMutex_;
  ag::Container targetModel_;

};

template <class Archive>
void GasTrainerA2C::save(Archive& ar) const {
  ar(CEREAL_NVP(*model_));
  ar(CEREAL_NVP(optim_));
  ar(CEREAL_NVP(updateCount_));
}

template <class Archive>
void GasTrainerA2C::load(Archive& ar) {
  ar(CEREAL_NVP(*model_));
  auto o = std::static_pointer_cast<torch::optim::Adam>(optim_);
  auto options = o->options;
  ar(CEREAL_NVP(optim_));
  o = std::static_pointer_cast<torch::optim::Adam>(optim_);
  o->options = options;
  optim_->add_parameters(model_->parameters());
  ar(CEREAL_NVP(updateCount_));
}
} // namespace cpid
