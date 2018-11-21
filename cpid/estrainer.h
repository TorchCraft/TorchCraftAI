/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "metrics.h"
#include "policygradienttrainer.h"
#include "sampler.h"
#include <shared_mutex>

#include "distributed.h"
#include <stack>

namespace cpid {

class ESTrainer : public Trainer {
 public:
  enum RewardTransform {
    kNone = 0,
    // Transforms a vector of elements into a vector of floats
    // uniformly distributed within [-0.5,+0.5] according to their ranks.
    // Used in https://arxiv.org/pdf/1703.03864.pdf
    kRankTransform,
    // Divides by the std of the rewards.
    // Defined in https://arxiv.org/pdf/1803.07055.pdf
    kStdNormalize
  };

 protected:
  float std_;
  size_t batchSize_;

  // (generation, seed) => model mapping used to speed up forward in the active
  // local models
  std::unordered_map<std::pair<int, int64_t>, ag::Container, pairhash>
      modelCache_;
  // (GameUID, Key) => (generation, seed) for the active local models
  std::unordered_map<std::pair<GameUID, EpisodeKey>,
                     std::pair<int, int64_t>,
                     pairhash>
      gameToGenerationSeed_;
  std::shared_timed_mutex modelStorageMutex_;

  // the max number of historical models to store for the off-policy mode
  size_t historyLength_;
  // historyLength_ pairs of (generationId, model) stored in the order of
  // sequentially increasing generations (front() is the oldest, back() is
  // the newest)
  std::deque<std::pair<int, ag::Container>> modelsHistory_;
  std::shared_timed_mutex currentModelMutex_;

  bool antithetic_;
  RewardTransform transform_;

  bool onPolicy_;

  std::shared_timed_mutex insertionMutex_;
  std::deque<std::pair<GameUID, EpisodeKey>> newGames_;

  std::mutex seedQueueMutex_;
  std::vector<int64_t> seedQueue_;

  std::mutex updateMutex_;
  size_t gamesStarted_ = 0;
  bool resetThreads_ = false;
  std::condition_variable batchBarrier_;

  size_t gatherSize_;
  std::vector<float> allRewards_;
  std::vector<int> allGenerations_;
  std::vector<int64_t> allSeeds_;

  std::vector<float> rewards_;
  std::vector<int> generations_;
  std::vector<int64_t> seeds_;

  virtual void stepEpisode(
      GameUID const&,
      EpisodeKey const&,
      ReplayBuffer::Episode&) override;

  ag::Container generateModel(int generation, int64_t seed);
  void populateSeedQueue();

 public:
  ESTrainer(
      ag::Container model,
      ag::Optimizer optim,
      std::unique_ptr<BaseSampler> sampler,
      float std,
      size_t batchSize,
      size_t historyLength,
      bool antithetic,
      RewardTransform transform,
      bool onPolicy);

  ag::Container getGameModel(GameUID const& gameIUID, EpisodeKey const& key);
  void forceStopEpisode(GameUID const&, EpisodeKey const& = kDefaultEpisodeKey)
      override;
  bool startEpisode(GameUID const&, EpisodeKey const& = kDefaultEpisodeKey)
      override;
  bool update() override;
  virtual ag::Variant forward(
      ag::Variant inp,
      GameUID const& gameIUID,
      EpisodeKey const& key) override;
  std::shared_ptr<Evaluator> makeEvaluator(
      size_t n,
      std::unique_ptr<BaseSampler> sampler =
          std::make_unique<DiscreteMaxSampler>()) override;
  torch::Tensor rewardTransform(
      torch::Tensor const& rewards,
      RewardTransform transform);
  void reset() override;
  virtual std::shared_ptr<ReplayBufferFrame> makeFrame(
      ag::Variant trainerOutput,
      ag::Variant state,
      float reward) override;

  // If set to true, after successful update() worker threads would remain
  // blocked until the next update() call.
  TORCH_ARG(bool, waitUpdate) = false;
};

} // namespace cpid
