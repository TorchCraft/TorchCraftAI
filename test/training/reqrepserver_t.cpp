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
  RecvCountServer(
      std::string endpoint = std::string(),
      std::shared_ptr<zmq::context_t> context = nullptr) {
    rrs_ = std::make_unique<ReqRepServer>(
        [this](std::vector<char>&& message, ReqRepServer::ReplyFn reply) {
          handleRequest(std::move(message), reply);
        },
        std::move(endpoint),
        std::move(context));
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

 private:
  // I've spend some effort on proper encapsulation to make TSAN happy
  std::mutex mutex_;
  std::uniform_int_distribution<> delayDist_{0, 0};
  size_t nrecv_ = 0;
  std::vector<std::vector<char>> received_;

 protected:
  virtual void handleRequest(
      std::vector<char>&& message,
      ReqRepServer::ReplyFn reply) {
    std::lock_guard<std::mutex> guard(mutex_);
    nrecv_ += message.size();
    received_.emplace_back(std::move(message));

    std::this_thread::sleep_for(
        std::chrono::milliseconds(common::Rand::sample(delayDist_)));
    reply("OK", 3);
  }

  std::unique_ptr<ReqRepServer> rrs_;
};

class IgnorantReqRepClient : public ReqRepClient {
 public:
  IgnorantReqRepClient(
      size_t maxBacklogSize,
      std::vector<std::string> endpoints,
      std::shared_ptr<zmq::context_t> context = nullptr)
      : ReqRepClient(
            [](std::vector<char>&&, void const*, size_t) {
              // Don't care
            },
            maxBacklogSize,
            std::move(endpoints),
            std::move(context)) {}
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

CASE("reqrepclient/api/bad_endpoint") {
  EXPECT_THROWS(std::make_shared<IgnorantReqRepClient>(
      16, std::vector<std::string>{"foo://bar"}));
}

CASE("reqrepclient/countbytes") {
  auto context = std::make_shared<zmq::context_t>();
  RecvCountServer s1(std::string(), context), s2(std::string(), context);
  IgnorantReqRepClient client(16, {s1.endpoint(), s2.endpoint()}, context);

  size_t nsent = 0;
  auto rengine = common::Rand::makeRandEngine<std::mt19937>();
  for (int i = 0; i < 100; i++) {
    size_t sz = 1 + (rengine() % 1000);
    std::vector<char> buf(sz, 0xFE);
    client.request(std::move(buf));
    nsent += sz;
  }

  client.waitForReplies();
  EXPECT(s1.nrecv() + s2.nrecv() == nsent);
}

CASE("reqrepclient/countbytes/slow") {
  auto context = std::make_shared<zmq::context_t>();
  RecvCountServer s1(std::string(), context), s2(std::string(), context),
      s3(std::string(), context);
  s1.setDelay(10, 250);
  s2.setDelay(10, 250);
  s3.setDelay(10, 250);
  IgnorantReqRepClient client(
      16, {s1.endpoint(), s2.endpoint(), s3.endpoint()}, context);
  client.setReplyTimeoutMs(100);

  size_t nsent = 0;
  auto rengine = common::Rand::makeRandEngine<std::mt19937>();
  for (int i = 0; i < 16; i++) {
    size_t sz = 1 + (rengine() % 1000);
    std::vector<char> buf(sz, 0xFE);
    client.request(std::move(buf));
    nsent += sz;
  }

  client.waitForReplies();
  // Servers may send responses with a delay that's not acceptable by the
  // client. The client will retry sending but the server with the delay will
  // still have received the message. In the end we will likely end up with more
  // bytes received than fed to the client.
  EXPECT(s1.nrecv() + s2.nrecv() + s3.nrecv() >= nsent);
}

CASE("reqrepclient/countbytes/dropping") {
  auto context = std::make_shared<zmq::context_t>();
  auto s1 = std::make_shared<RecvCountServer>(std::string(), context);
  IgnorantReqRepClient client(2, {s1->endpoint()}, context);
  client.setReplyTimeoutMs(10);

  // Destroy server immediately to cause the client to drop messages (due to a
  // small backlog size)
  s1 = nullptr;

  size_t nsent = 0;
  for (int i = 0; i < 8; i++) {
    auto str = std::to_string(i);
    std::vector<char> buf(str.begin(), str.end());
    client.request(std::move(buf));
    nsent++;
  }

  // Create new server. updateEndpoints() should retry all remaining messages in
  // the backlog.
  s1 = std::make_shared<RecvCountServer>(std::string(), context);
  client.updateEndpoints({s1->endpoint()});

  client.waitForReplies();

  // We should have received 3 messages -- 2 were in the client backlog and 1
  // was still in transit with an outstanding reply.
  EXPECT(s1->received().size() == 3U);
}

CASE("reqrepclient/countbytes/update_endpoints") {
  auto context = std::make_shared<zmq::context_t>();
  // Start with a slow server that will always take too long to reply
  RecvCountServer s1(std::string(), context);
  s1.setDelay(20, 40);
  IgnorantReqRepClient client(32, {s1.endpoint()}, context);
  client.setReplyTimeoutMs(10);

  size_t nsent = 0;
  auto rengine = common::Rand::makeRandEngine<std::mt19937>();
  for (int i = 0; i < 20; i++) {
    if (i == 10) {
      // Try specifying a wrong end point. We'll get an exception but the client
      // will still be usable
      EXPECT_THROWS(client.updateEndpoints({"foo://bar"}));
    }
    size_t sz = 1 + (rengine() % 1000);
    std::vector<char> buf(sz, 0xFE);
    client.request(std::move(buf));
    nsent += sz;
  }

  // Continue with two fast servers. All messages should still be in the backlog
  // and will end up at these two servers.
  RecvCountServer s2(std::string(), context), s3(std::string(), context);
  client.updateEndpoints({s2.endpoint(), s3.endpoint()});
  for (int i = 0; i < 20; i++) {
    size_t sz = 1 + (rengine() % 1000);
    std::vector<char> buf(sz, 0xFE);
    client.request(std::move(buf));
    nsent += sz;
  }

  client.waitForReplies();
  EXPECT(s2.nrecv() + s3.nrecv() == nsent);
}
