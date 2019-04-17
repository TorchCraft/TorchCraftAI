/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <queue>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

namespace zmq {
class context_t;
class socket_t;
} // namespace zmq

namespace cpid {

/**
 * A request-reply server backed by ZeroMQ.
 *
 * This server will listen for messages in a dedicated thread and call the
 * supplied callback function for every incoming request. Note that if
 * `numThreads` is greater than one, the callback function maybe be called
 * concurrently from multiple threads.
 * The callback function will be supplied with a reply function; this function
 * *must* be called before returning. Failure to do so will return in a fatal
 * error (i.e. program abort).
 */
class ReqRepServer final {
 public:
  using ReplyFn = std::function<void(void const* buf, size_t len)>;
  using CallbackFn =
      std::function<void(void const* buf, size_t len, ReplyFn reply)>;

  /// Constructor.
  /// This instance will handle up to numThreads replies concurrently.
  /// If endpoint is an empty string, bind to local IP with automatic port
  /// selection.
  /// The callback will be called from dedicated threads.
  ReqRepServer(
      CallbackFn callback,
      size_t numThreads = 1,
      std::string endpoint = std::string());
  ~ReqRepServer();

  std::string endpoint() const;

 private:
  void listen(std::string endpoint, std::promise<std::string>&& endpointP);
  void runWorker(std::string const& endpoint);

  CallbackFn callback_;
  size_t numThreads_;
  std::shared_ptr<zmq::context_t> context_;
  std::mutex contextM_;
  mutable std::string endpoint_;
  mutable std::future<std::string> endpointF_;
  mutable std::mutex endpointM_;
  std::thread thread_;
};

/**
 * A request-reply client backed by ZeroMQ.
 *
 * This class provides a futures-based interface to the request-reply pattern.
 * You call request() and get a future fo your (future) reply. Note that
 * requests() will always happily accept the request and move into in internal
 * queue. This queue is *unbounded* -- if this is a concern you should add some
 * manual blocking logic; see ZeroMQBufferedConsumer() for an example.
 *
 * The client can be connected to multiple ReqRepServers and will send out
 * requests in a round-robin fashion. The number of concurrent replies that can
 * be sent is controlled with the `maxConcurrentRequests` parameter. There are
 * some basic robustness guarantees regarding slow or crashing servers: if a
 * server does not send a reply in time, retries will be attempted. The number
 * of retries can be limited; in this case, the future will be fulfilled with an
 * exception. The server list can be updated loss of messages.
 */
class ReqRepClient final {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = std::chrono::time_point<Clock>;
  using Blob = std::vector<char>;

  ReqRepClient(
      size_t maxConcurrentRequests,
      std::vector<std::string> endpoints,
      std::shared_ptr<zmq::context_t> context = nullptr);
  ~ReqRepClient();

  std::future<std::vector<char>> request(std::vector<char> msg);
  /// Returns true if the endpoints changed
  bool updateEndpoints(std::vector<std::string> endpoints);

  void setReplyTimeout(std::chrono::milliseconds timeout) {
    setReplyTimeoutMs(timeout.count());
  }
  void setReplyTimeoutMs(size_t timeoutMs);
  void setMaxRetries(size_t count);

 private:
  void run();

  struct QueueItem {
    Blob msg;
    std::promise<Blob> promise;
    size_t retries = 0;
    QueueItem(Blob msg) : msg(std::move(msg)) {}
    QueueItem() = default;
    QueueItem(QueueItem&&) = default;
    QueueItem& operator=(QueueItem&&) = default;
    QueueItem(QueueItem const&) = delete;
    QueueItem& operator=(QueueItem const&) = delete;
  };

  std::shared_ptr<zmq::context_t> context_;

  std::shared_mutex epM_;
  std::vector<std::string> endpoints_;
  bool endpointsChanged_ = false;

  std::mutex queueM_;
  std::queue<QueueItem> queue_;
  size_t const maxConcurrentRequests_;
  std::atomic<size_t> replyTimeoutMs_{10 * 1000};
  std::atomic<size_t> maxRetries_{std::numeric_limits<size_t>::max()};
  std::thread thread_;
  std::atomic<bool> stop_{false};
  std::string signalEndpoint_;
  std::unique_ptr<zmq::socket_t> signalSocket_;
};

} // namespace cpid
