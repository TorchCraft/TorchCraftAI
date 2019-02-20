/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "metrics.h"
#include "trainer.h"
#include <shared_mutex>

#include "distributed.h"
#include "sampler.h"

namespace cpid {

class Evaluator : public Trainer {
 protected:
  size_t batchSize_;

  size_t gamesStarted_ = 0;
  std::condition_variable batchBarrier_;

  std::mutex updateMutex_;
  std::shared_timed_mutex insertionMutex_;
  std::deque<std::pair<GameUID, EpisodeKey>> newGames_;

  virtual void stepEpisode(
      GameUID const&,
      EpisodeKey const&,
      ReplayBuffer::Episode&) override;

  ForwardFunction forwardFunction_;

  Evaluator(
      ag::Container model,
      std::unique_ptr<BaseSampler> sampler,
      size_t batchSize,
      ForwardFunction func);

 public:
  EpisodeHandle startEpisode() override;
  void forceStopEpisode(EpisodeHandle const&) override;
  bool update() override;
  virtual ag::Variant forward(ag::Variant inp, EpisodeHandle const&) override;
  void reset() override;
  std::shared_ptr<ReplayBufferFrame> makeFrame(
      ag::Variant /*trainerOutput*/,
      ag::Variant /*state*/,
      float reward) override;

  struct make_shared_enabler;
};

// IMPLEMENTATION DETAIL:
// This guy is just here to help Trainer make a shared pointer when the
// constructor to Evaluator is private. Nobody else can call that constructor,
// not even make_shared, but this enabler + friending Trainer helps Trainer make
// a shared_ptr<Evaluator>.
struct Evaluator::make_shared_enabler : public Evaluator {
  friend class Trainer;
  make_shared_enabler(
      ag::Container model,
      std::unique_ptr<BaseSampler> s,
      size_t n,
      ForwardFunction f)
      : Evaluator(model, std::move(s), n, f) {}
};

} // namespace cpid
