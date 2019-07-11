/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "synctrainer.h"

namespace cpid {

/**
 * Implements a n-step Advantage Actor-critic algorithm.
 * It supports settings where one samples multiple actions independently at each
 * frame (useful for multi-agent-like settings)
 * The contract is the following. We expect models to output a map with the
 * following keys:
 *   - "V" this is the value function of the current state. For one frame, it
 *         has dimension [1]
 *   - "Pi" this represents the policy. If n actions were taken in this frame,
 * amongst m possible choices, then for one frame, this tensor has dimension nxm
 */
class A2C : public SyncTrainer {
 public:
  A2C(ag::Container model,
      ag::Optimizer optim,
      std::unique_ptr<BaseSampler> sampler,
      std::unique_ptr<AsyncBatcher> batcher,
      int returnsLength,
      int updateFreq,
      int trainerBatchSize,
      float discount = 0.99,
      float ratio_clamp = 10.,
      float entropy_ratio = 0.01,
      float policy_ratio = 1.,
      bool overlappingUpdates = true,
      bool gpuMemoryEfficient = false,
      bool reduceGradients = true,
      float maxGradientNorm = -1);

  void setPolicyRatio(float pr);

 protected:
  virtual torch::Tensor computePolicyLoss(
      std::shared_ptr<BatchedFrame> currentFrame,
      torch::Tensor advantage,
      int batchSize);

  void doUpdate(
      const std::vector<std::shared_ptr<SyncFrame>>& seq,
      torch::Tensor terminal) override;

  torch::Tensor replicateAdvantage(
      std::vector<int64_t>& polSizes,
      torch::Tensor const& pgWeights,
      torch::Tensor const& currentPolicy,
      ag::Variant const& currentOut);

  float discount_ = 0.999;
  float ratio_clamp_ = 10.;
  float entropy_ratio_ = 0.01;
  float policy_ratio_ = 1.;
};

class ContinuousA2C : public A2C {
 public:
  ContinuousA2C(
      ag::Container model,
      ag::Optimizer optim,
      std::unique_ptr<BaseSampler> sampler,
      std::unique_ptr<AsyncBatcher> batcher,
      int returnsLength,
      int updateFreq,
      int trainerBatchSize,
      float discount = 0.99,
      float ratio_clamp = 10.,
      float entropy_ratio = 0.01,
      float policy_ratio = 1,
      bool overlappingUpdates = true,
      bool gpuMemoryEfficient = false,
      bool reduceGradients = true,
      float maxGradientNorm = -1);

  static torch::Tensor
  computeLikelihood(torch::Tensor a, torch::Tensor mean, torch::Tensor var);

  static torch::Tensor
  computeLogLikelihood(torch::Tensor a, torch::Tensor mean, torch::Tensor var);

 protected:
  torch::Tensor computePolicyLoss(
      std::shared_ptr<BatchedFrame> currentFrame,
      torch::Tensor advantage,
      int batchSize) override;
};

} // namespace cpid
