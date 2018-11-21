/*
 * Copyright (c) 2018-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <valarray>
#include <vector>

namespace cpid {
class MetricsContext {
  using hires_clock = std::chrono::steady_clock;

 public:
  using Timestamp = uint64_t; // ms since epoch, for simple json dump
  using Event = std::pair<Timestamp, float>;
  using Events = std::pair<Timestamp, std::vector<float>>;
  using TimeInterval = double; // ms

  void pushEvent(std::string const& key, float value = 1.0);
  void pushEvents(std::string const& key, std::vector<float> values);
  Event getLastEvent(std::string const& key) const;
  std::vector<Event> getLastEvents(std::string const& key, size_t n) const;
  float getLastEventValue(std::string const& key) const;
  bool hasEvent(std::string const& key) const;
  std::unordered_map<std::string, float> getMeanEventValues() const;
  using Reducer = std::function<float(float, float)>;
  /// Behaves exactly as std::accumulate on all the events streams
  std::unordered_map<std::string, float> reduceEventValues(
      const Reducer& reducer,
      float initValue) const;
  void incCounter(std::string const& key, float amount = 1.0);
  void setCounter(std::string const& key, float amount);
  float getCounter(std::string const& key) const;
  float getCounter(std::string const& key, float defaultValue) const;
  void snapshotCounter(
      std::string const& counterKey,
      std::string const& eventKey,
      float defaultValue);
  TimeInterval getLastInterval(std::string const& key) const;
  std::unordered_map<std::string, float> getMeanIntervals() const;
  /// Behaves exactly as std::accumulate on all the intervals streams
  std::unordered_map<std::string, float> reduceIntervals(
      const Reducer& reducer,
      float initValue) const;
  void dumpJson(std::string const& path) const;
  void dumpJson(std::ostream&) const;
  void loadJson(std::string const& path);
  void loadJson(std::istream&);

  void clear();

  bool operator==(const MetricsContext&) const; // Mostly for testing

  class Timer {
    hires_clock::time_point start_;
    std::shared_ptr<MetricsContext> metrics_;
    std::string key_;
    unsigned long subsampleFactor_;

   public:
    /// Only subsampleRatio events are stored; it is expected that
    // 0 <= subsampleRatio <= 1.
    Timer(
        std::shared_ptr<MetricsContext> metrics,
        std::string key,
        float subsampleRatio = 1);
    ~Timer();
  };

 protected:
  std::unordered_map<std::string, std::vector<Event>> timeSeries_;
  std::unordered_map<std::string, std::vector<Events>> timeSeriesS_;
  std::unordered_map<std::string, float> counters_;
  std::unordered_map<std::string, std::vector<TimeInterval>> intervals_;
  mutable std::mutex mutex_;
};
} // namespace cpid
