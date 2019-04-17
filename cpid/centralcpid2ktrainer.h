/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "blobpubsub.h"
#include "centraltrainer.h"
#include "episodeserver.h"

#include <shared_mutex>

namespace cpid {
class Cpid2kWorker;
namespace distributed {
class Context;
}

/**
 * A trainer that sends episodes to one or more central instances.
 *
 * This is like CentralTrainer but uses Redis for figuring out servers and
 * clients.
 */
class CentralCpid2kTrainer : public CentralTrainer {
 public:
  CentralCpid2kTrainer(
      ag::Container model,
      ag::Optimizer optim,
      std::unique_ptr<BaseSampler> sampler,
      std::unique_ptr<AsyncBatcher> batcher = nullptr,
      std::string serverRole = "train");

  virtual bool update() override;

  void updateDone();

  distributed::Context& context();
  distributed::Context& serverContext();

  int numUpdates() {
    return numUpdates_.load();
  }

 protected:
  void bcastWeights();
  void recvWeights(void const* data, size_t len, int64_t numUpdates);

  std::string serverRole_;
  std::unique_ptr<Cpid2kWorker> worker_;
  std::mutex makeClientMutex_;
  std::vector<std::string> endpoints_;

  std::shared_ptr<zmq::context_t> zmqContext_;
  // Models are pushed from server to clients
  std::shared_ptr<BlobPublisher> modelPub_;
  std::shared_ptr<BlobSubscriber> modelSub_;

  std::atomic<int64_t> numUpdates_{-1};
};

} // namespace cpid
