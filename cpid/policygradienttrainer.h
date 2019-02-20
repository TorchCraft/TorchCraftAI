/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "metrics.h"
#include "sampler.h"
#include "trainer.h"
#include <autogradpp/autograd.h>

#include <queue>

namespace cpid {

struct BatchedPGReplayBufferFrame : ReplayBufferFrame {
  BatchedPGReplayBufferFrame(
      ag::Variant state,
      torch::Tensor action,
      float pAction,
      double reward)
      : state(state), action(action), pAction(pAction), reward(reward) {}

  ag::Variant state;
  torch::Tensor action;
  /// Probability of action according to the policy that was used to obtain this
  /// frame
  float pAction;
  /// Reward observed since taking previous action
  double reward;
};

/**
 * Off policy policy gradient with a critic.
 * This Trainer implements two modes:
 * - Online:
 *     It does 1 update with the given batch size per node whenever it gets an
 *     episode. Therefore, one episode will always be new and the others will
 *     be from the replay buffer. THIS MODE IS UNTESTED
 * - Offline:
 *     Many threads are assumed to generate episodes in the background, and
 *     it does updates in a seperate background thread.
 * In both modes, it will first update on new episodes at least once before
 * moving to sample from the replay buffer. If more episodes are generated than
 * it can update, it will block until the next update. When the replaybuffer of
 * episodes it has already updated over reaches maxBatchSize, it will remove
 * the oldest episode it's seen.
 *
 * Replayer format:
 *   state, action, p(action), reward
 *
 * Model output:
 *   Probability vector over actions: 1-dim Vector
 *   Critic's value estimate: Double
 */
class BatchedPGTrainer : public Trainer {
  int batchSize_;
  std::size_t maxBatchSize_;
  double gamma_;
  bool onlineUpdates_ = false;

  std::shared_timed_mutex updateMutex_;
  // Games that were not used for updating the model yet
  std::deque<std::pair<GameUID, EpisodeKey>> newGames_;
  // Games that were already used for updating the model but which are still in
  // the replay buffer. This will be kept <= maxBatchSize_; older games will be
  // removed first.
  std::queue<std::pair<GameUID, EpisodeKey>> seenGames_;
  std::mutex newGamesMutex_;
  bool enoughEpisodes_ = false;
  int episodes_ = 0;

  void updateModel();

 protected:
  void stepEpisode(GameUID const&, EpisodeKey const&, ReplayBuffer::Episode&)
      override;

 public:
  ag::Variant forward(ag::Variant inp, EpisodeHandle const&) override;
  bool update() override;
  void doOnlineUpdatesInstead();

  inline int episodes() {
    return episodes_;
  }

  BatchedPGTrainer(
      ag::Container model,
      ag::Optimizer optim,
      std::unique_ptr<BaseSampler> sampler,
      double gamma = 0.99,
      int batchSize = 10,
      std::size_t maxBatchSize = 50,
      std::unique_ptr<AsyncBatcher> batcher = nullptr);

  /**
   * Contract: the trainer output should be a map with keys: "action" for the
   * taken action "V" for the state value, and "action" for the action
   * probability
   */
  virtual std::shared_ptr<ReplayBufferFrame> makeFrame(
      ag::Variant trainerOutput,
      ag::Variant state,
      float reward) override;
  std::shared_ptr<Evaluator> makeEvaluator(
      size_t,
      std::unique_ptr<BaseSampler> sampler =
          std::make_unique<DiscreteMaxSampler>()) override;
};
} // namespace cpid
