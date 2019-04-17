/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "cpid/blobpubsub.h"

#include <common/fsutils.h>
#include <common/rand.h>
#include <common/utils.h>

#include <glog/logging.h>

using namespace cpid;
using namespace common;

CASE("blobpublisher/api/auto_endpoint") {
  BlobPublisher pub;
  auto endpoint = pub.endpoint();
  auto parts = common::stringSplit(endpoint, ':');
  EXPECT(parts[0] == "tcp");
  int port;
  EXPECT(((port = std::stoi(parts[2])), true));
  EXPECT(port <= 65535);
  EXPECT(port >= 1024);
}

CASE("blobpublisher/api/fixed_endpoint") {
  auto socketPath = fsutils::mktemp("test.socket");
  auto guard = common::makeGuard([&] { fsutils::rmrf(socketPath); });
  auto ep = "ipc://" + socketPath;
  BlobPublisher pub(ep);
  auto endpoint = pub.endpoint();
  EXPECT(endpoint == ep);
}

CASE("blobpubsub/simple") {
  int64_t tag;
  std::mutex m;
  std::promise<std::string> msgP;
  auto recv = [&](void const* data, size_t len, int64_t t) {
    std::string s(static_cast<char const*>(data));
    EXPECT(s.size() + 1 == len); // s is null-terminated string
    std::scoped_lock l(m);
    tag = t;
    msgP.set_value(s);
  };

  auto context = std::make_shared<zmq::context_t>();
  BlobPublisher pub({}, context);
  BlobSubscriber sub(recv, {pub.endpoint()}, context);

  // publish void*/size_t
  pub.publish("hello", 6, 0xF00);
  auto s = msgP.get_future().get();
  EXPECT(s == "hello");
  EXPECT(tag == 0xF00);

  // publish std::vector<char>
  {
    std::scoped_lock l(m);
    msgP = std::promise<std::string>();
  }
  std::vector<char> d{'h', 'a', 'l', 'l', 'o', '\0'};
  pub.publish(std::move(d), 0xF01);
  s = msgP.get_future().get();
  EXPECT(s == "hallo");
  EXPECT(tag == 0xF01);

  // no traffic if there's no new data and no new subscriber.
  // XXX this depends on the timeout used in BlobPublisher::run().
  tag = 0x0;
  std::this_thread::sleep_for(std::chrono::seconds(3));
  EXPECT(tag == 0x0);

  // Change endpoint
  EXPECT_THROWS(sub.updateEndpoints({}));
  BlobPublisher pub2({}, context);
  sub.updateEndpoints({pub2.endpoint()});
  {
    std::scoped_lock l(m);
    msgP = std::promise<std::string>();
  }
  pub2.publish("bonjour", 8, 0xF02);
  s = msgP.get_future().get();
  EXPECT(s == "bonjour");
  EXPECT(tag == 0xF02);
}

CASE("blobpubsub/1toN") {
  std::vector<int64_t> tags;
  std::vector<std::string> msgs;
  std::mutex mx;
  auto recv = [&](void const* data, size_t len, int64_t t) {
    thread_local int64_t lastTag;
    if (t == lastTag) {
      return;
    }
    lastTag = t;
    auto lock = std::lock_guard(mx);
    tags.push_back(t);
    msgs.emplace_back(static_cast<char const*>(data));
    EXPECT(msgs.back().size() + 1 == len); // s is null-terminated string
  };

  auto context = std::make_shared<zmq::context_t>();
  BlobPublisher pub({}, context);
  pub.publish("hello", 6, 0xF00);

  std::vector<std::unique_ptr<BlobSubscriber>> subs;
  std::vector<std::string> ep{pub.endpoint()};
  for (auto i = 0; i < 10; i++) {
    subs.push_back(std::make_unique<BlobSubscriber>(recv, ep, context));
  }

  while (true) {
    {
      auto lock = std::lock_guard(mx);
      if (tags.size() == subs.size()) {
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  for (auto i = 0U; i < tags.size(); i++) {
    EXPECT(msgs[i] == "hello");
    EXPECT(tags[i] == 0xF00);
  }

  // Push one more message out
  {
    auto lock = std::lock_guard(mx);
    tags.clear();
    msgs.clear();
  }
  pub.publish("foobar", 7, 0xB0F);

  while (true) {
    {
      auto lock = std::lock_guard(mx);
      if (tags.size() == subs.size()) {
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  for (auto i = 0U; i < tags.size(); i++) {
    EXPECT(msgs[i] == "foobar");
    EXPECT(tags[i] == 0xB0F);
  }
}

CASE("blobpubsub/NtoM") {
  std::vector<int64_t> tags;
  std::vector<std::string> msgs;
  std::mutex mx;
  auto recv = [&](void const* data, size_t len, int64_t t) {
    thread_local int64_t lastTag;
    if (t == lastTag) {
      return;
    }
    lastTag = t;
    auto lock = std::lock_guard(mx);
    tags.push_back(t);
    msgs.emplace_back(static_cast<char const*>(data));
    EXPECT(msgs.back().size() + 1 == len); // s is null-terminated string
  };

  auto context = std::make_shared<zmq::context_t>();
  std::vector<std::unique_ptr<BlobPublisher>> pubs;
  std::vector<std::string> ep;
  for (auto i = 0; i < 2; i++) {
    pubs.push_back(std::make_unique<BlobPublisher>(std::string(), context));
    ep.push_back(pubs.back()->endpoint());
  }
  std::vector<std::unique_ptr<BlobSubscriber>> subs;
  for (auto i = 0; i < 8; i++) {
    subs.push_back(std::make_unique<BlobSubscriber>(recv, ep, context));
  }

  for (auto& p : pubs) {
    p->publish("hello", 6, 0xF00);
  }
  while (true) {
    {
      auto lock = std::lock_guard(mx);
      if (tags.size() == subs.size()) {
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  for (auto i = 0U; i < tags.size(); i++) {
    EXPECT(msgs[i] == "hello");
    EXPECT(tags[i] == 0xF00);
  }

  // Push one more message out
  {
    auto lock = std::lock_guard(mx);
    tags.clear();
    msgs.clear();
  }
  for (auto& p : pubs) {
    p->publish("foobar", 7, 0xB0F);
  }

  while (true) {
    {
      auto lock = std::lock_guard(mx);
      if (tags.size() == subs.size()) {
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  for (auto i = 0U; i < tags.size(); i++) {
    EXPECT(msgs[i] == "foobar");
    EXPECT(tags[i] == 0xB0F);
  }
}
