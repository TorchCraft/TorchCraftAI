/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cpid/batcher.h>
#include <cpid/metrics.h>
#include <cpid/sampler.h>
#include <cpid/trainer.h>

#include <autogradpp/autograd.h>

#include <deque>
#include <list>

namespace cpid {

struct BPGReplayBufferFrame : ReplayBufferFrame {
  BPGReplayBufferFrame(
      ag::Variant state,
      int action,
      float pAction,
      float reward)
      : state(state), action(action), pAction(pAction), reward(reward) {}

  ag::Variant state;
  int action;
  float pAction;
  float reward;
};

/**
 * A batched policy gradient trainer with entropy regularization.
 *
 * This is a simple policy gradient trainer for non-recurrent models that
 * samples a fixed number of transitions for every update. It does not support
 * models with a value head (i.e. a critic). The model output is expected to
 * contain both a distribution over actions and a corresponding mask. `eta` is
 * used to control the entropy regularization, which will account for the
 * effective number of of actions wrt the mask.
 */
class BPGTrainer : public Trainer {
  struct Transition {
    GameUID gameId;
    EpisodeKey episodeKey;
    // A transition consists of the frame at the given index *and* the next
    // frame (which contains the relevant reward).
    size_t frame;

    Transition() = default;
    Transition(GameUID gameId, EpisodeKey episodeKey, size_t frame)
        : gameId(std::move(gameId)),
          episodeKey(std::move(episodeKey)),
          frame(frame) {}
  };

 public:
  BPGTrainer(
      ag::Container model,
      ag::Optimizer optim,
      std::unique_ptr<BaseSampler> sampler,
      int batchSize,
      size_t maxBufferSize,
      double gamma = 0.99,
      double eta = -1.0,
      std::unique_ptr<AsyncBatcher> batcher = nullptr)
      : Trainer(model, optim, std::move(sampler), std::move(batcher)),
        batchSize_(batchSize),
        maxBufferSize_(maxBufferSize),
        gamma_(gamma),
        eta_(eta){};

  void stepEpisode(GameUID const&, EpisodeKey const&, ReplayBuffer::Episode&)
      override;
  ag::Variant forward(
      ag::Variant inp,
      GameUID const& gameUID,
      EpisodeKey const& key = kDefaultEpisodeKey) override;
  bool update() override;
  virtual std::shared_ptr<ReplayBufferFrame> makeFrame(
      ag::Variant trainerOutput,
      ag::Variant state,
      float reward) override;
  std::shared_ptr<Evaluator> makeEvaluator(
      size_t n,
      std::unique_ptr<BaseSampler> sampler) override;

 protected:
  int batchSize_;
  size_t maxBufferSize_;
  double gamma_;
  // entropy regularizAtion factor (negative disables it)
  double eta_;

  std::shared_timed_mutex updateMutex_;
  std::mutex newGamesMutex_;
  bool enoughTransitions_ = false;

  // Transitions that were not used for updating the model yet
  std::list<Transition> newTransitions_;
  // Transitions that were already used for updating the model but which are
  // still in the replay buffer. This will be kept <= maxBufferSize_; older
  // games will be removed first.
  std::deque<Transition> seenTransitions_;
  // Keeps track of the number of transitions that are still in
  // `newTransitions_` or `seenTransitions_` for a given episode. Episodes with
  // zero active transitions will be removed from the replay buffer.
  std::map<std::pair<GameUID, EpisodeKey>, ssize_t> numActiveTransitions_;

  virtual void updateModel();
};

} // namespace cpid
