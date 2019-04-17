/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "reqrepserver.h"

#include "distributed.h"
#include "netutils.h"

#include <common/assert.h>
#include <common/rand.h>

#include <fmt/format.h>
#include <glog/logging.h>
#include <zmq.hpp>

namespace {
size_t constexpr kMaxEndpointLength = 4096;
uint32_t const kWakeupSignal = 0xFEED;
} // namespace

namespace cpid {
namespace {
size_t recvMultipart(zmq::socket_t& socket, int flags, zmq::message_t* msg) {
  size_t partsReceived = 0;
  if (socket.recv(msg, flags) == false) {
    // Timeout
    return partsReceived;
  }
  partsReceived++;
  // Read remains if any, but discard
  while (socket.getsockopt<int>(ZMQ_RCVMORE) == 1) {
    zmq::message_t discard;
    if (socket.recv(&discard, flags) == false) {
      break;
    }
    partsReceived++;
  }
  return partsReceived;
}

template <typename... Args>
size_t recvMultipart(
    zmq::socket_t& socket,
    int flags,
    zmq::message_t* msg,
    Args... args) {
  size_t partsReceived = 0;
  if (socket.recv(msg, flags) == false) {
    return partsReceived;
  }
  partsReceived++;
  if (socket.getsockopt<int>(ZMQ_RCVMORE) == 1) {
    return partsReceived +
        recvMultipart(socket, flags, std::forward<Args>(args)...);
  }
  return partsReceived;
}

} // namespace

ReqRepServer::ReqRepServer(
    CallbackFn callback,
    size_t numThreads,
    std::string endpoint)
    : callback_(callback), numThreads_(numThreads) {
  context_ = std::make_shared<zmq::context_t>();
  std::promise<std::string> endpointP;
  endpointF_ = endpointP.get_future();
  thread_ = std::thread(
      &ReqRepServer::listen, this, std::move(endpoint), std::move(endpointP));
}

ReqRepServer::~ReqRepServer() {
  {
    auto guard = std::lock_guard(contextM_);
    context_.reset();
  }
  thread_.join();
}

std::string ReqRepServer::endpoint() const {
  // Protect this with a mutex so we can call it from multiple threads
  auto guard = std::lock_guard(endpointM_);
  if (endpointF_.valid()) {
    endpoint_ = endpointF_.get();
  }
  return endpoint_;
}

void ReqRepServer::listen(
    std::string endpoint,
    std::promise<std::string>&& endpointP) {
  auto ctxLock = std::unique_lock(contextM_);
  // Create appropriate socket that will exposed to clients
  auto frontendType = zmq::socket_type::router;
  zmq::socket_t frontend(*context_.get(), frontendType);
  frontend.setsockopt(ZMQ_LINGER, 0);
  try {
    if (endpoint.empty()) {
      // Bind to local IP on random port
      auto iface = netutils::getInterfaceAddresses()[0];
      frontend.bind(fmt::format("tcp://{}:0", iface));
      endpoint.resize(kMaxEndpointLength);
      size_t epsize = endpoint.size();
      frontend.getsockopt(
          ZMQ_LAST_ENDPOINT, const_cast<char*>(endpoint.c_str()), &epsize);
      endpoint.resize(epsize - 1);
    } else {
      frontend.bind(endpoint);
    }
    VLOG(1) << "ReqRepServer bound to " << endpoint;
  } catch (...) {
    endpointP.set_exception(std::current_exception());
    return;
  }
  endpointP.set_value(endpoint);

  // Create backend socket for worker threads
  zmq::socket_t backend(*context_.get(), zmq::socket_type::dealer);
  backend.setsockopt(ZMQ_LINGER, 0);
  auto backendAddr = fmt::format("inproc://reqrep.{}", common::randId(8));
  backend.bind(backendAddr);
  ctxLock.unlock();

  // Spin up workers and run ZeroMQ proxy
  std::vector<std::thread> workers;
  for (auto i = 0U; i < numThreads_; i++) {
    workers.emplace_back(&ReqRepServer::runWorker, this, backendAddr);
  }
  try {
    // Note that zmq::proxy() will perform fair queueing of all requests, no
    // matter how long it will take to actually handle them.
    // Alternatively, one could implement manual load balancing, e.g. as
    // described in http://zguide.zeromq.org/cpp:rtreq.
    zmq::proxy(frontend, backend, nullptr);
  } catch (zmq::error_t const& ex) {
    if (zmq_errno() != ETERM) {
      // Context was not terminated -- another error?
      throw;
    }
  }

  // Clean up
  frontend.close();
  backend.close();
  for (auto& w : workers) {
    w.join();
  }
}

void ReqRepServer::runWorker(std::string const& endpoint) {
  auto clock = std::unique_lock(contextM_);
  if (context_ == nullptr) {
    return;
  }
  zmq::socket_t socket(*context_.get(), zmq::socket_type::dealer);
  socket.setsockopt(ZMQ_LINGER, int(0));
  socket.connect(endpoint);
  clock.unlock();

  bool replySent = true;
  bool terminated = false;
  zmq::message_t idC; // from client
  zmq::message_t idR; // from request

  auto reply = [&](void const* buf, size_t len) {
    while (true) {
      try {
        for (auto* id : {&idC, &idR}) {
          if (socket.send(*id, ZMQ_SNDMORE) == 0) {
            throw zmq::error_t();
          }
        }
        if (socket.send(buf, len, 0) == 0) {
          throw zmq::error_t();
        }
        VLOG(2) << fmt::format("ReqRepServer sent {} bytes as reply", len);
        replySent = true;
        return;
      } catch (zmq::error_t const& e) {
        if (zmq_errno() == ETERM) {
          terminated = true;
          return;
        }
        VLOG(0) << fmt::format(
            "ReqRepServer failed sending data; retrying ({})", e.what());
        continue;
      }
    }
  };

  zmq::message_t msg;
  while (true) {
    try {
      auto nparts = recvMultipart(socket, 0, &idC, &idR, &msg);
      if (nparts == 0) {
        continue;
      }
      if (nparts != 3) {
        VLOG(0) << fmt::format(
            "ReqRepServer got invalid request (got {} parts instead of 3)",
            nparts);
        continue;
      }
    } catch (zmq::error_t const& e) {
      if (zmq_errno() == ETERM) {
        break;
      }
      VLOG(0) << "ReqRepServer exception while waiting for message: "
              << e.what();
      continue;
    }

    VLOG(2) << "ReqRepServer received " << msg.size() << " bytes from request "
            << std::string_view(idR.data<const char>(), idR.size());
    replySent = false;
    callback_(msg.data<void>(), msg.size(), reply);
    if (terminated) {
      break;
    }
    if (!replySent) {
      LOG(WARNING) << "ReqRepServer: reply was not sent in callback";
    }
  }
}

ReqRepClient::ReqRepClient(
    size_t maxConcurrentRequests,
    std::vector<std::string> endpoints,
    std::shared_ptr<zmq::context_t> context)
    : context_(
          context == nullptr ? std::make_shared<zmq::context_t>() : context),
      maxConcurrentRequests_(maxConcurrentRequests) {
  updateEndpoints(std::move(endpoints));

  // Use endpoint instead of a wait conditions when new requests are performed.
  // This way, we can wait for either replies or new requests with a single
  // poll().
  signalEndpoint_ = fmt::format("inproc://reqrep.{}", common::randId(8));
  signalSocket_ =
      std::make_unique<zmq::socket_t>(*context_.get(), zmq::socket_type::pair);
  signalSocket_->setsockopt(ZMQ_LINGER, int(0));
  signalSocket_->connect(signalEndpoint_);

  thread_ = std::thread(&ReqRepClient::run, this);
}

ReqRepClient::~ReqRepClient() {
  stop_.store(true);
  {
    auto guard = std::lock_guard(queueM_);
    signalSocket_->send(&kWakeupSignal, sizeof(kWakeupSignal));
  }
  thread_.join();
}

std::future<std::vector<char>> ReqRepClient::request(std::vector<char> msg) {
  auto lock = std::unique_lock(queueM_);
  queue_.emplace(std::move(msg));
  signalSocket_->send(&kWakeupSignal, sizeof(kWakeupSignal));
  return queue_.back().promise.get_future();
}

bool ReqRepClient::updateEndpoints(std::vector<std::string> endpoints) {
  std::sort(endpoints.begin(), endpoints.end());
  {
    auto sg = std::shared_lock(epM_);
    if (endpoints_ == endpoints) {
      return false;
    }
  }
  auto guard = std::unique_lock(epM_);
  if (endpoints_ == endpoints) {
    return false;
  }
  endpoints_ = std::move(endpoints);
  endpointsChanged_ = true;
  if (signalSocket_) {
    signalSocket_->send(&kWakeupSignal, sizeof(kWakeupSignal));
  }
  return true;
}

void ReqRepClient::setReplyTimeoutMs(size_t timeout) {
  replyTimeoutMs_.store(timeout);
}

void ReqRepClient::setMaxRetries(size_t count) {
  maxRetries_.store(count);
}

void ReqRepClient::run() {
  using namespace std::chrono;
  zmq::socket_t queueSignal(*context_.get(), zmq::socket_type::pair);
  queueSignal.setsockopt(ZMQ_LINGER, 0);
  queueSignal.bind(signalEndpoint_);

  zmq::socket_t socket(*context_.get(), zmq::socket_type::dealer);
  auto host = netutils::getInterfaceAddresses()[0];
  auto clientId = fmt::format("{}_{}", host, common::randId(8));
  socket.setsockopt(ZMQ_IDENTITY, clientId.c_str(), clientId.length());
  socket.setsockopt(ZMQ_LINGER, 0);

  // Maintain a set of requests that have been sent already but for which
  // replies are still outstanding.
  struct Request {
    QueueItem item;
    TimePoint sentTime;
  };
  std::queue<QueueItem> resendQueue;
  std::unordered_map<std::string, Request> requests;

  // Keep endpoints in a local variable and re-establish connection whenever it
  // changed.
  std::vector<std::string> endpoints;
  {
    auto guard = std::lock_guard(epM_);
    endpoints = endpoints_;
    endpointsChanged_ = false;
  }
  for (auto const& ep : endpoints) {
    VLOG(2) << "ReqRepClient connecting to " << ep;
    try {
      socket.connect(ep);
    } catch (zmq::error_t const& ex) {
      LOG(WARNING) << fmt::format(
          "ReqRepClient cannot connect to {}: {}", ep, ex.what());
    }
  }

  bool needPoll = true;
  auto pollHandleReply = [&] {
    auto replyTimeout = milliseconds(replyTimeoutMs_.load());
    if (needPoll) {
      // Poll for new items in queue and for replies
      zmq_pollitem_t items[2];
      items[0].socket = socket;
      items[0].events = ZMQ_POLLIN;
      items[1].socket = queueSignal;
      items[1].events = ZMQ_POLLIN;
      // Wait for as long as needed to obtain a time-out for the oldest
      // request
      TimePoint firstSent = Clock::now();
      for (auto const& it : requests) {
        if (it.second.sentTime < firstSent) {
          firstSent = it.second.sentTime;
        }
      }

      int ret = 0;
      do {
        auto pollEnd = firstSent + replyTimeout;
        auto timeout = std::max(
            duration_cast<milliseconds>(pollEnd - Clock::now()),
            milliseconds(0));
        ret = zmq::poll(items, sizeof(items) / sizeof(items[0]), timeout);
      } while (ret < 0 && zmq_errno() == EINTR);

      // If got a signal for new queue items we should consume it. We'll check
      // for new requests separately.
      if (items[1].revents & ZMQ_POLLIN) {
        zmq::message_t reply;
        queueSignal.recv(&reply, ZMQ_DONTWAIT);
      }
    }

    // Get reply content
    zmq::message_t id, reply;
    auto nparts = recvMultipart(socket, ZMQ_NOBLOCK, &id, &reply);
    if (nparts == 0) {
      // No actual message, use poll() next time
      needPoll = true;
      return;
    }
    needPoll = false;
    if (nparts != 2) {
      VLOG(2) << fmt::format(
          "ReqRepClient got invalid reply (got {} parts instead of 2)", nparts);
      return;
    }

    // Fulfill promise
    std::string idstr(id.data<char const>(), id.size());
    auto it = requests.find(idstr);
    if (it != requests.end()) {
      VLOG(2) << fmt::format(
          "ReqRepClient got reply of {} bytes for request '{}'",
          reply.size(),
          idstr);
      it->second.item.promise.set_value(
          Blob(reply.data<char>(), reply.data<char>() + reply.size()));
      requests.erase(it);
    } else {
      VLOG(2) << fmt::format(
          "ReqRepClient no current request with id "
          "'{}', ignoring",
          idstr);
    }
  };

  auto requeueExpiredRequests = [&] {
    // Expire requests if needed
    auto replyTimeout = milliseconds(replyTimeoutMs_.load());
    auto maxRetries = maxRetries_.load();
    auto now = Clock::now();
    for (auto it = requests.begin(); it != requests.end();) {
      if (it->second.sentTime + replyTimeout < now) {
        VLOG(2) << fmt::format(
            "ReqRepClient timeout {} for request '{}'",
            it->second.item.retries + 1,
            it->first);
        // Push back to queue
        if (it->second.item.retries < maxRetries) {
          it->second.item.retries++;
          resendQueue.emplace(std::move(it->second.item));
        } else {
          it->second.item.promise.set_exception(std::make_exception_ptr(
              std::runtime_error("Maximum number of retries reached")));
        }
        it = requests.erase(it);
      } else {
        ++it;
      }
    }
  };

  auto sendRequests = [&](std::queue<QueueItem>& queue) {
    while (!queue.empty()) {
      if (endpoints.empty()) {
        break;
      }
      if (requests.size() >= maxConcurrentRequests_) {
        // Limit on simultaneous requests
        break;
      }

      // Each request obtains a new random ID
      auto id = common::randId(8);
      auto& item = queue.front();
      while (true) {
        try {
          if (socket.send(id.data(), id.size(), ZMQ_SNDMORE) == 0) {
            throw zmq::error_t();
          }
          if (socket.send(item.msg.data(), item.msg.size(), 0) == 0) {
            throw zmq::error_t();
          }
          VLOG(2) << fmt::format(
              "ReqRepClient sent {} bytes via request '{}'",
              item.msg.size(),
              id);
          Request req;
          req.item = std::move(item);
          req.sentTime = Clock::now();
          requests[id] = std::move(req);
          queue.pop();
          break;
        } catch (zmq::error_t const& e) {
          if (e.num() == EINTR) {
            VLOG(0) << "ReqRepClient interrupted while sending data; retrying";
            continue;
          } else {
            VLOG(0) << "ReqRepClient error sending data: " << e.what();
            break;
          }
        } catch (std::exception const& e) {
          VLOG(0) << "ReqRepClient error sending data: " << e.what();
          break;
        }
        break;
      }
    }
  };

  while (!stop_.load()) {
    {
      // Handle change in endpoints/sockets
      auto lock = std::shared_lock(epM_);
      if (endpointsChanged_) {
        // Wait for previous requests
        lock.unlock();
        while (!stop_.load() && !requests.empty()) {
          pollHandleReply();
          requeueExpiredRequests();
        }

        // No more messages in flight -- change sockets now
        for (auto const& ep : endpoints) {
          socket.disconnect(ep);
        }
        {
          auto xlock = std::unique_lock(epM_);
          endpoints = endpoints_;
          endpointsChanged_ = false;
        }
        for (auto const& ep : endpoints) {
          VLOG(2) << "ReqRepClient connecting to " << ep;
          try {
            socket.connect(ep);
          } catch (zmq::error_t const& ex) {
            LOG(WARNING) << fmt::format(
                "ReqRepClient cannot connect to {}: {}", ep, ex.what());
          }
        }
        if (endpoints.empty()) {
          LOG(WARNING) << "No endpoints set for ReqRepClient -- won't be able "
                          "to send out requests";
        }
      }
    }

    pollHandleReply();
    requeueExpiredRequests();
    sendRequests(resendQueue);
    ASSERT(resendQueue.empty());
    auto guard = std::lock_guard(queueM_);
    sendRequests(queue_);
  }
}

} // namespace cpid
