/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "distributed.h"
#include "reqrepserver.h"

#include <common/parallel.h>
#include <common/serialization.h>
#include <common/zstdstream.h>

namespace cpid {
namespace detail {
extern std::string kConfirm;
extern std::string kDeny;
} // namespace detail

/**
 * A buffered producer that obtains data via ZeroMQ.
 *
 * The intended use-case is for this class to be used together with
 * ZeroMQBufferedConsumer to implement distributed producer-consumer setups.
 * Suppose you have an existing setup that looks like this, with sections of
 * your code producing items of type T and other sections consuming them:
 *
 *   [Producer] -> [Consumer]
 *
 * Then, assuming that items can be serialized with cereal, the
 * ZeroMQBufferedConsumer/Producer classes enable the following design:
 *
 *   [Producer] -> [ZeroMQBufferedConsumer]
 *                        |
 *                       TCP
 *                        |
 *                 [ZeroMQBufferedProducer] -> [Consumer]
 *
 * As in common::BufferedProducer you specify a number of threads in the
 * constructor which will be used to deserialize data.  Calling get() returns
 * data. Destructing the object will stop all threads.
 *
 * Make sure that you're calling get() fast enough; if you expect delays for
 * consumption set maxQueueSize accordingly. If the queue runs full the server
 * will not accept new data from the network.
 */
template <typename T>
class ZeroMQBufferedProducer {
 public:
  ZeroMQBufferedProducer(
      uint8_t nthreads,
      size_t maxQueueSize,
      std::string endpoint = std::string());
  ~ZeroMQBufferedProducer();

  std::optional<T> get();
  std::string endpoint() const {
    return rrs_->endpoint();
  }
  void stop();

 protected:
  void handleRequest(void const* buf, size_t len, ReqRepServer::ReplyFn reply);

 private:
  std::optional<T> produce();

  std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<std::vector<char>> queue_;
  size_t const maxInQueue_;
  std::atomic<bool> stop_{false};
  std::unique_ptr<common::BufferedProducer<T>> bprod_;
  std::unique_ptr<ReqRepServer> rrs_;
};

template <typename T>
ZeroMQBufferedProducer<T>::ZeroMQBufferedProducer(
    uint8_t nthreads,
    size_t maxQueueSize,
    std::string endpoint)
    : maxInQueue_(maxQueueSize) {
  bprod_ = std::make_unique<common::BufferedProducer<T>>(
      nthreads, maxQueueSize, [this] { return produce(); });
  rrs_ = std::make_unique<ReqRepServer>(
      [this](void const* buf, size_t len, ReqRepServer::ReplyFn reply) {
        handleRequest(buf, len, reply);
      },
      1,
      std::move(endpoint));
}

template <typename T>
ZeroMQBufferedProducer<T>::~ZeroMQBufferedProducer() {
  stop();
}

template <typename T>
std::optional<T> ZeroMQBufferedProducer<T>::get() {
  return bprod_->get();
}

template <typename T>
void ZeroMQBufferedProducer<T>::stop() {
  stop_.store(true);
  cv_.notify_all();
}

template <typename T>
void ZeroMQBufferedProducer<T>::handleRequest(
    void const* buf,
    size_t len,
    ReqRepServer::ReplyFn reply) {
  VLOG(2) << "ZeroMQBufferedProducer: received " << len << " bytes";
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= maxInQueue_) {
      VLOG(0) << "ZeroMQBufferedProducer: queue is full, cannot accept message";
      reply(detail::kDeny.c_str(), detail::kDeny.size());
      return;
    } else if (queue_.size() > 0) {
      VLOG(1) << "ZeroMQBufferedProducer: queue size " << queue_.size();
    }
    // Place in queue
    queue_.emplace(
        static_cast<char const*>(buf), static_cast<char const*>(buf) + len);
  }

  // Notify client that we received the message
  reply(detail::kConfirm.c_str(), detail::kConfirm.size());
  cv_.notify_one();
}

template <typename T>
std::optional<T> ZeroMQBufferedProducer<T>::produce() {
  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait(lock, [&] { return stop_ || !queue_.empty(); });
  if (stop_) {
    return {};
  }

  auto data = std::move(queue_.front());
  queue_.pop();
  lock.unlock();

  common::IMembuf buf(data);
  common::zstd::istream is(&buf);
  cereal::BinaryInputArchive ar(is);
  T item;
  ar(item);
  return item;
}

} // namespace cpid
