/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "zmqbufferedproducer.h"

namespace cpid {
namespace detail {

/**
 * Wraps ReqRepClient assuming perform() is called from a dedicated thread.
 * In particular, this class takes care to perform all client operations
 * (construct, send, updateEndpoints) within perform().
 */
class RRClientWrapper {
 public:
  enum class Action {
    Send,
    WaitForReplies,
    SendRetries,
  };

  RRClientWrapper(
      size_t maxBacklogSize,
      std::vector<std::string> endpoints,
      std::shared_ptr<zmq::context_t> context);

  void updateEndpoints(std::vector<std::string> endpoints);
  void perform(Action action, std::vector<char>&& msg);
  size_t numScheduledForRetry() const {
    return retries_.size();
  }

 private:
  size_t const maxBacklogSize_;
  std::vector<std::string> endpoints_;
  std::shared_ptr<zmq::context_t> context_;
  std::mutex mutex_;
  std::unique_ptr<ReqRepClient> rrc_;
  bool endpointsChanged_ = false;
  std::queue<std::vector<char>> retries_;
};
} // namespace detail

/**
 * A buffered consumer that sends data via ZeroMQ.
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
 * As in common::BufferedConsumer, you specify the number of threads and a queue
 * size. In addition, you supply a list of end points that
 * ZeroMQBufferedProducer instances have been bound to. Data will be send to
 * end-points in a round-robin fashion. If producer end points don't accept new
 * data (because their queue is full and items are not consumed fast enough),
 * `enqueue()` will eventually block and perform retries.
 */
template <typename T>
class ZeroMQBufferedConsumer {
  using RRClientWrapper = detail::RRClientWrapper;
  using ClientAction = std::pair<RRClientWrapper::Action, std::vector<char>>;

 public:
  ZeroMQBufferedConsumer(
      uint8_t nthreads,
      size_t maxQueueSize,
      std::vector<std::string> endpoints,
      std::shared_ptr<zmq::context_t> context = nullptr);

  void enqueue(T arg);
  void updateEndpoints(std::vector<std::string> endpoints);
  /// Wait for replies, send out all retries
  void flush();

 private:
  std::unique_ptr<detail::RRClientWrapper> client_;
  std::unique_ptr<common::BufferedConsumer<ClientAction>> bcsend_;
  std::unique_ptr<common::BufferedConsumer<T>> bcser_;
};

template <typename T>
ZeroMQBufferedConsumer<T>::ZeroMQBufferedConsumer(
    uint8_t nthreads,
    size_t maxQueueSize,
    std::vector<std::string> endpoints,
    std::shared_ptr<zmq::context_t> context) {
  client_ = std::make_unique<detail::RRClientWrapper>(
      maxQueueSize, std::move(endpoints), std::move(context));
  // BufferedConsumer for sending out data
  bcsend_ = std::make_unique<common::BufferedConsumer<ClientAction>>(
      1, 1, [this, maxQueueSize](ClientAction ca) {
        client_->perform(ca.first, std::move(ca.second));
        // XXX We can't queue up retries indefinitely, so let's do some busy
        // waiting with exponential backoff for retries.
        if (client_->numScheduledForRetry() > maxQueueSize) {
          auto start = std::chrono::steady_clock::now();
          int ntry = 0;
          while (client_->numScheduledForRetry() > maxQueueSize) {
            if (ntry++ > 0) {
              std::this_thread::sleep_for(std::chrono::milliseconds(
                  int(10 * std::pow(2, std::min(ntry, 5)))));
            }
            client_->perform(
                RRClientWrapper::Action::SendRetries, std::vector<char>());
            client_->perform(
                RRClientWrapper::Action::WaitForReplies, std::vector<char>());
          }
          auto elapsed = std::chrono::steady_clock::now() - start;
          auto ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(elapsed)
                  .count();
          VLOG_ALL(1) << "ZeroMQBufferedConsumer: waited " << ms
                      << "ms for retries";
        }
      });
  // BufferedConsumer for data serialization
  bcser_ = std::make_unique<common::BufferedConsumer<T>>(
      nthreads, maxQueueSize, [this](T data) {
        common::OMembuf buf;
        {
          common::zstd::ostream os(&buf);
          cereal::BinaryOutputArchive ar(os);
          ar(data);
        }
        bcsend_->enqueue(
            std::make_pair(RRClientWrapper::Action::Send, buf.takeData()));
      });
}

template <typename T>
void ZeroMQBufferedConsumer<T>::enqueue(T arg) {
  bcser_->enqueue(std::move(arg));
}

template <typename T>
void ZeroMQBufferedConsumer<T>::updateEndpoints(
    std::vector<std::string> endpoints) {
  client_->updateEndpoints(std::move(endpoints));
}

template <typename T>
void ZeroMQBufferedConsumer<T>::flush() {
  bcser_->wait();
  bcsend_->enqueue(std::make_pair(
      RRClientWrapper::Action::WaitForReplies, std::vector<char>()));
  bcsend_->enqueue(std::make_pair(
      RRClientWrapper::Action::SendRetries, std::vector<char>()));
  bcsend_->wait();
}

} // namespace cpid
