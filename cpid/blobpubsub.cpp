/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "blobpubsub.h"

#include "netutils.h"

#include <common/rand.h>
#include <common/serialization.h>
#include <common/zstdstream.h>

#include <fmt/format.h>
#include <glog/logging.h>
#include <zmq.hpp>

namespace {

std::string getLastEndpoint(zmq::socket_t& socket) {
  size_t constexpr kMaxEndpointLength = 4096;
  std::string ep(kMaxEndpointLength, '\0');
  size_t epsize = ep.size();
  socket.getsockopt(ZMQ_LAST_ENDPOINT, const_cast<char*>(ep.c_str()), &epsize);
  ep.resize(epsize - 1);
  return ep;
}

} // namespace

namespace cpid {

BlobPublisher::BlobPublisher(
    std::string endpoint,
    std::shared_ptr<zmq::context_t> context)
    : context_(
          context == nullptr ? std::make_shared<zmq::context_t>() : context) {
  std::promise<std::string> endpointP;
  endpointF_ = endpointP.get_future();
  thread_ = std::thread(
      &BlobPublisher::run, this, std::move(endpoint), std::move(endpointP));
}

BlobPublisher::~BlobPublisher() {
  stop_.store(true);
  dataCV_.notify_one();
  thread_.join();
}

std::string BlobPublisher::endpoint() const {
  // Protect this with a mutex so we can call it from multiple threads
  auto lock = std::lock_guard(endpointM_);
  if (endpointF_.valid()) {
    endpoint_ = endpointF_.get();
  }
  return endpoint_;
}

void BlobPublisher::publish(void const* data, size_t len, int64_t tag) {
  {
    auto lock = std::lock_guard(dataM_);
    auto const* ptr = static_cast<char const*>(data);
    data_.assign(ptr, ptr + len);
    tag_ = tag;
    dflags_ = DataFlags::HasData | DataFlags::NewData;
  }
  dataCV_.notify_one();
}

void BlobPublisher::publish(std::vector<char>&& data, int64_t tag) {
  {
    auto lock = std::lock_guard(dataM_);
    std::swap(data_, data);
    tag_ = tag;
    dflags_ = DataFlags::HasData | DataFlags::NewData;
  }
  dataCV_.notify_one();
}

void BlobPublisher::run(
    std::string endpoint,
    std::promise<std::string>&& endpointP) {
  // Offer XPUB socket to subscribers. We'll turn on verbose mode so we'll get
  // notified of subscribers.
  zmq::socket_t socket(*context_.get(), zmq::socket_type::xpub);
  socket.setsockopt(ZMQ_XPUB_VERBOSE, 1);
  try {
    if (endpoint.empty()) {
      // Bind to local IP on random port
      auto iface = netutils::getInterfaceAddresses()[0];
      socket.bind(fmt::format("tcp://{}:0", iface));
      endpoint = getLastEndpoint(socket);
    } else {
      socket.bind(endpoint);
    }
    VLOG(1) << "BlobPublisher bound to " << endpoint;
  } catch (...) {
    endpointP.set_exception(std::current_exception());
    return;
  }
  endpointP.set_value(endpoint);

  auto checkForNewSubscriber = [&] {
    size_t newSubscribers = 0;
    while (true) {
      // Check for incoming message (1 == new subscription)
      zmq::message_t msg;
      bool ret = socket.recv(&msg, ZMQ_DONTWAIT);
      if (ret == false) {
        break;
      }
      if (msg.size() >= 1 && msg.data<char>()[0] == 1) {
        newSubscribers++;
      }
      // Continue checking for new messages so that we don't freak out if a lot
      // of subscribers join at the same time.
    }
    VLOG_IF(1, newSubscribers > 0) << newSubscribers << " new subscribers";
    return newSubscribers > 0;
  };

  auto lock = std::unique_lock(dataM_);
  while (!stop_.load()) {
    // Introduce a not-too-tiny delay between checks for new subscribers so that
    // we can easily handle bulk subscriptions gracefully (and publish previous
    // data only once).
    dataCV_.wait_for(lock, std::chrono::seconds(1), [&] {
      return dflags_ & DataFlags::NewData || stop_.load();
    });

    // Publish data if we have a new subscriber or if we have actual new data.
    bool needSend = checkForNewSubscriber();
    if (dflags_ & DataFlags::NewData) {
      needSend = true;
      dflags_ = dflags_ & ~DataFlags::NewData;
    }

    if (needSend && (dflags_ & DataFlags::HasData)) {
      VLOG(3) << fmt::format(
          "Sending blob of size {} with tag {}", data_.size(), tag_);
      socket.send(&tag_, sizeof(tag_), ZMQ_SNDMORE);
      socket.send(data_.data(), data_.size());
    }
  }
}

BlobSubscriber::BlobSubscriber(
    CallbackFn callback,
    std::vector<std::string> endpoints,
    std::shared_ptr<zmq::context_t> context)
    : callback_(callback),
      context_(
          context == nullptr ? std::make_shared<zmq::context_t>() : context),
      endpoints_(std::move(endpoints)) {
  if (endpoints_.empty()) {
    throw std::runtime_error("No server endpoints available");
  }
  thread_ = std::thread(&BlobSubscriber::listen, this);
}

BlobSubscriber::~BlobSubscriber() {
  stop_.store(true);
  thread_.join();
}

void BlobSubscriber::updateEndpoints(std::vector<std::string> endpoints) {
  if (endpoints.empty()) {
    throw std::runtime_error("Can't update to empty endpoint list");
  }
  auto lock = std::lock_guard(endpointsM_);
  endpoints_ = std::move(endpoints);
  endpointsChanged_.store(true);
}

void BlobSubscriber::listen() {
  std::string endpoint;
  std::mt19937 rng = std::mt19937(std::random_device()());
  {
    auto lock = std::lock_guard(endpointsM_);
    endpoint =
        *common::select_randomly(endpoints_.begin(), endpoints_.end(), rng);
  }

  zmq::socket_t socket(*context_.get(), zmq::socket_type::sub);
  socket.setsockopt(ZMQ_RCVTIMEO, 250 /*milliseconds*/);
  socket.setsockopt(ZMQ_LINGER, 0);
  socket.setsockopt(ZMQ_RCVHWM, 4); // we send large-ish blobs around and the
                                    // default high water mark is 1000
  socket.setsockopt(ZMQ_SUBSCRIBE, nullptr, 0); // We want all messages
  socket.connect(endpoint);
  VLOG(1) << "BlobSubscriber connecting to " << endpoint;

  while (!stop_.load()) {
    if (endpointsChanged_.load()) {
      auto lock = std::lock_guard(endpointsM_);
      auto newEP =
          *common::select_randomly(endpoints_.begin(), endpoints_.end(), rng);
      if (newEP != endpoint) {
        socket.disconnect(endpoint);
        socket.connect(newEP);
        endpoint = std::move(newEP);
        VLOG(1) << "BlobSubscriber switching to " << endpoint;
      }
      endpointsChanged_.store(false);
    }

    zmq::message_t tagMsg, dataMsg;
    try {
      bool res = socket.recv(&tagMsg);
      if (res == false) {
        // timeout
        continue;
      }
      if (!socket.getsockopt<int>(ZMQ_RCVMORE)) {
        VLOG(0) << "Expected two-part message (tag, data), got just one";
        continue;
      }
      res = socket.recv(&dataMsg);
      if (res == false) {
        VLOG(0) << "Expected two-part message (tag, data), timed out reading "
                   "the second one";
        continue;
      }
      if (socket.getsockopt<int>(ZMQ_RCVMORE)) {
        VLOG(0) << "Expected two-part message (tag, data), got more";
        continue;
      }
    } catch (std::exception const& e) {
      VLOG(0) << "Exception while waiting for message: " << e.what();
      continue;
    }

    if (tagMsg.size() != sizeof(int64_t)) {
      VLOG(0) << fmt::format(
          "Unexpected tag length: {} != {}", tagMsg.size(), sizeof(int64_t));
      continue;
    }

    callback_(dataMsg.data<char>(), dataMsg.size(), tagMsg.data<int64_t>()[0]);
  }
}

} // namespace cpid
