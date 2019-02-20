/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "distributed.h"

#include <mutex>
#include <thread>

namespace cpid {

class RedisClient;

struct Cpid2kWorkerInfo {
  static Cpid2kWorkerInfo withLocalIp();

  template <typename Archive>
  void serialize(Archive& ar) {
    ar(CEREAL_NVP(id));
    ar(CEREAL_NVP(host));
    ar(CEREAL_NVP(services));
  }

  /// Worker ID
  std::string id;
  /// IP address of the machine this process is running on.
  std::string host;
  /// Services offered by this worker (name to port number)
  std::map<std::string, int> services;
};

/**
 * Periodically sends out heartbeats to a Redis instance.
 *
 * The supplied Cpid2kWorkerInfo will be sent as the heartbeat value to the
 * database. In addition, during construction this class will ensure that
 * startup can be performed according to the scheduler (see the supplied Redis
 * schema). If not, the constructor will throw.
 */
class Cpid2kHeartBeater {
 public:
  Cpid2kHeartBeater(
      Cpid2kWorkerInfo info,
      std::string prefix,
      std::string_view host,
      int port,
      int64_t intevalMs = 10 * 1000);
  ~Cpid2kHeartBeater();

  /// Returns true if the worker is considered dead by the scheduler.
  /// In this case, the worker should abort its execution.
  bool consideredDead() const;

  int64_t intervalMs() const {
    return intervalMs_;
  }

 private:
  std::string bootKey() const;
  std::string deadKey() const;
  std::string heartBeatKey() const;
  std::string heartBeatData() const;
  void boot();
  void run();

  Cpid2kWorkerInfo info_;
  std::string prefix_;
  int64_t intervalMs_;
  std::unique_ptr<RedisClient> redis_;
  std::thread th_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> consideredDead_{false};
};

/**
 * Helper class for job coordination via a central Redis instance.
 *
 * In a nutshell, the Cpid2kWorker class does the following:
 * - Communicate local job status to the scheduler via Cpid2kHeartbeater (ctor
 *   will throw if job does not have start permission).
 * - Provide basic information about global job status (`peers()`, `isDone()`,
 *   etc.) and local status as seen by the scheduler (`consideredDead()`).
 * - Convenience functions for common operations (`dcontext()`,
 *   `waitForOne/All()`, etc.)
 *
 * For manual operations on the Redis database, use `threadLocalClient()` to
 * obtain a RedisClient instance for the current thread. Note that these will be
 * re-used.
 *
 * All public functions are thread-safe, i.e. it's alright to call them from
 * several trainer or game threads.
 */
class Cpid2kWorker {
 public:
  using Clock = std::chrono::steady_clock;
  static std::string const kAnyRole;
  static std::chrono::milliseconds const kNoTimeout;
  static std::chrono::milliseconds const kDefaultTimeout;

  Cpid2kWorker(
      Cpid2kWorkerInfo info,
      std::string prefix,
      std::string host,
      int port = 6379,
      int64_t hbIntervalMs = 10 * 1000);
  ~Cpid2kWorker();

  Cpid2kWorkerInfo const& info() const;
  std::string_view prefix() const;
  bool consideredDead() const;
  bool isDone();
  std::string redisKey(std::string_view key) const;
  std::shared_ptr<RedisClient> threadLocalClient();

  distributed::Context& dcontext(
      std::string const& role = kAnyRole,
      std::chrono::milliseconds timeout = kDefaultTimeout);
  void discardDContext(std::string const& role = kAnyRole);

  std::vector<Cpid2kWorkerInfo> peers(std::string_view role = kAnyRole);
  std::vector<std::string> serviceEndpoints(std::string const& serviceName);
  bool waitForOne(
      std::string_view role,
      std::chrono::milliseconds timeout = kNoTimeout);
  bool waitForAll(
      std::string_view role,
      std::chrono::milliseconds timeout = kNoTimeout);

 private:
  std::shared_ptr<RedisClient> redisClient(std::thread::id id);
  void updateGlobalState();
  void updateGlobalStateImpl();

  std::mutex mutex_; // General mutex to make functions thread-safe
  Cpid2kWorkerInfo info_;
  std::string prefix_;
  std::string host_;
  int port_;
  Cpid2kHeartBeater hb_;
  int64_t peerv_ = -1; // Version number for peer information
  Clock::time_point lastPeersCheck_;
  std::chrono::milliseconds pcInterval_;
  std::vector<Cpid2kWorkerInfo> peers_;
  bool isDone_ = false;
  std::unordered_map<std::string, std::unique_ptr<distributed::Context>>
      dcontexts_;
  std::unordered_map<std::string, std::vector<std::string>> dcontextIds_;
  std::unordered_map<std::thread::id, std::shared_ptr<RedisClient>>
      threadClients_;
};

} // namespace cpid
