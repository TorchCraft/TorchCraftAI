/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the LICENSE file
 * in the root directory of this source tree.
 */

#pragma once

#include "zmqbufferedproducer.h"

#include <atomic>

namespace cpid {

/** A buffered consumer that sends data via ZeroMQ.
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
 * end-points in a round-robin fashion. If producer endpoints don't accept new
 * data (because their queue is full and items are not consumed fast enough),
 * `enqueue()` will eventually block and perform retries.
 */
template <typename T>
class ZeroMQBufferedConsumer {
  using Request = std::vector<char>;
  using Reply = std::vector<char>;

 public:
  ZeroMQBufferedConsumer(
      uint8_t nthreads,
      size_t maxQueueSize,
      std::vector<std::string> endpoints,
      std::shared_ptr<zmq::context_t> context = nullptr);
  ~ZeroMQBufferedConsumer();

  void enqueue(T arg);
  void updateEndpoints(std::vector<std::string> endpoints);

 private:
  size_t const maxConcurrentRequests_;
  std::list<std::pair<Request, std::future<Reply>>> pending_;
  std::atomic<bool> stop_{false};
  ReqRepClient client_;
  std::unique_ptr<common::BufferedConsumer<Request>> bcsend_;
  std::unique_ptr<common::BufferedConsumer<T>> bcser_;
};

template <typename T>
ZeroMQBufferedConsumer<T>::ZeroMQBufferedConsumer(
    uint8_t nthreads,
    size_t maxQueueSize,
    std::vector<std::string> endpoints,
    std::shared_ptr<zmq::context_t> context)
    : maxConcurrentRequests_(std::min(maxQueueSize, size_t(64))),
      client_(maxConcurrentRequests_, endpoints, context) {
  // BufferedConsumer for sending out data. With a single thread, this will
  // simply run in the calling thread (protected by a mutex).
  bcsend_ = std::make_unique<common::BufferedConsumer<Request>>(
      0, 1, [this](Request ca) {
        // Check pending requests. We'll keep on retrying with an exponential
        // (bounded) backoff. For this implementation we're painfully reminded
        // of the rawness of C++11's thread support library: we can't attach
        // callbacks to futures (i.e. future::then()) or wait for multiple
        // futures at once.
        // This is the reason we limit the maximum queue size for ReqRepClient
        // to 64 (tuned by manual monkeying).
        int ntry = 0;
        while (pending_.size() >= maxConcurrentRequests_ && !stop_.load()) {
          if (ntry++ > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(
                int(10 * std::pow(2, std::min(ntry, 5)))));
          }

          for (auto it = pending_.begin(); it != pending_.end();) {
            auto& [req, fut] = *it;
            if (fut.wait_for(std::chrono::seconds(0)) !=
                std::future_status::ready) {
              ++it;
              continue;
            }
            Reply reply;
            try {
              reply = fut.get();
            } catch (std::exception const& ex) {
              // Something failed -- need to resend
              VLOG(1)
                  << "ZeroMQBufferedConsumer: got exception instead of reply: "
                  << ex.what();
              auto copy = req;
              *it = std::make_pair(
                  std::move(copy), client_.request(std::move(req)));
              ++it;
              continue;
            }

            // Recepient confirmed?
            if (detail::kConfirm.compare(
                    0, detail::kConfirm.size(), reply.data(), reply.size()) ==
                0) {
              it = pending_.erase(it);
            } else {
              VLOG(0) << "ZeroMQBufferedConsumer: got non-affirmative "
                         "reply of size "
                      << reply.size() << ", retrying";
              auto copy = req;
              *it = std::make_pair(
                  std::move(copy), client_.request(std::move(req)));
              ++it;
            }
          }
        }

        auto copy = ca;
        pending_.emplace_back(std::move(copy), client_.request(std::move(ca)));
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
        bcsend_->enqueue(buf.takeData());
      });
}

template <typename T>
ZeroMQBufferedConsumer<T>::~ZeroMQBufferedConsumer() {
  bcser_.reset();
  stop_.store(true);
  bcsend_.reset();
}

template <typename T>
void ZeroMQBufferedConsumer<T>::enqueue(T arg) {
  bcser_->enqueue(std::move(arg));
}

template <typename T>
void ZeroMQBufferedConsumer<T>::updateEndpoints(
    std::vector<std::string> endpoints) {
  client_.updateEndpoints(std::move(endpoints));
}

} // namespace cpid
