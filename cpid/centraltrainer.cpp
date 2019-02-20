/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "centraltrainer.h"

#include "batcher.h"
#include "sampler.h"

#include "common/rand.h"

#include <fmt/format.h>
#include <glog/logging.h>
#include <prettyprint/prettyprint.hpp>
#include <zmq.hpp>

namespace cpid {

namespace dist = distributed;

CentralTrainer::CentralTrainer(
    bool isServer,
    ag::Container model,
    ag::Optimizer optim,
    std::unique_ptr<BaseSampler> sampler,
    std::unique_ptr<AsyncBatcher> batcher)
    : Trainer(model, optim, std::move(sampler), std::move(batcher)) {
  std::vector<int64_t> serverList(dist::globalContext()->size);
  serverList[dist::globalContext()->rank] = isServer;
  dist::allreduce(serverList.data(), serverList.size());

  // Start servers and collect endpoints on clients
  std::vector<std::string> endpoints;
  char epbuf[1024];
  for (auto i = 0; i < dist::globalContext()->size; i++) {
    if (isServer && i == dist::globalContext()->rank) {
      // Start this server
      server_ = std::make_shared<EpisodeServer>(2, 64);
      snprintf(epbuf, sizeof(epbuf), "%s", server_->endpoint().c_str());
      dist::broadcast(epbuf, sizeof(epbuf), i);
    } else if (serverList[i] == 1) {
      // Else, receive endpoint
      dist::broadcast(epbuf, sizeof(epbuf), i);
      endpoints.push_back(epbuf);
    }
  }

  if (!isServer) {
    client_ = std::make_shared<EpisodeClient>(1, 16, endpoints);
  } else {
    dequeueEpisodes_ = std::thread(&CentralTrainer::dequeueEpisodes, this);
  }
}

CentralTrainer::~CentralTrainer() {
  stop_.store(true);
  if (server_) {
    server_->stop();
    dequeueEpisodes_.join();
  }
}

void CentralTrainer::stepFrame(
    GameUID const& gameId,
    EpisodeKey const&,
    ReplayBuffer::Episode& episode) {
  uint64_t maxSz = getMaxBatchLength();
  uint64_t sendInterval = getSendInterval();
  if (episode.size() == maxSz || episode.size() == maxSz + sendInterval ||
      replayBuffer().isDone(gameId)) {
    // Trim the episode. The point of this is that sometimes, when we
    // hit the end, we get a hanging section, so the last frame,
    // the one most important for the reward is often dropped
    // or associated with fewer frames. Therefore, we keep a bit
    // of extra padding around, and trim the front.
    if (episode.size() > maxSz) {
      std::move(episode.end() - maxSz, episode.end(), episode.begin());
      episode.resize(maxSz);
    }
    auto key =
        episode.size() < maxSz ? cpid::kDefaultEpisodeKey : common::randId(5);
    if (client_) {
      client_->enqueue(EpisodeData({gameId, key}, episode));
    } else {
      std::lock_guard<std::mutex> lock(newGamesMutex_);
      if (key != cpid::kDefaultEpisodeKey) {
        for (auto f : episode) {
          replayBuffer().append(gameId, key, f);
        }
      }
      newBatches_.emplace(EpisodeTuple{gameId, key});
    }
  }
}

void CentralTrainer::stepEpisode(
    GameUID const& gameId,
    EpisodeKey const&,
    ReplayBuffer::Episode& episode) {
  if (!isServer()) {
    replayBuffer().erase(gameId);
  }
}

ag::Variant CentralTrainer::forward(
    ag::Variant inp,
    EpisodeHandle const& handle) {
  std::shared_lock<std::shared_timed_mutex> lock(modelMutex_);
  return Trainer::forward(inp, handle);
}

bool CentralTrainer::update() {
  if (server_ == nullptr) {
    // TODO: For impala-style training, this would be a good place for barrier +
    // broadcast to receive model updates
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return false;
  }

  std::unique_lock<std::mutex> lock(newGamesMutex_);
  while (!newBatches_.empty()) {
    auto key = newBatches_.front();
    newBatches_.pop();
    VLOG_ALL(2) << fmt::format(
        "New episode {}/{} of size {}",
        key.gameID,
        key.episodeKey,
        replayer_.get(key.gameID, key.episodeKey).size());
    lock.unlock();
    receivedFrames(key.gameID, key.episodeKey);
    lock.lock();
  }
  return false;
}

std::shared_ptr<ReplayBufferFrame> CentralTrainer::makeFrame(
    ag::Variant trainerOutput,
    ag::Variant state,
    float reward) {
  throw std::runtime_error("Automatic frame construction is not implemented");
}

std::shared_lock<std::shared_timed_mutex> CentralTrainer::modelReadLock() {
  return std::shared_lock<std::shared_timed_mutex>(modelMutex_);
}

std::unique_lock<std::shared_timed_mutex> CentralTrainer::modelWriteLock() {
  return std::unique_lock<std::shared_timed_mutex>(modelMutex_);
}

void CentralTrainer::dequeueEpisodes() {
  if (server_ == nullptr) {
    throw std::runtime_error("No active server");
  }

  while (!stop_.load()) {
    auto oepd = server_->get();
    if (!oepd.has_value()) {
      break;
    }
    auto& epd = oepd.value();
    auto& key = epd.key;
    auto nframes = epd.episode.size();
    // TODO insert method for replay buffer would be nice to save some
    // locking/unlocking here.
    for (auto i = 0U; i < nframes; i++) {
      replayer_.append(
          key.gameID,
          key.episodeKey,
          std::move(epd.episode[i]),
          i + 1 == nframes);
    }
    std::lock_guard<std::mutex> lock(newGamesMutex_);
    newBatches_.emplace(EpisodeTuple{key.gameID, key.episodeKey});
  }
}

uint32_t CentralTrainer::getMaxBatchLength() const {
  return std::numeric_limits<uint32_t>::max();
}

uint32_t CentralTrainer::getSendInterval() const {
  return getMaxBatchLength();
}

} // namespace cpid
