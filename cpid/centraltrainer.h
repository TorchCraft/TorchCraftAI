/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "distributed.h"
#include "trainer.h"

#include <cereal/archives/binary.hpp>
#include <cereal/types/polymorphic.hpp>

#include <queue>
#include <shared_mutex>

namespace cpid {

/**
 * The base class for replay buffer frames that can be used with CentralTrainer.
 *
 * Subclasses need to implement a serialization function an register their
 * replay buffer frame type in the global scope with:
 * CEREAL_REGISTER_TYPE(MyReplayBufferFrame)
 */
struct CerealizableReplayBufferFrame : ReplayBufferFrame {
  virtual ~CerealizableReplayBufferFrame() = default;

  template <class Archive>
  void serialize(Archive& ar) {}
};

namespace detail {
struct CentralTrainerServer;
struct CentralTrainerClient;
} // namespace detail

/**
 * A trainer that sends episodes to one or more central instances.
 *
 * In this trainer, several "server" instances will collect episode data from
 * "client" instances. Users are required to subclass this and override
 * `receivedEpisode()`, which will be called on server instances whenever a new
 * episode arrives. The trainer can be used like any other trainer, but ideally
 * there should be no calls to sleep() between `update()` calls to ensure fast
 * processing of collected episode data.
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

  virtual void stepEpisode(
      GameUID const&,
      EpisodeKey const&,
      ReplayBuffer::Episode&) override;
  ag::Variant forward(
      ag::Variant inp,
      GameUID const& gameUID,
      EpisodeKey const& key = kDefaultEpisodeKey) override;
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
  /// episodes.
  virtual void receivedEpisode(GameUID const&, EpisodeKey const&) = 0;

 private:
  // Each instance either has a server or client
  std::shared_ptr<detail::CentralTrainerServer> server_;
  std::shared_ptr<detail::CentralTrainerClient> client_;

  // For the moment, these are just for locally generated episodes
  std::mutex newGamesMutex_;
  std::queue<EpisodeTuple> newGames_;

  std::shared_timed_mutex modelMutex_;
};

} // namespace cpid
