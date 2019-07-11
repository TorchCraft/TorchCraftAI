/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "trainer.h"
#include "zmqbufferedconsumer.h"
#include "zmqbufferedproducer.h"

#include <cereal/archives/binary.hpp>

namespace zmq {
class context_t;
class socket_t;
} // namespace zmq

namespace cpid {

struct EpisodeData {
  EpisodeTuple key;
  ReplayBuffer::Episode episode;

  EpisodeData() = default;
  EpisodeData(EpisodeTuple key, ReplayBuffer::Episode const& episode)
      : key(std::move(key)), episode(episode) {}
  EpisodeData(EpisodeTuple key, ReplayBuffer::Episode&& episode)
      : key(std::move(key)), episode(std::move(episode)) {}

  template <typename Archive>
  void serialize(Archive& ar) {
    ar(key.gameID);
    ar(key.episodeKey);
    ar(episode);
  }
};

using EpisodeServer = ZeroMQBufferedProducer<EpisodeData>;
using EpisodeClient = ZeroMQBufferedConsumer<EpisodeData>;

} // namespace cpid
