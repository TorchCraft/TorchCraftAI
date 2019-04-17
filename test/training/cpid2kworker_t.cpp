/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * NOTE: each of these tests requires an empy redis instance available at
 * -redis_host and -redis_port.
 */

#include "test.h"

#include "cpid/cpid2kworker.h"
#include "cpid/redisclient.h"

#include <common/str.h>

#include <fmt/format.h>
#include <gflags/gflags.h>
#include <nlohmann/json.hpp>

#include <poll.h>

// From redisclient_t.cpp
DECLARE_string(redis_host);
DECLARE_int32(redis_port);

using json = nlohmann::json;

namespace cpid {
bool operator<(Cpid2kWorkerInfo const& a, Cpid2kWorkerInfo const& b) {
  return a.id < b.id;
}
bool operator==(Cpid2kWorkerInfo const& a, Cpid2kWorkerInfo const& b) {
  return a.id == b.id && a.host == b.host && a.services == b.services;
}
} // namespace cpid

using namespace cpid;

namespace {
std::string jobspec(std::vector<Cpid2kWorkerInfo> const& infos) {
  std::unordered_map<std::string, int> counts;
  for (auto const& info : infos) {
    counts[common::stringSplit(info.id, '_')[0]] += 1;
  }

  json spec;
  for (auto const& it : counts) {
    json j = json::object();
    j["name"] = it.first;
    j["count"] = it.second;
    j["args"] = json::array();
    spec.push_back(j);
  }
  VLOG(0) << spec.dump();
  return spec.dump();
}
} // namespace

CASE("cpid2kworker/heartbeat/basic[.redis]") {
  auto prefix = "test_basic";
  auto id = "myid";
  auto info = Cpid2kWorkerInfo::withLocalIp();
  info.id = id;

  std::unique_ptr<RedisClient> cl;
  EXPECT_NO_THROW(
      cl = std::make_unique<RedisClient>(FLAGS_redis_host, FLAGS_redis_port));
  // Set boot key
  RedisReply reply;
  EXPECT_NO_THROW(
      reply = cl->set(fmt::format("{}:boot:{}", prefix, id), "true"));
  EXPECT(reply.ok());

  std::unique_ptr<Cpid2kHeartBeater> hb;
  EXPECT_NO_THROW(
      hb = std::make_unique<Cpid2kHeartBeater>(
          info, prefix, FLAGS_redis_host, FLAGS_redis_port, 100));

  // Boot key has been deleted
  EXPECT_NO_THROW(
      reply = cl->command({"EXISTS", fmt::format("{}:boot:{}", prefix, id)}));
  EXPECT(reply.integer() == 0);
  // Heartbeat has been sent
  EXPECT_NO_THROW(
      reply =
          cl->command({"EXISTS", fmt::format("{}:heartbeat:{}", prefix, id)}));
  EXPECT(reply.integer() == 1);

  // Delete object
  EXPECT_NO_THROW(hb.reset());

  // Heartbeat still there
  EXPECT_NO_THROW(
      reply =
          cl->command({"EXISTS", fmt::format("{}:heartbeat:{}", prefix, id)}));
  EXPECT(reply.integer() == 1);

  // If we wait for a while, they heartbeat should be expired
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  EXPECT_NO_THROW(
      reply =
          cl->command({"EXISTS", fmt::format("{}:heartbeat:{}", prefix, id)}));
  EXPECT(reply.integer() == 0);
}

CASE("cpid2kworker/heartbeat/noboot[.redis]") {
  auto prefix = "test_noboot";
  auto id = "myid";
  auto info = Cpid2kWorkerInfo::withLocalIp();
  info.id = id;

  // Constructing the heartbeater without a corresponding boot key results in an
  // exception during construction
  std::unique_ptr<Cpid2kHeartBeater> hb;
  EXPECT_THROWS(
      hb = std::make_unique<Cpid2kHeartBeater>(
          info, prefix, FLAGS_redis_host, FLAGS_redis_port));
}

CASE("cpid2kworker/heartbeat/dead[.redis]") {
  auto prefix = "test_dead";
  auto id = "myid";
  auto info = Cpid2kWorkerInfo::withLocalIp();
  info.id = id;

  std::unique_ptr<RedisClient> cl;
  EXPECT_NO_THROW(
      cl = std::make_unique<RedisClient>(FLAGS_redis_host, FLAGS_redis_port));
  // Set boot key
  RedisReply reply;
  EXPECT_NO_THROW(
      reply = cl->set(fmt::format("{}:boot:{}", prefix, id), "true"));
  EXPECT(reply.ok());

  std::unique_ptr<Cpid2kHeartBeater> hb;
  EXPECT_NO_THROW(
      hb = std::make_unique<Cpid2kHeartBeater>(
          info, prefix, FLAGS_redis_host, FLAGS_redis_port, 100));

  // Heartbeat has been sent
  EXPECT_NO_THROW(
      reply =
          cl->command({"EXISTS", fmt::format("{}:heartbeat:{}", prefix, id)}));
  EXPECT(reply.integer() == 1);

  // Set dead key
  EXPECT_NO_THROW(
      reply = cl->set(fmt::format("{}:dead:{}", prefix, id), "true"));
  EXPECT(reply.ok());

  // Wait until next hearbeat should have been sent out
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  EXPECT(hb->consideredDead());
}

CASE("cpid2kworker/heartbeat/connection_drop[.redis]") {
  auto prefix = "test_drop";
  auto id = "myid";
  auto info = Cpid2kWorkerInfo::withLocalIp();
  info.id = id;

  std::unique_ptr<RedisClient> cl;
  EXPECT_NO_THROW(
      cl = std::make_unique<RedisClient>(FLAGS_redis_host, FLAGS_redis_port));
  // Set boot key
  RedisReply reply;
  EXPECT_NO_THROW(
      reply = cl->set(fmt::format("{}:boot:{}", prefix, id), "true"));
  EXPECT(reply.ok());

  std::unique_ptr<Cpid2kHeartBeater> hb;
  auto intervalMs = 100;
  EXPECT_NO_THROW(
      hb = std::make_unique<Cpid2kHeartBeater>(
          info, prefix, FLAGS_redis_host, FLAGS_redis_port, intervalMs));

  // Heartbeat has been sent
  EXPECT(
      cl->command({"EXISTS", fmt::format("{}:heartbeat:{}", prefix, id)})
          .integer() == 1);

  // Drop heartbeat connection
  EXPECT(
      cl->command({"CLIENT", "KILL", "TYPE", "normal", "SKIPME", "yes"})
          .integer() == 1);

  // Ensure that the heartbeat does not expire. Do this by subscribing for the
  // relevant event and polling for longer than the heartbeat interval.
  cl->command({"SUBSCRIBE", "__keyevent@0__:expired"});
  struct pollfd pfd;
  pfd.fd = cl->ctx()->fd;
  pfd.events = POLLIN;
  auto ret = poll(&pfd, 1, intervalMs * 3);
  auto gotNotified = ret > 0 && (pfd.revents & POLLIN);
  EXPECT_NOT(gotNotified);
}

CASE("cpid2kworker/peers[.redis]") {
  auto constexpr numTrain = 2;
  auto constexpr numRollout = 4;
  auto prefix = "test_peers";
  std::vector<Cpid2kWorkerInfo> infos;
  std::vector<std::string> episodeEndpoints; // faked
  for (auto i = 0; i < numTrain; i++) {
    auto info = Cpid2kWorkerInfo::withLocalIp();
    info.id = fmt::format("0train_{}", i);
    auto port = 1234 + i;
    info.services["episodeserver"] = port;
    episodeEndpoints.push_back(fmt::format("tcp://{}:{}", info.host, port));
    infos.push_back(std::move(info));
  }
  for (auto i = 0; i < numRollout; i++) {
    auto info = Cpid2kWorkerInfo::withLocalIp();
    info.id = fmt::format("1rollout_{}", i);
    infos.push_back(std::move(info));
  }
  std::sort(infos.begin(), infos.end());
  std::sort(episodeEndpoints.begin(), episodeEndpoints.end());

  std::unique_ptr<RedisClient> cl;
  EXPECT_NO_THROW(
      cl = std::make_unique<RedisClient>(FLAGS_redis_host, FLAGS_redis_port));
  // Set boot keys
  RedisReply reply;
  for (auto const& winfo : infos) {
    EXPECT_NO_THROW(
        reply = cl->set(fmt::format("{}:boot:{}", prefix, winfo.id), "true"));
    EXPECT(reply.ok());
  }

  // Start workers
  std::vector<std::shared_ptr<Cpid2kWorker>> workers;
  for (auto const& winfo : infos) {
    EXPECT_NO_THROW(workers.push_back(std::make_shared<Cpid2kWorker>(
        winfo, prefix, FLAGS_redis_host, FLAGS_redis_port, 100)));
    // Peers counter
    EXPECT_NO_THROW(cl->command({"INCR", fmt::format("{}:peerv", prefix)}));
  }

  // Verify peers on each worker
  for (auto& w : workers) {
    std::vector<Cpid2kWorkerInfo> peers;
    EXPECT_NO_THROW(peers = w->peers());
    EXPECT(peers.size() == workers.size());
    std::sort(peers.begin(), peers.end());
    EXPECT(infos == peers);

    EXPECT_NO_THROW(peers = w->peers("train"));
    EXPECT(peers.size() == size_t(numTrain));
    for (auto const& p : peers) {
      EXPECT(common::startsWith(p.id, "0train_"));
    }
    EXPECT_NO_THROW(peers = w->peers("rollout"));
    EXPECT(peers.size() == size_t(numRollout));

    auto endpoints = w->serviceEndpoints("episodeserver");
    EXPECT(endpoints.size() == episodeEndpoints.size());
    std::sort(endpoints.begin(), endpoints.end());
    EXPECT(endpoints == episodeEndpoints);

    EXPECT_NO_THROW(peers = w->peers("foobar"));
    EXPECT(peers.size() == size_t(0));
  }
}

CASE("cpid2kworker/peers_many[.redis]") {
  auto constexpr numTrain = 103;
  auto constexpr numRollout = 502;
  auto prefix = "test_peers_many";
  std::vector<Cpid2kWorkerInfo> infos;
  std::vector<std::string> episodeEndpoints; // faked
  for (auto i = 0; i < numTrain; i++) {
    auto info = Cpid2kWorkerInfo::withLocalIp();
    info.id = fmt::format("0train_{}", i);
    auto port = 1234 + i;
    info.services["episodeserver"] = port;
    episodeEndpoints.push_back(fmt::format("tcp://{}:{}", info.host, port));
    infos.push_back(std::move(info));
  }
  for (auto i = 0; i < numRollout; i++) {
    auto info = Cpid2kWorkerInfo::withLocalIp();
    info.id = fmt::format("1rollout_{}", i);
    infos.push_back(std::move(info));
  }
  std::sort(infos.begin(), infos.end());
  std::sort(episodeEndpoints.begin(), episodeEndpoints.end());

  std::unique_ptr<RedisClient> cl;
  EXPECT_NO_THROW(
      cl = std::make_unique<RedisClient>(FLAGS_redis_host, FLAGS_redis_port));
  // Set boot keys
  RedisReply reply;
  for (auto const& winfo : infos) {
    EXPECT_NO_THROW(
        reply = cl->set(fmt::format("{}:boot:{}", prefix, winfo.id), "true"));
    EXPECT(reply.ok());
  }

  // Start workers
  std::vector<std::shared_ptr<Cpid2kWorker>> workers;
  for (auto const& winfo : infos) {
    EXPECT_NO_THROW(workers.push_back(std::make_shared<Cpid2kWorker>(
        winfo, prefix, FLAGS_redis_host, FLAGS_redis_port, 100)));
    // Peers counter
    EXPECT_NO_THROW(cl->command({"INCR", fmt::format("{}:peerv", prefix)}));
  }

  // Verify peers on each 100th worker for brevity/speed
  for (auto i = 0U; i < workers.size(); i += 100) {
    auto& w = workers[i];
    std::vector<Cpid2kWorkerInfo> peers;
    EXPECT_NO_THROW(peers = w->peers());
    EXPECT(peers.size() == workers.size());
    std::sort(peers.begin(), peers.end());
    EXPECT(infos == peers);

    EXPECT_NO_THROW(peers = w->peers("train"));
    EXPECT(peers.size() == size_t(numTrain));
    for (auto const& p : peers) {
      EXPECT(common::startsWith(p.id, "0train_"));
    }
    EXPECT_NO_THROW(peers = w->peers("rollout"));
    EXPECT(peers.size() == size_t(numRollout));

    auto endpoints = w->serviceEndpoints("episodeserver");
    EXPECT(endpoints.size() == episodeEndpoints.size());
    std::sort(endpoints.begin(), endpoints.end());
    EXPECT(endpoints == episodeEndpoints);

    EXPECT_NO_THROW(peers = w->peers("foobar"));
    EXPECT(peers.size() == size_t(0));
  }
}

// Verifies that calls like peers() are ok with connection drops (assuming that
// it can be re-established)
CASE("cpid2kworker/connection_drop[.redis]") {
  auto constexpr numWorkers = 2;
  auto prefix = "test_drop";
  std::vector<Cpid2kWorkerInfo> infos;
  for (auto i = 0; i < numWorkers; i++) {
    auto info = Cpid2kWorkerInfo::withLocalIp();
    info.id = fmt::format("0train_{}", i);
    infos.push_back(std::move(info));
  }
  std::sort(infos.begin(), infos.end());

  std::unique_ptr<RedisClient> cl;
  EXPECT_NO_THROW(
      cl = std::make_unique<RedisClient>(FLAGS_redis_host, FLAGS_redis_port));
  // Set boot keys
  RedisReply reply;
  for (auto const& winfo : infos) {
    EXPECT_NO_THROW(
        reply = cl->set(fmt::format("{}:boot:{}", prefix, winfo.id), "true"));
    EXPECT(reply.ok());
  }

  // Start workers
  std::vector<std::shared_ptr<Cpid2kWorker>> workers;
  for (auto const& winfo : infos) {
    EXPECT_NO_THROW(workers.push_back(std::make_shared<Cpid2kWorker>(
        winfo, prefix, FLAGS_redis_host, FLAGS_redis_port, 100)));
    // Peers counter
    EXPECT_NO_THROW(cl->command({"INCR", fmt::format("{}:peerv", prefix)}));
  }

  // Verify peers on each worker, which will initialize the respective redis
  // clients. Cache Redis clients locally since `threadLocalClient()` will
  // automatically
  std::map<std::string, std::shared_ptr<RedisClient>> rdsClients;
  for (auto& w : workers) {
    std::vector<Cpid2kWorkerInfo> peers;
    EXPECT_NO_THROW(peers = w->peers());
    EXPECT(peers.size() == workers.size());
    std::sort(peers.begin(), peers.end());
    EXPECT(infos == peers);
    rdsClients[w->info().id] = w->threadLocalClient();
  }

  // Drop all connections
  EXPECT(
      cl->command({"CLIENT", "KILL", "TYPE", "normal", "SKIPME", "yes"})
          .integer() > 0);

  // Verify that clients are disconnected
  for (auto& w : workers) {
    auto rcl = rdsClients[w->info().id];
    EXPECT_THROWS(rcl->ping());
    EXPECT_NOT(rcl->isConnected());
  }

  // Wait for a while so that cached information on workers is considered stale.
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  // Query peers again
  for (auto& w : workers) {
    std::vector<Cpid2kWorkerInfo> peers;
    EXPECT_NO_THROW(peers = w->peers());
    EXPECT(peers.size() == workers.size());
    std::sort(peers.begin(), peers.end());
    EXPECT(infos == peers);

    // Verify that connection was re-established
    auto rcl = rdsClients[w->info().id];
    EXPECT_NO_THROW(rcl->ping());
    EXPECT(rcl->isConnected());
  }
}

CASE("cpid2kworker/waitfor[.redis]") {
  auto constexpr numFast = 3;
  auto constexpr numSlow = 2;
  auto prefix = "test_waitfor";
  std::vector<Cpid2kWorkerInfo> infos;
  for (auto i = 0; i < numFast; i++) {
    auto info = Cpid2kWorkerInfo::withLocalIp();
    info.id = fmt::format("0fast_{}", i);
    infos.push_back(std::move(info));
  }
  for (auto i = 0; i < numSlow; i++) {
    auto info = Cpid2kWorkerInfo::withLocalIp();
    info.id = fmt::format("1slow_{}", i);
    infos.push_back(std::move(info));
  }
  std::sort(infos.begin(), infos.end());

  std::unique_ptr<RedisClient> cl;
  EXPECT_NO_THROW(
      cl = std::make_unique<RedisClient>(FLAGS_redis_host, FLAGS_redis_port));
  RedisReply reply;
  EXPECT_NO_THROW(
      reply = cl->set(fmt::format("{}:jobspec", prefix), jobspec(infos)));
  EXPECT(reply.ok());

  // Set boot keys
  for (auto const& winfo : infos) {
    EXPECT_NO_THROW(
        reply = cl->set(fmt::format("{}:boot:{}", prefix, winfo.id), "true"));
    EXPECT(reply.ok());
  }

  // Start fast workers
  bool first = true;
  std::vector<std::shared_ptr<Cpid2kWorker>> workers;
  for (auto const& winfo : infos) {
    if (!common::gmatch(winfo.id, "0fast_*")) {
      continue;
    }
    EXPECT_NO_THROW(workers.push_back(std::make_shared<Cpid2kWorker>(
        winfo, prefix, FLAGS_redis_host, FLAGS_redis_port, 100)));
    // Peers counter
    EXPECT_NO_THROW(cl->command({"INCR", fmt::format("{}:peerv", prefix)}));

    if (first) {
      for (auto& w : workers) {
        EXPECT(w->waitForOne("fast", std::chrono::milliseconds(100)));
        EXPECT_NOT(w->waitForAll("fast", std::chrono::milliseconds(100)));
        EXPECT_NOT(w->waitForOne("slow", std::chrono::milliseconds(100)));
        EXPECT_NOT(w->waitForAll("slow", std::chrono::milliseconds(100)));
        EXPECT_NOT(w->waitForAll(
            Cpid2kWorker::kAnyRole, std::chrono::milliseconds(100)));
      }
    }
    first = false;
  }
  for (auto& w : workers) {
    EXPECT(w->waitForOne("fast", std::chrono::milliseconds(100)));
    EXPECT(w->waitForAll("fast", std::chrono::milliseconds(100)));
    EXPECT_NOT(w->waitForOne("slow", std::chrono::milliseconds(100)));
    EXPECT_NOT(w->waitForAll("slow", std::chrono::milliseconds(100)));
    EXPECT_NOT(
        w->waitForAll(Cpid2kWorker::kAnyRole, std::chrono::milliseconds(100)));
  }

  first = true;
  for (auto const& winfo : infos) {
    if (!common::gmatch(winfo.id, "1slow_*")) {
      continue;
    }
    EXPECT_NO_THROW(workers.push_back(std::make_shared<Cpid2kWorker>(
        winfo, prefix, FLAGS_redis_host, FLAGS_redis_port, 100)));
    // Peers counter
    EXPECT_NO_THROW(cl->command({"INCR", fmt::format("{}:peerv", prefix)}));

    if (first) {
      for (auto& w : workers) {
        EXPECT(w->waitForOne("fast", std::chrono::milliseconds(100)));
        EXPECT(w->waitForAll("fast", std::chrono::milliseconds(100)));
        EXPECT(w->waitForOne("slow", std::chrono::milliseconds(100)));
        EXPECT_NOT(w->waitForAll("slow", std::chrono::milliseconds(100)));
        EXPECT_NOT(w->waitForAll(
            Cpid2kWorker::kAnyRole, std::chrono::milliseconds(100)));
      }
    }
    first = false;
  }

  for (auto& w : workers) {
    EXPECT(w->waitForOne("fast", std::chrono::milliseconds(100)));
    EXPECT(w->waitForAll("fast", std::chrono::milliseconds(100)));
    EXPECT(w->waitForOne("slow", std::chrono::milliseconds(100)));
    EXPECT(w->waitForAll("slow", std::chrono::milliseconds(100)));
    EXPECT(
        w->waitForAll(Cpid2kWorker::kAnyRole, std::chrono::milliseconds(100)));
  }
}

CASE("cpid2kworker/contexts[.redis]") {
  auto constexpr numTrain = 2;
  auto constexpr numRollout = 4;
  auto prefix = "test_contexts";
  std::vector<Cpid2kWorkerInfo> infos;
  for (auto i = 0; i < numTrain; i++) {
    auto info = Cpid2kWorkerInfo::withLocalIp();
    info.id = fmt::format("0train_{}", i);
    infos.push_back(std::move(info));
  }
  for (auto i = 0; i < numRollout; i++) {
    auto info = Cpid2kWorkerInfo::withLocalIp();
    info.id = fmt::format("1rollout_{}", i);
    infos.push_back(std::move(info));
  }

  std::unique_ptr<RedisClient> cl;
  EXPECT_NO_THROW(
      cl = std::make_unique<RedisClient>(FLAGS_redis_host, FLAGS_redis_port));
  // Set boot keys
  RedisReply reply;
  for (auto const& winfo : infos) {
    EXPECT_NO_THROW(
        reply = cl->set(fmt::format("{}:boot:{}", prefix, winfo.id), "true"));
    EXPECT(reply.ok());
  }

  // Start workers
  std::vector<std::shared_ptr<Cpid2kWorker>> workers;
  for (auto const& winfo : infos) {
    EXPECT_NO_THROW(workers.push_back(std::make_shared<Cpid2kWorker>(
        winfo, prefix, FLAGS_redis_host, FLAGS_redis_port, 100)));
    // Peers counter
    EXPECT_NO_THROW(cl->command({"INCR", fmt::format("{}:peerv", prefix)}));
  }

  // Simple test: train workers allreduce a tensor and broadcast it to rollout
  // workers. This relies on train workers having a lexicographically smaller ID
  // so that rank 0 of the global context will always fall onto a trainer.
  std::vector<torch::Tensor> t1, t2;
  for (size_t i = 0; i < infos.size(); i++) {
    t1.push_back(torch::ones({5, 5}));
    t2.push_back(torch::ones({5, 5}));
  }
  auto runTrain = [&](size_t i, std::shared_ptr<Cpid2kWorker> worker) {
    EXPECT_THROWS(worker->dcontext("no_matching_peers"));
    EXPECT(worker->dcontext("train").size == numTrain);
    worker->dcontext("train").allreduce(t1[i]);
    worker->dcontext().broadcast(t1[i]);
    worker->dcontext().allreduce(t2[i]);
  };
  auto runRollout = [&](size_t i, std::shared_ptr<Cpid2kWorker> worker) {
    EXPECT_THROWS(
        worker->dcontext("train")); // rollout worker is not part of this
    EXPECT(worker->dcontext().size == numTrain + numRollout);
    worker->dcontext().broadcast(t1[i]);
    worker->dcontext().allreduce(t2[i]);
  };

  std::vector<std::thread> threads;
  for (size_t i = 0; i < infos.size(); i++) {
    if (common::startsWith(infos[i].id, "0train_")) {
      threads.emplace_back(runTrain, i, workers[i]);
    } else {
      threads.emplace_back(runRollout, i, workers[i]);
    }
  }

  for (auto& th : threads) {
    th.join();
  }
  for (size_t i = 0; i < infos.size(); i++) {
    EXPECT(t1[i].sum().item<float>() == 25 * numTrain);
    EXPECT(t2[i].sum().item<float>() == 25 * (numTrain + numRollout));
  }
}

CASE("cpid2kworker/broadcast_timeout[.redis]") {
  auto constexpr numTrain = 2;
  auto prefix = "test_broadcast_timeout";
  std::vector<Cpid2kWorkerInfo> infos;
  for (auto i = 0; i < numTrain; i++) {
    auto info = Cpid2kWorkerInfo::withLocalIp();
    info.id = fmt::format("0train_{}", i);
    infos.push_back(std::move(info));
  }

  std::unique_ptr<RedisClient> cl;
  EXPECT_NO_THROW(
      cl = std::make_unique<RedisClient>(FLAGS_redis_host, FLAGS_redis_port));
  // Set boot keys
  RedisReply reply;
  for (auto const& winfo : infos) {
    EXPECT_NO_THROW(
        reply = cl->set(fmt::format("{}:boot:{}", prefix, winfo.id), "true"));
    EXPECT(reply.ok());
  }

  // Start workers
  std::vector<std::shared_ptr<Cpid2kWorker>> workers;
  for (auto const& winfo : infos) {
    EXPECT_NO_THROW(workers.push_back(std::make_shared<Cpid2kWorker>(
        winfo, prefix, FLAGS_redis_host, FLAGS_redis_port, 100)));
    // Peers counter
    EXPECT_NO_THROW(cl->command({"INCR", fmt::format("{}:peerv", prefix)}));
  }

  std::vector<torch::Tensor> t1;
  for (size_t i = 0; i < infos.size(); i++) {
    t1.push_back(torch::ones({5, 5}));
  }

  // Simulate timeouts by mutually exclusive execution. As in a real setup,
  // we'll need to ensure that all jobs perform the same calls for context
  // creation and collectives.
  std::mutex mutex;

  // Context creation timeout
  auto runTrain1 = [&](size_t i, std::shared_ptr<Cpid2kWorker> worker) {
    auto lock = std::lock_guard<std::mutex>(mutex);
    try {
      worker->dcontext(Cpid2kWorker::kAnyRole, std::chrono::milliseconds(500));
    } catch (std::exception const& ex) {
      VLOG(1) << "Got exception: " << ex.what();
      auto match = common::startsWith(ex.what(), "Wait timeout for key(s)") ||
          common::gmatchi(ex.what(), "*Connect timoeut*");
      EXPECT(match);
      return;
    }
    EXPECT(false);
  };

  // Collective timeout
  auto runTrain2 = [&](size_t i, std::shared_ptr<Cpid2kWorker> worker) {
    auto& ctx = worker->dcontext(
        Cpid2kWorker::kAnyRole, std::chrono::milliseconds(500));
    EXPECT(ctx.size == numTrain);

    auto lock = std::lock_guard<std::mutex>(mutex);
    static int n = 0;
    n++;
    try {
      ctx.broadcast(t1[i]);
    } catch (std::exception const& ex) {
      VLOG(1) << "Got exception: " << ex.what();
      if (n == 1) { // first one
        EXPECT(common::gmatchi(ex.what(), "*Timed out*"));
      } else { // second one
        EXPECT(common::gmatchi(ex.what(), "*Connection closed by peer*"));
      }
      // This will force context re-creation upon next usage
      worker->discardDContext(Cpid2kWorker::kAnyRole);
      return;
    }
    EXPECT(false);
  };

  // With the same workers, do a successful broadcast now
  auto runTrain3 = [&](size_t i, std::shared_ptr<Cpid2kWorker> worker) {
    while (true) {
      try {
        auto& ctx = worker->dcontext(
            Cpid2kWorker::kAnyRole, std::chrono::milliseconds(2000));
        // This should work now
        ctx.broadcast(t1[i]);
        break;
      } catch (std::exception const& ex) {
        VLOG(0) << "This was... unexpected: " << ex.what();
        EXPECT(false); // This should not happen
      }
    }
  };

  auto run = [&](auto func) {
    std::vector<std::thread> threads;
    for (size_t i = 0; i < infos.size(); i++) {
      threads.emplace_back(func, i, workers[i]);
    }
    for (auto& th : threads) {
      th.join();
    }
  };

  run(runTrain1);
  run(runTrain2);
  run(runTrain3);
}
