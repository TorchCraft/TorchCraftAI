/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "distributed.h"
#include <nlohmann/json.hpp>

#include <mutex>
#include <thread>

namespace cpid {

class RedisClient;

struct Cpid2kWorkerInfo {
  static Cpid2kWorkerInfo withLocalIp();
  static Cpid2kWorkerInfo withLocalIpFromEnvVars();

  bool roleIs(std::string_view role);

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

  using CommandImpl = std::function<void(nlohmann::json const&)>;
  void registerCommand(std::string_view name, CommandImpl impl);

 private:
  std::string bootKey() const;
  std::string deadKey() const;
  std::string commandsKey() const;
  std::string heartBeatKey() const;
  std::string heartBeatData() const;
  void boot();
  void run();
  void executeCommands(std::string command);

  Cpid2kWorkerInfo info_;
  std::string prefix_;
  int64_t intervalMs_;
  std::unique_ptr<RedisClient> redis_;
  std::thread th_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> consideredDead_{false};

  std::mutex commandsImplM_;
  std::unordered_map<std::string, CommandImpl> commandsImpl_;
};

/**
 * Encapsulates information about the participating peers in a cpid2k job.
 */
class Cpid2kGlobalState {
 public:
  using Clock = std::chrono::steady_clock;

  Cpid2kGlobalState(std::string prefix, int64_t updateIntervalMs = 5 * 1000);

  void update(RedisClient& client);

  std::string_view prefix() const {
    return prefix_;
  }
  bool isDone();
  std::vector<Cpid2kWorkerInfo> peers(std::string_view role);
  std::vector<std::string> serviceEndpoints(std::string const& serviceName);

 private:
  void tryUpdate(RedisClient& client);

  std::string prefix_;
  std::mutex mutex_;
  int64_t peerv_ = -1; // Version number for peer information
  Clock::time_point lastPeersCheck_;
  std::chrono::milliseconds pcInterval_;
  std::vector<Cpid2kWorkerInfo> peers_;
  bool isDone_ = false;
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

  static std::unique_ptr<Cpid2kWorker> fromEnvVars(Cpid2kWorkerInfo const&);
  static std::unique_ptr<Cpid2kWorker> fromEnvVars();

  Cpid2kWorkerInfo const& info() const;
  std::string_view prefix() const;
  bool consideredDead() const;
  bool isDone();
  std::string redisKey(std::string_view key) const;
  std::shared_ptr<RedisClient> threadLocalClient();
  Cpid2kHeartBeater& heartBeater() {
    return hb_;
  }

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
  void appendMetrics(std::string_view metricsName, nlohmann::json const& json);
  void publishEvent(std::string_view key, nlohmann::json data);

 private:
  std::shared_ptr<RedisClient> redisClient(std::thread::id id);
  int numWorkersWithRoleInSpec(std::string_view role);

  std::mutex mutex_; // General mutex to make functions thread-safe
  Cpid2kWorkerInfo info_;
  std::string prefix_;
  std::string host_;
  int port_;
  Cpid2kHeartBeater hb_;
  Cpid2kGlobalState gs_;
  std::chrono::milliseconds pcInterval_;
  std::unordered_map<std::string, std::unique_ptr<distributed::Context>>
      dcontexts_;
  std::unordered_map<std::string, std::vector<std::string>> dcontextIds_;
  std::unordered_map<std::thread::id, std::shared_ptr<RedisClient>>
      threadClients_;
};

/**
 * Helper class to aggregate metrics locally, and send them reguarly as events
 * in the redis database as key 'prefix:metricEvents'
 */
class Cpid2kMetrics {
 public:
  enum AggregationType {
    AggregateMean,
    AggregateCumSum,
    AggregateSum,
    AggregateMin,
    AggregateMax,
    AggregateLast,
  };
  struct EventMetric {
    template <typename T>
    EventMetric(
        std::string n,
        T v,
        AggregationType a = AggregateMean,
        typename std::enable_if_t<std::is_arithmetic<T>::value>* = 0)
        : name(std::move(n)), value(float(v)), aggregation(a) {}
    virtual ~EventMetric() = default;
    std::string name;
    float value;
    AggregationType aggregation;
  };
  struct Aggregator {
    virtual ~Aggregator() = default;
    virtual void add(float value) = 0;
    virtual nlohmann::json value() const = 0;
    virtual float floatValue() const = 0;
    std::string_view type;
  };
  class TimerMs {
   public:
    TimerMs(
        std::shared_ptr<Cpid2kMetrics>,
        std::string const& name,
        AggregationType agg = AggregateMean,
        std::string const& prefix = "");
    TimerMs(TimerMs const& other) = delete;
    ~TimerMs();

    TimerMs& operator=(TimerMs const&) = delete;

    void stop();
    void resume();

   protected:
    std::chrono::duration<double, std::milli> elapsed_{0};
    bool running_ = true;
    std::chrono::steady_clock::time_point start_;
    std::shared_ptr<Cpid2kMetrics> m_;
    std::string name_;
    AggregationType agg_;
    std::string prefix_;
  };

  Cpid2kMetrics(
      std::shared_ptr<Cpid2kWorker> worker,
      std::chrono::milliseconds sendInterval = std::chrono::seconds(30),
      size_t subsample = 1);
  ~Cpid2kMetrics();
  void push(std::vector<EventMetric> const& metrics, std::string prefix = "");
  // This metrics object can be disabled when subsample > 1
  bool enabled() const;
  std::unordered_map<std::string, float> aggregateLocal(
      std::string const& prefix = "") const;

 protected:
  void run();
  using Clock = std::chrono::steady_clock;

  std::shared_ptr<Cpid2kWorker> worker_;
  std::chrono::milliseconds sendInterval_;

  size_t subsample_;
  std::thread thr_;
  std::atomic<bool> stop_;

  mutable std::mutex aggregatorsMutex_;
  std::unordered_map<
      std::string /* prefix */,
      std::unordered_map<
          std::string /* metric key */,
          std::unique_ptr<Aggregator>>>
      aggregators_;
};

} // namespace cpid
