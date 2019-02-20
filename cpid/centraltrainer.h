/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "episodeserver.h"

#include <shared_mutex>

namespace cpid {

/**
 * A trainer that sends episodes to one or more central instances.
 *
 * In this trainer, several "server" instances will collect episode data from
 * "client" instances. Users are required to subclass this and override
 * `receivedFrames()`, which will be called on server instances whenever a new
 * sequence of frames arrives. The trainer can be used like any other trainer,
 * but ideally there should be no calls to sleep() between `update()` calls to
 * ensure fast processing of collected episode data.
 *
 * Implementation details:
 * The trainer spawns dedicated threads for servers and clients.
 * The data that goes over the network (serialized episodes) will be compressed
 * using Zstandard, so there's no need to use compression to your custom replay
 * buffer frame structure.
 * Episode (de)serialization (including (de)compression) will be performed in
 * the respective thread calling `stepEpisode()` (client) and `update()`
 * (server).
 *
 * TODO: Extend this so that it can be used in RL settings.
 */
class CentralTrainer : public Trainer {
 public:
  CentralTrainer(
      bool isServer,
      ag::Container model,
      ag::Optimizer optim,
      std::unique_ptr<BaseSampler> sampler,
      std::unique_ptr<AsyncBatcher> batcher = nullptr);
  virtual ~CentralTrainer();

  bool isServer() const {
    return server_ != nullptr;
  }

  virtual void
  stepFrame(GameUID const&, EpisodeKey const&, ReplayBuffer::Episode&) override;
  virtual void stepEpisode(
      GameUID const&,
      EpisodeKey const&,
      ReplayBuffer::Episode&) override;
  ag::Variant forward(ag::Variant inp, EpisodeHandle const&) override;
  virtual bool update() override;
  virtual std::shared_ptr<ReplayBufferFrame> makeFrame(
      ag::Variant trainerOutput,
      ag::Variant state,
      float reward) override;

  std::shared_lock<std::shared_timed_mutex> modelReadLock();
  std::unique_lock<std::shared_timed_mutex> modelWriteLock();

 protected:
  /// Callback for new episodes.
  /// This will be called from update() for locally and remotely generated
  /// episodes. The second argument is a unique ID per sequence of frames,
  /// and should be removed when we get episodes to send to the same
  /// node. This _depends_ on the deprecation of EpisodeKey
  virtual void receivedFrames(GameUID const&, std::string const&) = 0;

  /// Allows implementing trainers to send partial episodes
  /// TODO: set up synchronization so the partial episodes end up
  /// on the same node.
  virtual uint32_t getMaxBatchLength() const;
  virtual uint32_t getSendInterval() const;

 private:
  void dequeueEpisodes();

  // Each instance either has a server or client
  std::shared_ptr<EpisodeServer> server_;
  std::shared_ptr<EpisodeClient> client_;

  std::mutex newGamesMutex_;
  std::queue<EpisodeTuple> newBatches_;

  std::shared_timed_mutex modelMutex_;
  std::thread dequeueEpisodes_;
  std::atomic<bool> stop_{false};
};

} // namespace cpid
