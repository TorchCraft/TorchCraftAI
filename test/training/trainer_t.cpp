/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <chrono>
#include <future>
#include <thread>

#include "test.h"

#include "cpid/metrics.h"
#include "cpid/trainer.h"

using std::chrono::system_clock;

std::string getUID() {
  return cpid::genGameUID();
}

CASE("trainer/genuid") {
  auto futures = std::vector<std::future<std::string>>();
  std::unordered_set<std::string> uids;
  size_t ntries = 100;

  for (size_t i = 0; i < ntries; i++) {
    futures.push_back(std::async(std::launch::async, getUID));
  }
  for (auto& f : futures) {
    uids.insert(f.get());
  }
  EXPECT(uids.size() == ntries);

  futures.clear();
  uids.clear();

  for (size_t i = 0; i < ntries; i++) {
    futures.push_back(std::async(std::launch::async, getUID));
  }
  for (auto& f : futures) {
    uids.insert(f.get());
  }
  EXPECT(uids.size() == ntries);
}

CASE("trainer/metrics/getlastevent") {
  cpid::MetricsContext ctx;
  ctx.pushEvent("event", 1.5);
  ctx.pushEvent("event", 2.5);
  ctx.pushEvent("event", 3.5);
  ctx.pushEvent("event", 4.5);
  EXPECT(ctx.getLastEvent("event").second == 4.5);
  EXPECT(ctx.getLastEventValue("event") == 4.5);

  auto lastEvents = ctx.getLastEvents("event", 3);
  EXPECT(lastEvents.size() == 3U);
  EXPECT(lastEvents[2].second == 4.5);
  EXPECT(lastEvents[1].second == 3.5);
  EXPECT(lastEvents[0].second == 2.5);

  lastEvents = ctx.getLastEvents("event", 100);
  EXPECT(lastEvents.size() == 4U);
  EXPECT(lastEvents[3].second == 4.5);
  EXPECT(lastEvents[2].second == 3.5);
  EXPECT(lastEvents[1].second == 2.5);
  EXPECT(lastEvents[0].second == 1.5);
}

CASE("trainer/metrics/counter") {
  cpid::MetricsContext ctx;
  ctx.incCounter("ctr");
  ctx.incCounter("ctr", 2.0);
  ctx.incCounter("ctr");
  EXPECT(ctx.getCounter("ctr") == 4.0);

  ctx.setCounter("ctr", 2.0);
  EXPECT(ctx.getCounter("ctr") == 2.0);
}

CASE("trainer/metrics/serialization") {
  auto ctx = std::make_shared<cpid::MetricsContext>();
  {
    cpid::MetricsContext::Timer(ctx, "timer");
    ctx->incCounter("ctr");
    ctx->incCounter("ctr");
    ctx->incCounter("ctr");

    ctx->pushEvent("event", 1.5);
    ctx->pushEvent("event", 2.5);
    ctx->pushEvent("event", 3.5);
    ctx->pushEvent("event", 4.5);

    ctx->pushEvents("events", {1.3, 1.5, 1.7});
    ctx->pushEvents("events", {2.3, 2.5, 2.7});
  }
  std::stringstream ss;
  ctx->dumpJson(ss);
  auto ctx2 = std::make_shared<cpid::MetricsContext>();
  ctx2->loadJson(ss);
  EXPECT(*ctx == *ctx2);
}
