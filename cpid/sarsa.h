/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "synctrainer.h"

namespace cpid {

class Sarsa : public SyncTrainer {
 public:
  Sarsa(
      ag::Container model,
      ag::Optimizer optim,
      std::unique_ptr<BaseSampler> sampler,
      std::unique_ptr<AsyncBatcher> batcher,
      int returnsLength,
      int trainerBatchSize,
      float discount = 0.99,
      bool gpuMemoryEfficient = false);

 protected:
  void doUpdate(
      const std::vector<std::shared_ptr<SyncFrame>>& seq,
      torch::Tensor terminal) override;

  float discount_ = 0.999;
};
} // namespace cpid
