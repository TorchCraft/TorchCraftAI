/*
 * Copyright (c) 2018-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "metrics.h"
#include <fstream>
#include <nlohmann/json.hpp>

using namespace cpid;

void MetricsContext::pushEvent(std::string const& key, float value) {
  Timestamp now = std::chrono::duration_cast<std::chrono::milliseconds>(
                      hires_clock::now().time_since_epoch())
                      .count();
  std::lock_guard<std::mutex> lock(mutex_);
  timeSeries_[key].emplace_back(now, value);
}

void MetricsContext::pushEvents(
    std::string const& key,
    std::vector<float> values) {
  Timestamp now = std::chrono::duration_cast<std::chrono::milliseconds>(
                      hires_clock::now().time_since_epoch())
                      .count();
  std::lock_guard<std::mutex> lock(mutex_);
  timeSeriesS_[key].emplace_back(now, std::move(values));
}

MetricsContext::Event MetricsContext::getLastEvent(
    std::string const& key) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = timeSeries_.find(key);
  if (it == timeSeries_.end()) {
    throw std::runtime_error("No such event: " + key);
  }
  return it->second.back();
}

std::vector<MetricsContext::Event> MetricsContext::getLastEvents(
    std::string const& key,
    size_t n) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = timeSeries_.find(key);
  if (it == timeSeries_.end()) {
    return std::vector<MetricsContext::Event>();
  }
  auto itsize = it->second.size() < n ? it->second.size() : n;
  return std::vector<MetricsContext::Event>(
      it->second.end() - itsize, it->second.end());
}

float MetricsContext::getLastEventValue(std::string const& key) const {
  return std::get<1>(getLastEvent(key));
}

bool MetricsContext::hasEvent(std::string const& key) const {
  return timeSeries_.count(key) > 0;
}

std::unordered_map<std::string, float> MetricsContext::getMeanEventValues()
    const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::unordered_map<std::string, float> means;
  for (auto const& it : timeSeries_) {
    if (it.second.empty()) {
      continue;
    }
    float val = 0.0f;
    for (auto const& ev : it.second) {
      val += ev.second;
    }
    means[it.first] = val / it.second.size();
  }
  return means;
}

std::unordered_map<std::string, float> MetricsContext::reduceEventValues(
    const Reducer& reducer,
    float initValue) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::unordered_map<std::string, float> reduced;
  for (auto const& it : timeSeries_) {
    if (it.second.empty()) {
      continue;
    }
    auto eventReducer = [&](float a, const Event& b) {
      return reducer(a, b.second);
    };
    reduced[it.first] = std::accumulate(
        it.second.begin(), it.second.end(), initValue, eventReducer);
  }
  return reduced;
}

void MetricsContext::incCounter(std::string const& key, float amount) {
  std::lock_guard<std::mutex> lock(mutex_);
  counters_[key] += amount;
}

float MetricsContext::getCounter(std::string const& key) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = counters_.find(key);
  if (it == counters_.end()) {
    throw std::runtime_error("No such counter: " + key);
  }
  return it->second;
}

void MetricsContext::setCounter(std::string const& key, float amount) {
  std::lock_guard<std::mutex> lock(mutex_);
  counters_[key] = amount;
}

float MetricsContext::getCounter(std::string const& key, float defaultValue)
    const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = counters_.find(key);
  if (it == counters_.end()) {
    return defaultValue;
  }
  return it->second;
}

void MetricsContext::snapshotCounter(
    std::string const& counterKey,
    std::string const& eventKey,
    float defaultValue) {
  Timestamp now = std::chrono::duration_cast<std::chrono::milliseconds>(
                      hires_clock::now().time_since_epoch())
                      .count();
  std::lock_guard<std::mutex> lock(mutex_);
  float value = defaultValue;
  auto it = counters_.find(counterKey);
  if (it != counters_.end()) {
    value = it->second;
  }
  timeSeries_[eventKey].emplace_back(now, value);
}

MetricsContext::TimeInterval MetricsContext::getLastInterval(
    std::string const& key) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = intervals_.find(key);
  if (it == intervals_.end()) {
    throw std::runtime_error("No such interval: " + key);
  }
  return it->second.back();
}

std::unordered_map<std::string, float> MetricsContext::getMeanIntervals()
    const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::unordered_map<std::string, float> means;
  for (auto const& it : intervals_) {
    if (it.second.empty()) {
      continue;
    }
    float val = 0.0f;
    for (double ev : it.second) {
      val += ev;
    }
    means[it.first] = val / it.second.size();
  }
  return means;
}

std::unordered_map<std::string, float> MetricsContext::reduceIntervals(
    const Reducer& reducer,
    float initValue) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::unordered_map<std::string, float> reduced;
  for (auto const& it : intervals_) {
    if (it.second.empty()) {
      continue;
    }
    reduced[it.first] =
        std::accumulate(it.second.begin(), it.second.end(), initValue, reducer);
  }
  return reduced;
}
void MetricsContext::dumpJson(std::string const& path) const {
  std::ofstream o(path);
  dumpJson(o);
}

void MetricsContext::dumpJson(std::ostream& o) const {
  auto metrics = [&]() {
    std::lock_guard<std::mutex> lock(mutex_);
    return nlohmann::json{
        {"counters", counters_},
        {"time_series", timeSeries_},
        {"time_series_s", timeSeriesS_},
        {"intervals", intervals_},
    };
  }();
  o << metrics;
}

void MetricsContext::loadJson(std::string const& path) {
  std::ifstream ifs(path);
  loadJson(ifs);
}

void MetricsContext::loadJson(std::istream& is) {
  nlohmann::json metrics;
  is >> metrics;
  std::lock_guard<std::mutex> lock(mutex_);
  counters_ = metrics.at("counters").get<decltype(counters_)>();
  timeSeries_ = metrics.at("time_series").get<decltype(timeSeries_)>();
  timeSeriesS_ = metrics.at("time_series_s").get<decltype(timeSeriesS_)>();
  intervals_ = metrics.at("intervals").get<decltype(intervals_)>();
}

void MetricsContext::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  timeSeries_.clear();
  timeSeriesS_.clear();
  counters_.clear();
  intervals_.clear();
}

bool MetricsContext::operator==(const MetricsContext& o) const {
  return (
      timeSeries_ == o.timeSeries_ && timeSeriesS_ == o.timeSeriesS_ &&
      counters_ == o.counters_ && intervals_ == o.intervals_);
}

MetricsContext::Timer::Timer(
    std::shared_ptr<MetricsContext> metrics,
    std::string key,
    float subsampleRatio)
    : metrics_(metrics), key_(std::move(key)) {
  if (subsampleRatio < 0.0f || subsampleRatio > 1.0f) {
    throw std::runtime_error("subsampleRatio should be within [0;1].");
  }
  if (subsampleRatio > 0.0f) {
    subsampleFactor_ = (unsigned long)1.0f / subsampleRatio;
  } else {
    subsampleFactor_ = 0;
  }

  if (metrics_) {
    start_ = hires_clock::now();
  }
}

MetricsContext::Timer::~Timer() {
  auto end = hires_clock::now();
  if (metrics_ && subsampleFactor_ > 0 &&
      (end.time_since_epoch().count() % subsampleFactor_ == 0)) {
    std::chrono::duration<double, std::milli> duration_ms = end - start_;
    std::lock_guard<std::mutex> lock(metrics_->mutex_);
    metrics_->intervals_[key_].push_back(duration_ms.count());
  }
}
