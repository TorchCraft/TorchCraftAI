/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "centralcpid2ktrainer.h"

#include "batcher.h"
#include "cpid2kworker.h"
#include "sampler.h"

#include <common/rand.h>
#include <common/str.h>

#include <fmt/format.h>
#include <glog/logging.h>
#include <prettyprint/prettyprint.hpp>
#include <zmq.hpp>

namespace cpid {
namespace dist = distributed;

namespace {

std::string assertEnv(char const* name) {
  if (char* value = std::getenv(name)) {
    return value;
  }
  throw std::runtime_error(
      fmt::format("Missing environment variable: {}", name));
}
} // namespace

CentralCpid2kTrainer::CentralCpid2kTrainer(
    ag::Container model,
    ag::Optimizer optim,
    std::unique_ptr<BaseSampler> sampler,
    std::unique_ptr<AsyncBatcher> batcher,
    std::string serverRole)
    : CentralTrainer(model, optim, std::move(sampler), std::move(batcher)),
      serverRole_(std::move(serverRole)) {
  auto id = assertEnv("CPID2K_ID");
  bool isServer = common::gmatch(id, fmt::format("?{}_*", serverRole_));
  VLOG(1) << "CentralCpid2kTrainer " << id << " starting as "
          << (isServer ? "server" : "client");
  auto info = Cpid2kWorkerInfo::withLocalIpFromEnvVars();
  info.id = id;

  zmqContext_ = std::make_shared<zmq::context_t>();
  if (isServer) {
    server_ = std::make_shared<EpisodeServer>(2, 64);
    modelPub_ = std::make_shared<BlobPublisher>(std::string(), zmqContext_);
    // XXX Obviously not robust
    info.services["episodes"] =
        std::stoi(common::stringSplit(server_->endpoint(), ':')[2]);
    info.services["updates"] =
        std::stoi(common::stringSplit(modelPub_->endpoint(), ':')[2]);
  }

  worker_ = Cpid2kWorker::fromEnvVars(info);

  if (isServer) {
    dequeueEpisodes_ =
        std::thread(&CentralCpid2kTrainer::dequeueEpisodes, this);

    VLOG(1) << "Waiting for remaining servers...";
    worker_->waitForAll(serverRole_);
    if (model_) {
      VLOG(1) << "Broadcasting model among servers...";
      while (true) {
        try {
          auto& ctx = serverContext();
          ctx.broadcast(model_);
          break;
        } catch (std::exception const& ex) {
          VLOG(0) << fmt::format("Broadcast failed: '{}', retrying", ex.what());
          worker_->discardDContext(serverRole_);
          continue;
        }
      }
      // Broadcast to clients
      numUpdates_ = 0;
      VLOG(1) << "Publishing initial weights...";
      bcastWeights();
    }
  } else {
    VLOG(1) << "Waiting for training workers...";
    worker_->waitForOne(serverRole_);

    // Set up episode client
    VLOG(1) << "Grabbing endpoints...";
    endpoints_ = worker_->serviceEndpoints("episodes");
    if (endpoints_.empty()) {
      throw std::runtime_error("No server endpoints found");
    }
    std::sort(endpoints_.begin(), endpoints_.end());
    client_ = std::make_shared<EpisodeClient>(1, 16, endpoints_, zmqContext_);

    auto ep = worker_->serviceEndpoints("updates");
    modelSub_ = std::make_shared<BlobSubscriber>(
        [this](void const* data, size_t len, int64_t tag) {
          this->recvWeights(data, len, tag);
        },
        ep,
        zmqContext_);

    // Receive initial model
    if (model_) {
      VLOG(1) << "Waiting for initial model..";
      // Not very advanced...
      while (numUpdates_ < 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  }
  VLOG(1) << "Good to go!";
}

bool CentralCpid2kTrainer::update() {
  // XXX Somewhat hacky
  done_.store(worker_->isDone() || worker_->consideredDead());

  if (server_ == nullptr) {
    // For clients, update list of server endpoints
    if (client_) {
      auto endpoints = worker_->serviceEndpoints("episodes");
      std::sort(endpoints.begin(), endpoints.end());
      if (endpoints.empty()) {
        // XXX unsafe
        client_ = nullptr;
        endpoints_.clear();
      } else if (endpoints_ != endpoints) {
        std::swap(endpoints_, endpoints);
        client_->updateEndpoints(endpoints_);
      }
    }

    // TODO Update list of model publishers?

    // TODO: For impala-style training, this would be a good place for barrier +
    // broadcast to receive model updates
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return false;
  }

  std::unique_lock<std::mutex> lock(newGamesMutex_);
  while (!newBatches_.empty()) {
    auto key = newBatches_.front();
    newBatches_.pop();
    VLOG(2) << fmt::format(
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

void CentralCpid2kTrainer::updateDone() {
  numUpdates_++;
  bcastWeights();
}

dist::Context& CentralCpid2kTrainer::context() {
  return worker_->dcontext();
}

dist::Context& CentralCpid2kTrainer::serverContext() {
  return worker_->dcontext(serverRole_, std::chrono::hours(3 * 24));
}

void CentralCpid2kTrainer::bcastWeights() {
  if (modelPub_ == nullptr) {
    return;
  }

  common::OMembuf buf;
  {
    common::zstd::ostream os(&buf);
    auto lock = modelReadLock();
    ag::save(os, model_);
  }
  modelPub_->publish(buf.takeData(), numUpdates_);
}

void CentralCpid2kTrainer::recvWeights(
    void const* data,
    size_t len,
    int64_t numUpdates) {
  if (numUpdates_ == numUpdates) {
    return;
  }

  while (!model_) {
    // This allows for us to set the model not in the constructor...
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::string_view sd(static_cast<char const*>(data), len);
  common::IMembuf buf(sd);
  common::zstd::istream is(&buf);
  try {
    auto lock = modelWriteLock();
    ag::load(is, model_);
  } catch (std::exception const& ex) {
    LOG(WARNING) << "Failed to deserialize model weights: " << ex.what();
    return;
  }

  numUpdates_ = numUpdates;
  VLOG(1) << "Received model weights " << numUpdates_;
}

} // namespace cpid
