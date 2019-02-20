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
#include <string>
#include <thread>
#include <vector>

namespace zmq {
class context_t;
class socket_t;
} // namespace zmq

namespace cpid {

/**
 * Server for ZeroMQ REQ-REP pattern.
 *
 * This server will listen for messages in a dedicated thread. The callback
 * function supplied in the constructor will be called for every message that is
 * receveid. Users are expected to call the supplied reply function within
 * handleRequest() -- if they don't, they'll be reminded with a runtime_error.
 */
class ReqRepServer final {
 public:
  using ReplyFn = std::function<void(void const* buf, size_t len)>;
  using CallbackFn =
      std::function<void(std::vector<char>&& message, ReplyFn reply)>;

  /// Constructor.
  /// If endpoint is an empty string, bind to local IP with automatic port
  /// selection.
  /// The callback will be called from a dedicated thread -- it should not take
  /// a long time. Make sure to call reply() from it.
  ReqRepServer(
      CallbackFn callback,
      std::string endpoint = std::string(),
      std::shared_ptr<zmq::context_t> context = nullptr);
  ~ReqRepServer();

  std::string endpoint() const;

 private:
  void listen(std::string endpoint, std::promise<std::string>&& endpointP);

  CallbackFn callback_;
  std::shared_ptr<zmq::context_t> context_;
  mutable std::string endpoint_;
  mutable std::future<std::string> endpointF_;
  mutable std::mutex endpointM_;
  std::thread thread_;
  std::atomic<bool> stop_{false};
};

/**
 * Client for ZeroMQ REQ-REP pattern.
 *
 * This client can be connected to multiple ReqRepServers and will send out
 * requests in a round-robin fashion.  It provides some basic robustness
 * regarding slow or crashing servers by implementing the ZeroMQ Lazy Pirate
 * pattern: if the server does not send a reply in time, retries will be
 * attempted. Once a reply has been received, the `handleReply()` function will
 * be called (to be provided by subclasses). The server list can be updated
 * without loss of messages.
 *
 * The client keeps a backlog of requests that could not be delivered yet and
 * for which delivery should be attempted at a later point. The size of this
 * backlog has to be specified during construction.
 *
 * A few words regarding retries and the callback function: since this class
 * provides a purely synchronous interface, send and receive operations are only
 * performed during `request()`, `waitForReplies()` and `updateEndpoints()`
 * calls. These are the functions that might result in a call to the supplied
 * callback function.
 */
class ReqRepClient {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = std::chrono::time_point<Clock>;
  using CallbackFn = std::function<
      void(std::vector<char>&& request, void const* reply, size_t len)>;

  ReqRepClient(
      CallbackFn callback,
      size_t maxBacklogSize,
      std::vector<std::string> endpoints,
      std::shared_ptr<zmq::context_t> context = nullptr);
  virtual ~ReqRepClient();

  void request(std::vector<char> msg);
  void waitForReplies();
  void updateEndpoints(std::vector<std::string> endpoints);

  void setReplyTimeout(std::chrono::milliseconds timeout);
  void setReplyTimeoutMs(size_t timeoutMs) {
    setReplyTimeout(std::chrono::milliseconds(timeoutMs));
  }

 private:
  void send(std::vector<char>&& msg);
  void processBacklog();

  bool waitForReply(size_t sidx);
  void processAvailableReply(size_t sidx);
  zmq::socket_t makeSocket(std::string const& endpoint);

  CallbackFn callback_;
  std::shared_ptr<zmq::context_t> context_;
  std::vector<std::string> endpoints_;
  std::vector<zmq::socket_t> sockets_;
  // One message and one flag for each socket
  std::vector<std::vector<char>> messages_;
  std::vector<bool> available_;
  std::vector<TimePoint> sentTimes_;
  size_t socketIndex_ = 0;
  // Backlog of messages that still need to be sent
  std::queue<std::vector<char>> backlog_;
  size_t const maxBacklogSize_;
  std::chrono::milliseconds replyTimeout_{10000};
};

} // namespace cpid
