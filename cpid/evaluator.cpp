/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "evaluator.h"

#include "batcher.h"
#include "sampler.h"

namespace cpid {

Evaluator::Evaluator(
    ag::Container model,
    std::unique_ptr<BaseSampler> sampler,
    size_t batchSize,
    ForwardFunction func)
    : Trainer(model, nullptr, std::move(sampler)),
      batchSize_(batchSize),
      forwardFunction_(func) {
  setTrain(false);
}

void Evaluator::stepEpisode(
    GameUID const& gameUID,
    EpisodeKey const& key,
    ReplayBuffer::Episode& /*gen_episode*/) {
  std::unique_lock<std::shared_timed_mutex> lock(insertionMutex_);
  newGames_.emplace_back(std::make_pair(gameUID, key));
}

bool Evaluator::update() {
  std::lock_guard<std::mutex> updateLock(updateMutex_);
  {
    std::shared_lock<std::shared_timed_mutex> lock(insertionMutex_);
    if (newGames_.size() < batchSize_) {
      if (gamesStarted_ < batchSize_) {
        batchBarrier_.notify_all();
      }
      return false;
    } else if (gamesStarted_ > batchSize_) {
      LOG(FATAL) << "We have too many games playing/played"
                 << " gamesStarted_ = " << gamesStarted_;
    }
  }

  float meanBatchReward = 0.0;
  for (size_t b = 0; b < batchSize_; ++b) {
    std::vector<RewardBufferFrame const*> episode;
    GameUID gameUID;
    EpisodeKey key;
    {
      std::unique_lock<std::shared_timed_mutex> lock(insertionMutex_);
      gameUID = newGames_.back().first;
      key = newGames_.back().second;
      episode = cast<RewardBufferFrame>(replayer_.get(gameUID, key));
      newGames_.pop_back();
    }
    for (size_t i = 0; i < episode.size(); ++i) {
      meanBatchReward += episode[i]->reward;
    }
  }
  meanBatchReward /= batchSize_;
  if (metricsContext_) {
    metricsContext_->pushEvent("evaluator:mean_batch_reward", meanBatchReward);
    metricsContext_->incCounter("evaluations");
  }
  std::lock_guard<std::shared_timed_mutex> mapLock(activeMapMutex_);
  if (!actives_.empty()) {
    LOG(FATAL) << "Somehow we have games at the end of the evaluation!";
  }
  std::shared_lock<std::shared_timed_mutex> insertLock(insertionMutex_);
  newGames_.clear();
  replayer_.clear();
  gamesStarted_ = 0;
  batchBarrier_.notify_all();
  return true;
}

Trainer::EpisodeHandle Evaluator::startEpisode() {
  using namespace std::chrono_literals;
  std::unique_lock<std::mutex> updateLock(updateMutex_);
  auto handle = Trainer::startEpisode();
  if (handle) {
    gamesStarted_++;
  }
  return handle;
}

void Evaluator::forceStopEpisode(EpisodeHandle const& handle) {
  std::unique_lock<std::mutex> updateLock(updateMutex_);
  if (isActive(handle) && gamesStarted_ > 0) {
    gamesStarted_--;
  }
  Trainer::forceStopEpisode(handle);
}

ag::Variant Evaluator::forward(ag::Variant inp, EpisodeHandle const& handle) {
  MetricsContext::Timer forwardTimer(
      metricsContext_, "evaluator:forward", kFwdMetricsSubsampling);
  return ag::Variant(forwardFunction_(inp, handle));
}

void Evaluator::reset() {
  Trainer::reset();
  std::unique_lock<std::mutex> updateLock(updateMutex_);
  gamesStarted_ = 0;
  batchBarrier_.notify_all();
}

std::shared_ptr<ReplayBufferFrame> Evaluator::makeFrame(
    ag::Variant /*trainerOutput*/,
    ag::Variant /*state*/,
    float reward) {
  return std::make_shared<RewardBufferFrame>(reward);
}
} // namespace cpid
