/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "reqrepserver.h"

#include "distributed.h"
#include "netutils.h"

#include <fmt/format.h>
#include <glog/logging.h>
#include <zmq.hpp>

namespace {
size_t constexpr kMaxEndpointLength = 4096;
} // namespace

namespace cpid {

ReqRepServer::ReqRepServer(
    CallbackFn callback,
    std::string endpoint,
    std::shared_ptr<zmq::context_t> context)
    : callback_(callback),
      context_(
          context == nullptr ? std::make_shared<zmq::context_t>() : context) {
  std::promise<std::string> endpointP;
  endpointF_ = endpointP.get_future();
  thread_ = std::thread(
      &ReqRepServer::listen, this, std::move(endpoint), std::move(endpointP));
}

ReqRepServer::~ReqRepServer() {
  stop_.store(true);
  thread_.join();
}

std::string ReqRepServer::endpoint() const {
  // Protect this with a mutex so we can call it from multiple threads
  std::lock_guard<std::mutex> guard(endpointM_);
  if (endpointF_.valid()) {
    endpoint_ = endpointF_.get();
  }
  return endpoint_;
}

void ReqRepServer::listen(
    std::string endpoint,
    std::promise<std::string>&& endpointP) {
  // Create socket
  zmq::socket_t socket(*context_.get(), zmq::socket_type::rep);
  try {
    if (endpoint.empty()) {
      // Bind to local IP on random port
      auto iface = netutils::getInterfaceAddresses()[0];
      socket.bind(fmt::format("tcp://{}:0", iface));
      endpoint.resize(kMaxEndpointLength);
      size_t epsize = endpoint.size();
      socket.getsockopt(
          ZMQ_LAST_ENDPOINT, const_cast<char*>(endpoint.c_str()), &epsize);
      endpoint.resize(epsize - 1);
    } else {
      socket.bind(endpoint);
    }
    VLOG_ALL(1) << "ReqRepServer bound to " << endpoint;
  } catch (...) {
    endpointP.set_exception(std::current_exception());
    return;
  }
  endpointP.set_value(endpoint);

  // Set small timeouts so that we don't hang in send() and recv() calls
  int timeoutMs = 100;
  socket.setsockopt(ZMQ_SNDTIMEO, &timeoutMs, sizeof(timeoutMs));
  socket.setsockopt(ZMQ_RCVTIMEO, &timeoutMs, sizeof(timeoutMs));

  bool replySent = true;
  auto reply = [this, &socket, &replySent](void const* buf, size_t len) {
    do {
      try {
        auto sent = socket.send(buf, len);
        if (sent == len) {
          replySent = true;
          return;
        }
      } catch (zmq::error_t const& e) {
        VLOG_ALL(0) << "ReqRepServer interrupted while sending data; retrying";
      }
    } while (!stop_.load());
  };

  zmq::message_t msg;
  std::vector<char> buf;
  while (!stop_.load()) {
    if (!replySent) {
      throw std::runtime_error("ReqRepServer: Reply was not sent in process()");
    }

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
    VLOG_ALL(2) << "ReqRepServer received " << buf.size() << " bytes";

    replySent = false;
    callback_(std::move(buf), reply);
  }
}

ReqRepClient::ReqRepClient(
    CallbackFn callback,
    size_t maxBacklogSize,
    std::vector<std::string> endpoints,
    std::shared_ptr<zmq::context_t> context)
    : callback_(callback),
      context_(
          context == nullptr ? std::make_shared<zmq::context_t>() : context),
      endpoints_(std::move(endpoints)),
      maxBacklogSize_(maxBacklogSize) {
  if (endpoints_.empty()) {
    throw std::runtime_error("No server endpoints available");
  }
  for (auto const& endp : endpoints_) {
    sockets_.emplace_back(*context_.get(), zmq::socket_type::req);
    VLOG_ALL(1) << "ReqRepClient connecting to " << endp;
    sockets_.back().setsockopt(ZMQ_LINGER, 0);
    sockets_.back().connect(endp);
  }
  available_.assign(sockets_.size(), true);
  messages_.resize(sockets_.size());
  sentTimes_.resize(sockets_.size());
}

ReqRepClient::~ReqRepClient() {}

void ReqRepClient::request(std::vector<char> msg) {
  send(std::move(msg));
  processBacklog();
}

void ReqRepClient::updateEndpoints(std::vector<std::string> endpoints) {
  if (endpoints.empty()) {
    throw std::runtime_error("No server endpoints available");
  }

  // One last chance to process the backlog of messages
  processBacklog();
  waitForReplies();

  // Open new sockets
  std::vector<zmq::socket_t> newSockets;
  for (auto const& endp : endpoints) {
    newSockets.emplace_back(*context_.get(), zmq::socket_type::req);
    VLOG_ALL(1) << "ReqRepClient connecting to " << endp;
    newSockets.back().setsockopt(ZMQ_LINGER, 0);
    newSockets.back().connect(endp);
  }

  // Every socket connected, we're good to go
  endpoints_ = std::move(endpoints);
  sockets_ = std::move(newSockets);
  socketIndex_ = 0;
  available_.assign(sockets_.size(), true);
  messages_.resize(sockets_.size());
  sentTimes_.resize(sockets_.size());

  // New chance to process backlog
  processBacklog();
}

void ReqRepClient::setReplyTimeout(std::chrono::milliseconds timeout) {
  replyTimeout_ = timeout;
}

void ReqRepClient::send(std::vector<char>&& msg) {
  // Search for next available socket, try each one no more than once
  bool sent = false;
  ssize_t attempts = sockets_.size();
  while (attempts-- > 0) {
    if (!available_[socketIndex_]) {
      // No reply yet -- try to get it
      if (!waitForReply(socketIndex_)) {
        // Move message to backlog_, we'll have to try sending this one again.
        backlog_.emplace(std::move(messages_[socketIndex_]));
      }
    }

    if (available_[socketIndex_]) {
      // Send out via this socket, retry on interrupts
      while (!sent) {
        try {
          auto nsent = sockets_[socketIndex_].send(msg.data(), msg.size());
          if (nsent == msg.size()) {
            VLOG_ALL(2) << "ReqRepClient sent " << msg.size()
                        << " bytes via socket " << socketIndex_;
            sentTimes_[socketIndex_] = Clock::now();
            std::swap(messages_[socketIndex_], msg); // Retain message content
            available_[socketIndex_] = false;
            sent = true;
          }
        } catch (zmq::error_t const& e) {
          if (e.num() == EINTR) {
            VLOG_ALL(0) << "ReqRep interrupted while sending data; retrying";
          } else {
            // This didn't work out... try another socket
            break;
          }
        }
      }
      if (sent) {
        break;
      }
    }

    // Next attempt with next socket
    socketIndex_ = (socketIndex_ + 1) % sockets_.size();
  }

  if (!sent) {
    backlog_.emplace(std::move(msg));
  }
  // Use next socket next time
  socketIndex_ = (socketIndex_ + 1) % sockets_.size();
}

void ReqRepClient::processBacklog() {
  // send() might put more messages in the backlog_. Don't wait until everything
  // has been sent out successfully but instead simply attempt to send all
  // messages in the current backlog_.
  auto n = backlog_.size();
  for (auto i = 0U; i < n; i++) {
    auto msg = std::move(backlog_.front());
    backlog_.pop();
    send(std::move(msg));
  }

  while (backlog_.size() > maxBacklogSize_) {
    LOG(WARNING) << "ReqRepClient: message backlog too large, dropping message";
    backlog_.pop();
  }
}

void ReqRepClient::waitForReplies() {
  // Here, we'll poll for all outstanding replies together, aiming to process
  // incoming replies as quickly as possible.
  while (true) {
    // Grab all sockets with outstanding replies and the sent time of the
    // least recent message (to ensure a minimum timout).
    std::vector<zmq_pollitem_t> items;
    std::vector<size_t> indices;
    TimePoint firstSent = Clock::now();
    for (size_t i = 0; i < sockets_.size(); i++) {
      if (!available_[i]) {
        indices.push_back(i);
        items.emplace_back();
        items.back().socket = sockets_[i];
        items.back().events = ZMQ_POLLIN;
        if (sentTimes_[i] < firstSent) {
          firstSent = sentTimes_[i];
        }
      }
    }

    if (items.empty()) {
      // Nothing to be done here
      break;
    }

    // Do polling
    int ret = 0;
    do {
      auto pollEnd = firstSent + replyTimeout_;
      auto timeout = std::max(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              pollEnd - Clock::now()),
          std::chrono::milliseconds(0));
      ret = zmq::poll(items, timeout);
    } while (ret < 0 && errno == EINTR);

    // Process result
    auto now = Clock::now();
    for (size_t i = 0; i < items.size(); i++) {
      auto sidx = indices[i];
      bool needsResend = false;
      if (items[i].revents & ZMQ_POLLIN) {
        // Get reply content
        try {
          zmq::message_t reply;
          bool res = sockets_[sidx].recv(&reply, ZMQ_DONTWAIT);
          if (res == false) {
            // No message there? strange -- consider this as failed.
            needsResend = true;
          } else {
            VLOG_ALL(2) << "ReqRepClient got reply of size " << reply.size()
                        << " from socket " << sidx;
            callback_(
                std::move(messages_[sidx]), reply.data<void>(), reply.size());
            available_[sidx] = true;
          }
        } catch (std::exception const& e) {
          VLOG_ALL(0) << "Exception while receiving message: " << e.what();
          needsResend = true;
        }
      } else if (sentTimes_[sidx] + replyTimeout_ < now) {
        // No reply and timeout
        needsResend = true;
      }

      if (needsResend) {
        // Move message to backlog and reconstruct socket
        backlog_.emplace(std::move(messages_[sidx]));
        sockets_[sidx] = makeSocket(endpoints_[sidx]);
        available_[sidx] = true;
      }
    }
  }
}

bool ReqRepClient::waitForReply(size_t sidx) {
  if (available_[sidx]) {
    // No pending message -- everything good
    return true;
  }

  // Poll for a new message, subject to remaining time since time of last
  // attempt to send.
  zmq_pollitem_t item;
  item.socket = sockets_[sidx];
  item.events = ZMQ_POLLIN;
  int ret = 0;
  do {
    auto pollEnd = sentTimes_[sidx] + replyTimeout_;
    auto timeout = std::max(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            pollEnd - Clock::now()),
        std::chrono::milliseconds(0));
    ret = zmq::poll(&item, 1, timeout);
  } while (ret < 0 && errno == EINTR);

  if (item.revents & ZMQ_POLLIN) {
    // Ah, there it is
    try {
      zmq::message_t reply;
      bool res = sockets_[sidx].recv(&reply, ZMQ_DONTWAIT);
      if (res == false) {
        // No message there? strange -- consider this as failed and
        // re-initialize the socket.
      } else {
        VLOG_ALL(2) << "ReqRepClient got reply of size " << reply.size()
                    << " from socket " << sidx;
        callback_(std::move(messages_[sidx]), reply.data<void>(), reply.size());
        available_[sidx] = true;
        return true;
      }
    } catch (std::exception const& e) {
      VLOG_ALL(0) << "Exception while receiving message: " << e.what();
      // Consider this as failed and re-initialize the socket.
    }
  }

  // Reconstruct socket since the REQ/REP FSM is now stuck waiting for a reply.
  sockets_[sidx] = makeSocket(endpoints_[sidx]);
  available_[sidx] = true;
  return false;
}

zmq::socket_t ReqRepClient::makeSocket(std::string const& endpoint) {
  zmq::socket_t socket(*context_.get(), zmq::socket_type::req);
  // Discard pending messages immediately if the socket is closed. We'll
  // explicitly wait for replies instead.
  socket.setsockopt(ZMQ_LINGER, int(0));
  VLOG_ALL(1) << "ReqRep reconnecting to " << endpoint;
  socket.connect(endpoint);
  return socket;
}

} // namespace cpid
