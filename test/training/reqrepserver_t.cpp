/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "cpid/reqrepserver.h"

#include <common/fsutils.h>
#include <common/rand.h>
#include <common/utils.h>

#include <glog/logging.h>
#include <zmq.hpp>

#include <vector>

using namespace cpid;
using namespace common;

namespace {

// Extend rather than inherit to prevent data races in handleRequest()
class RecvCountServer {
 public:
  RecvCountServer(std::string endpoint = std::string(), size_t numThreads = 1) {
    rrs_ = std::make_unique<ReqRepServer>(
        [this](void const* buf, size_t len, ReqRepServer::ReplyFn reply) {
          handleRequest(buf, len, reply);
        },
        numThreads,
        std::move(endpoint));
  }
  virtual ~RecvCountServer() = default;

  std::string endpoint() const {
    return rrs_->endpoint();
  }

  void setDelay(int min, int max) { // in ms
    std::lock_guard<std::mutex> guard(mutex_);
    delayDist_ = std::uniform_int_distribution<>(min, max);
  }

  size_t nrecv() {
    std::lock_guard<std::mutex> guard(mutex_);
    return nrecv_;
  }

  std::vector<std::vector<char>> received() {
    std::lock_guard<std::mutex> guard(mutex_);
    return received_;
  }

  std::unordered_map<std::thread::id, size_t> nrecvByThread() {
    std::lock_guard<std::mutex> guard(mutex_);
    return nrecvByThread_;
  }

 private:
  // I've spend some effort on proper encapsulation to make TSAN happy
  std::mutex mutex_;
  std::uniform_int_distribution<> delayDist_{0, 0};
  size_t nrecv_ = 0;
  std::vector<std::vector<char>> received_;
  std::unordered_map<std::thread::id, size_t> nrecvByThread_;

 protected:
  virtual void
  handleRequest(void const* buf, size_t len, ReqRepServer::ReplyFn reply) {
    std::lock_guard<std::mutex> guard(mutex_);
    nrecv_ += len;
    nrecvByThread_[std::this_thread::get_id()] += len;
    received_.emplace_back(
        static_cast<char const*>(buf), static_cast<char const*>(buf) + len);

    auto delay = common::Rand::sample(delayDist_);
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    reply(reinterpret_cast<void*>(&len), sizeof(size_t));
  }

  std::unique_ptr<ReqRepServer> rrs_;
};

// For this class, one thread is slower than others
class OneSlowRecvCountServer : public RecvCountServer {
 public:
  using RecvCountServer::RecvCountServer;

 protected:
  virtual void handleRequest(
      void const* buf,
      size_t len,
      ReqRepServer::ReplyFn reply) override {
    auto delay = 0;
    {
      auto guard = std::lock_guard(mutex_);
      if (delays_.find(std::this_thread::get_id()) == delays_.end()) {
        if (delays_.empty()) {
          delays_[std::this_thread::get_id()] = 20;
        } else {
          delays_[std::this_thread::get_id()] = 10;
        }
      }
      delay = delays_[std::this_thread::get_id()];
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));

    RecvCountServer::handleRequest(buf, len, reply);
  }

 private:
  std::mutex mutex_;
  std::unordered_map<std::thread::id, int> delays_;
};

} // namespace

CASE("reqrepserver/api/auto_endpoint") {
  RecvCountServer server;
  auto endpoint = server.endpoint();
  auto parts = common::stringSplit(endpoint, ':');
  EXPECT(parts[0] == "tcp");
  int port;
  EXPECT(((port = std::stoi(parts[2])), true));
  EXPECT(port <= 65535);
  EXPECT(port >= 1024);
}

CASE("reqrepserver/api/fixed_endpoint") {
  auto socketPath = fsutils::mktemp("test.socket");
  auto guard = common::makeGuard([&] { fsutils::rmrf(socketPath); });
  auto ep = "ipc://" + socketPath;
  RecvCountServer server(ep);
  auto endpoint = server.endpoint();
  EXPECT(endpoint == ep);
}

CASE("reqrepserver/api/bad_endpoint") {
  RecvCountServer server("foo://bar");
  EXPECT_THROWS(server.endpoint());
}

CASE("reqrepclient/countbytes/basic") {
  auto context = std::make_shared<zmq::context_t>();
  RecvCountServer s1, s2;
  ReqRepClient client(100, {s1.endpoint(), s2.endpoint()}, context);

  size_t nsent = 0;
  auto rengine = common::Rand::makeRandEngine<std::mt19937>();
  std::vector<std::future<std::vector<char>>> futures;
  for (int i = 0; i < 100; i++) {
    size_t sz = 1 + (rengine() % 1000);
    std::vector<char> buf(sz, 0xFE);
    auto f = client.request(std::move(buf));
    futures.push_back(std::move(f));
    nsent += sz;
  }

  for (auto& f : futures) {
    f.wait();
  }
  EXPECT(s1.nrecv() + s2.nrecv() == nsent);
  EXPECT(s1.nrecvByThread().size() == 1);
  EXPECT(s2.nrecvByThread().size() == 1);
}

CASE("reqrepclient/countbytes/mt") {
  auto context = std::make_shared<zmq::context_t>();
  RecvCountServer s1(std::string(), 4);
  ReqRepClient client(100, {s1.endpoint()}, context);

  size_t nsent = 0;
  std::vector<std::future<std::vector<char>>> futures;
  for (int i = 0; i < 100; i++) {
    size_t sz = 10;
    std::vector<char> buf(sz, 0xFE);
    auto f = client.request(std::move(buf));
    futures.push_back(std::move(f));
    nsent += sz;
  }

  for (auto& f : futures) {
    f.wait();
  }
  EXPECT(s1.nrecv() == nsent);
  auto bt = s1.nrecvByThread();
  EXPECT(bt.size() == 4);
  // Mostly fair queueing -- this turns out to be not 100% fair
  for (auto& it : bt) {
    EXPECT(it.second >= 24 * 10);
    EXPECT(it.second <= 26 * 10);
  }
}

CASE("reqrepclient/countbytes/mt2") {
  auto context = std::make_shared<zmq::context_t>();
  RecvCountServer s1(std::string(), 4);
  ReqRepClient client(100, {s1.endpoint()}, context);

  size_t nsent = 0;
  std::vector<std::future<std::vector<char>>> futures;
  for (int i = 0; i < 100; i++) {
    size_t sz = 10;
    std::vector<char> buf(sz, 0xFE);
    auto f = client.request(std::move(buf));
    futures.push_back(std::move(f));
    nsent += sz;
  }

  for (auto& f : futures) {
    f.wait();
  }
  EXPECT(s1.nrecv() == nsent);
  auto bt = s1.nrecvByThread();
  EXPECT(bt.size() == 4);
  // Mostly fair queueing -- this turns out to be not 100% fair
  for (auto& it : bt) {
    EXPECT(it.second >= 24 * 10);
    EXPECT(it.second <= 26 * 10);
  }
}

CASE("reqrepclient/countbytes/slow") {
  auto context = std::make_shared<zmq::context_t>();
  RecvCountServer s1, s2, s3;
  s1.setDelay(10, 250);
  s2.setDelay(10, 250);
  s3.setDelay(10, 250);
  // We need to limit the number of concurrent requests to 3; otherwise, the
  // client will just flood all servers with timed-out requests and they will
  // incur an increasing backlog of messages to handle.
  ReqRepClient client(
      3, {s1.endpoint(), s2.endpoint(), s3.endpoint()}, context);
  client.setReplyTimeoutMs(200);

  size_t nsent = 0;
  auto rengine = common::Rand::makeRandEngine<std::mt19937>();
  std::vector<std::future<std::vector<char>>> futures;
  for (int i = 0; i < 16; i++) {
    size_t sz = 1 + (rengine() % 1000);
    std::vector<char> buf(sz, 0xFE);
    auto f = client.request(std::move(buf));
    futures.push_back(std::move(f));
    nsent += sz;
  }

  for (auto& f : futures) {
    f.wait();
  }
  // Servers may send responses with a delay that's not acceptable by the
  // client. The client will retry sending but the server with the delay will
  // still have received the message. In the end we will likely end up with more
  // bytes received than fed to the client.
  EXPECT(s1.nrecv() + s2.nrecv() + s3.nrecv() >= nsent);
}

CASE("reqrepclient/countbytes/update_endpoints") {
  auto context = std::make_shared<zmq::context_t>();
  // Start with a slow server that will always take too long to reply
  RecvCountServer s1;
  s1.setDelay(60, 80);
  ReqRepClient client(2, {s1.endpoint()}, context);
  client.setReplyTimeoutMs(10);

  size_t nsent = 0;
  auto rengine = common::Rand::makeRandEngine<std::mt19937>();
  std::vector<std::future<std::vector<char>>> futures;
  for (int i = 0; i < 20; i++) {
    size_t sz = 1 + (rengine() % 1000);
    std::vector<char> buf(sz, 0xFE);
    auto f = client.request(std::move(buf));
    futures.push_back(std::move(f));
    nsent += sz;
  }

  // Continue with two fast servers. All messages should still be in the
  // client's queue and will end up at these two servers.
  RecvCountServer s2, s3;
  EXPECT(client.updateEndpoints({s2.endpoint(), s3.endpoint()}) == true);
  EXPECT(client.updateEndpoints({s2.endpoint(), s3.endpoint()}) == false);
  for (int i = 0; i < 20; i++) {
    size_t sz = 1 + (rengine() % 1000);
    std::vector<char> buf(sz, 0xFE);
    auto f = client.request(std::move(buf));
    futures.push_back(std::move(f));
    nsent += sz;
  }

  for (auto& f : futures) {
    f.wait();
  }
  EXPECT(s2.nrecv() + s3.nrecv() == nsent);
}

CASE("reqrepclient/countbytes/limit_retries") {
  auto context = std::make_shared<zmq::context_t>();
  // Start with a slow server that will always take too long to reply, and limit
  // the maximum number of retries. We'll expect an exception for every future.
  RecvCountServer s1;
  s1.setDelay(60, 80);
  ReqRepClient client(1, {s1.endpoint()}, context);
  client.setReplyTimeoutMs(10);
  client.setMaxRetries(5);

  size_t nsent = 0;
  auto rengine = common::Rand::makeRandEngine<std::mt19937>();
  std::vector<std::future<std::vector<char>>> futures;
  for (int i = 0; i < 20; i++) {
    size_t sz = 1 + (rengine() % 1000);
    std::vector<char> buf(sz, 0xFE);
    auto f = client.request(std::move(buf));
    futures.push_back(std::move(f));
    nsent += sz;
  }

  for (auto& f : futures) {
    f.wait();
    EXPECT_THROWS(f.get());
  }
}
