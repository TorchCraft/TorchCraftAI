/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "centraltrainer.h"

#include "batcher.h"
#include "netutils.h"
#include "sampler.h"

#include "common/rand.h"
#include "common/serialization.h"
#include "common/zstdstream.h"

#include <fmt/format.h>
#include <glog/logging.h>
#include <prettyprint/prettyprint.hpp>
#include <zmq.hpp>

namespace cpid {

namespace dist = distributed;
namespace zstd = common::zstd;
size_t constexpr kMaxEndpointLength = 4096;
int constexpr kMaxRank = 8192;

namespace detail {

std::vector<char> serializeEpisode(
    EpisodeTuple key,
    ReplayBuffer::Episode const& episode) {
  common::OMembuf buf;
  {
    zstd::ostream os(&buf);
    cereal::BinaryOutputArchive ar(os);
    ar(key.gameID);
    ar(key.episodeKey);
    ar((uint32_t)episode.size());
    for (auto const& frame : episode) {
      ar(std::static_pointer_cast<CerealizableReplayBufferFrame>(frame));
    }
  }
  return buf.takeData();
}

std::pair<EpisodeTuple, ReplayBuffer::Episode> deserializeEpisode(
    std::vector<char> const& data) {
  common::IMembuf buf(data);
  EpisodeTuple key;
  ReplayBuffer::Episode episode;
  {
    zstd::istream is(&buf);
    cereal::BinaryInputArchive ar(is);
    ar(key.gameID);
    ar(key.episodeKey);
    uint32_t size;
    ar(size);
    for (auto i = 0U; i < size; i++) {
      std::shared_ptr<CerealizableReplayBufferFrame> frame;
      ar(frame);
      episode.emplace_back(std::move(frame));
    }
  }
  return std::make_pair(key, episode);
}

struct CentralTrainerServer {
  zmq::context_t context;
  zmq::socket_t socket;
  std::string endpoint;
  std::thread th;
  std::mutex mutex;
  std::condition_variable cv;
  std::queue<std::vector<char>> q;
  std::atomic<bool> stop{false};

  CentralTrainerServer() : socket(context, zmq::socket_type::pull) {
    auto iface = netutils::getInterfaceAddresses()[0];
    socket.bind(fmt::format("tcp://{}:0", iface));

    endpoint.resize(kMaxEndpointLength);
    size_t epsize = endpoint.size();
    socket.getsockopt(
        ZMQ_LAST_ENDPOINT, const_cast<char*>(endpoint.c_str()), &epsize);
    endpoint.resize(epsize);
    VLOG_ALL(1) << "Server bound to " << endpoint;

    int timeoutMs = 100;
    socket.setsockopt(ZMQ_SNDTIMEO, &timeoutMs, sizeof(timeoutMs));
    socket.setsockopt(ZMQ_RCVTIMEO, &timeoutMs, sizeof(timeoutMs));

    th = std::thread(&CentralTrainerServer::run, this);
  }

  ~CentralTrainerServer() {
    stop.store(true);
    cv.notify_all(); // Wake up blocked dequeu() calls
    th.join();
  }

  std::pair<EpisodeTuple, ReplayBuffer::Episode> dequeue() {
    std::unique_lock<std::mutex> lock(mutex);
    if (q.empty()) {
      cv.wait_for(lock, std::chrono::milliseconds(10));
    }
    if (q.empty()) {
      return std::make_pair(EpisodeTuple{{}, {}}, ReplayBuffer::Episode());
    }
    // TODO: use-case for calling this from multiple threads?
    auto data = deserializeEpisode(q.front());
    q.pop();
    return data;
  }

  void run() {
    zmq::message_t msg;
    std::vector<char> buf;
    while (!stop.load()) {
      try {
        bool res = socket.recv(&msg);
        if (res == false) {
          // timeout
          continue;
        }
      } catch (std::exception const& e) {
        VLOG_ALL(0) << "Exception while waiting for message: " << e.what();
        continue;
      }

      auto d = msg.data<char>();
      buf.assign(d, d + msg.size());
      VLOG_ALL(2) << "Server received " << buf.size() << " bytes";

      {
        std::lock_guard<std::mutex> lock(mutex);
        q.emplace(std::move(buf));
      }
      cv.notify_all();
    }
  }
};

struct CentralTrainerClient {
  zmq::context_t context;
  std::vector<zmq::socket_t> sockets;
  std::thread th;
  std::mutex mutex;
  std::condition_variable cv;
  std::queue<std::vector<char>> q;
  bool stop = false;

  CentralTrainerClient(std::vector<std::string> const& endpoints)
      : th(&CentralTrainerClient::run, this) {
    if (endpoints.empty()) {
      throw std::runtime_error("No server endpoints available");
    }
    for (auto& endp : endpoints) {
      sockets.emplace_back(context, zmq::socket_type::push);
      VLOG_ALL(1) << "Client connecting to " << endp;
      sockets.back().connect(endp);
    }
  }

  ~CentralTrainerClient() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      stop = true;
    }
    cv.notify_all();
    th.join();
  }

  void enqueue(
      GameUID const& gameId,
      EpisodeKey const& key,
      ReplayBuffer::Episode const& episode) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      q.emplace(serializeEpisode({gameId, key}, episode));
    }
    cv.notify_all();
  }

  void run() {
    auto rengine = common::Rand::makeRandEngine<std::mt19937>();
    std::unique_lock<std::mutex> lock(mutex);
    while (!stop) {
      cv.wait(lock);
      if (q.size() == 0) {
        continue;
      }

      while (!q.empty()) {
        auto data = std::move(q.front());
        while (true) {
          try {
            auto id = rengine() % sockets.size();
            auto sent = sockets[id].send(data.data(), data.size());
            if (sent == data.size()) {
              VLOG_ALL(2) << "Client sent " << data.size()
                          << " bytes to server " << id;
              q.pop();
              break;
            }
          } catch (zmq::error_t const& e) {
            if (e.num() == EINTR) {
              VLOG_ALL(0) << "Client interrupted while sending data; retrying";
            } else {
              throw;
            }
          }
          continue; // retry
        }
      }
    }
  }
};

} // namespace detail

CentralTrainer::CentralTrainer(
    bool isServer,
    ag::Container model,
    ag::Optimizer optim,
    std::unique_ptr<BaseSampler> sampler,
    std::unique_ptr<AsyncBatcher> batcher)
    : Trainer(model, optim, std::move(sampler), std::move(batcher)) {
  if (dist::globalContext()->size >= kMaxRank) {
    throw std::runtime_error("This job is too large; increase kMaxRank?");
  }
  static int serverList[kMaxRank];
  serverList[dist::globalContext()->rank] = isServer;
  dist::allreduce(serverList, dist::globalContext()->size);

  // Start servers and collect endpoints on clients
  std::vector<std::string> endpoints;
  static char epbuf[kMaxEndpointLength];
  for (auto i = 0; i < dist::globalContext()->size; i++) {
    if (isServer && i == dist::globalContext()->rank) {
      // Start this server
      server_ = std::make_shared<detail::CentralTrainerServer>();
      snprintf(epbuf, sizeof(epbuf), "%s", server_->endpoint.c_str());
      dist::broadcast(epbuf, sizeof(epbuf), i);
    } else if (serverList[i] == 1) {
      // Else, receive endpoint
      dist::broadcast(epbuf, sizeof(epbuf), i);
      endpoints.push_back(epbuf);
    }
  }
  if (!isServer) {
    client_ = std::make_shared<detail::CentralTrainerClient>(endpoints);
  }
}

CentralTrainer::~CentralTrainer() {}

void CentralTrainer::stepEpisode(
    GameUID const& gameId,
    EpisodeKey const& key,
    ReplayBuffer::Episode& episode) {
  if (client_) {
    client_->enqueue(gameId, key, episode);
    replayer_.erase(gameId, key);
  } else {
    std::lock_guard<std::mutex> lock(newGamesMutex_);
    newGames_.emplace(EpisodeTuple{gameId, key});
  }
}

ag::Variant CentralTrainer::forward(
    ag::Variant inp,
    GameUID const& gameUID,
    EpisodeKey const& key) {
  std::shared_lock<std::shared_timed_mutex> lock(modelMutex_);
  return Trainer::forward(inp, gameUID, key);
}

bool CentralTrainer::update() {
  if (server_ == nullptr) {
    // TODO: For impala-style training, this would be a good place for barrier +
    // broadcast to receive model updates
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return false;
  }

  // Any local episodes?
  {
    std::unique_lock<std::mutex> lock(newGamesMutex_);
    while (!newGames_.empty()) {
      auto& key = newGames_.front();
      VLOG_ALL(2) << fmt::format(
          "New episode {}/{} of size {}",
          key.gameID,
          key.episodeKey,
          replayer_.get(key.gameID, key.episodeKey).size());
      lock.unlock();
      receivedEpisode(key.gameID, key.episodeKey);
      lock.lock();
      newGames_.pop();
    }
  }

  // Check for new episodes from other clients
  while (true) {
    EpisodeTuple key;
    ReplayBuffer::Episode episode;
    std::tie(key, episode) = server_->dequeue();
    if (episode.empty()) {
      break; // No more data for now
    }
    // TODO insert method for replay buffer?
    for (auto i = 0U; i < episode.size(); i++) {
      replayer_.append(
          key.gameID,
          key.episodeKey,
          std::move(episode[i]),
          i == episode.size() - 1);
    }
    VLOG_ALL(1) << fmt::format(
        "New episode {}/{} of size {}",
        key.gameID,
        key.episodeKey,
        episode.size());
    receivedEpisode(key.gameID, key.episodeKey);
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

} // namespace cpid
