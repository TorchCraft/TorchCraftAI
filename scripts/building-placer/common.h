/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "cherrypi.h"
#include "fsutils.h"
#include "utils.h"

#include "cpid/metrics.h"
#include "models/buildingplacer.h"

#include <fmt/format.h>
#include <glog/logging.h>

#include <cmath>
#include <map>

using namespace cherrypi;

namespace {
using UnitType = BuildingPlacerSample::UnitType;
const std::vector<std::string> kMetricsList = {"loss",
                                               "top1",
                                               "top5",
                                               "d1",
                                               "d3"};

struct ThroughputMeter {
  size_t n;
  hires_clock::time_point start;

  void reset() {
    n = 0;
    start = hires_clock::now();
  }

  // Per second
  double throughput() {
    auto duration = hires_clock::now() - start;
    return 1000 * double(n) /
        std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
  }
};

// This function will sum up all the losses by building type and then divide by
// the total number of samples to get the loss ratio
inline void pushNormalizedMetrics(
    cpid::MetricsContext* metricsContext,
    std::map<UnitType, uint32_t> const& typeN) {
  std::unordered_map<std::string, float> summedMetrics;
  for (auto const& metric : kMetricsList) {
    summedMetrics[metric] = 0.0f;
  }
  float n =
      metricsContext->getCounter("global_n", metricsContext->getCounter("n"));
  for (auto const& it : typeN) {
    auto denom = float(it.second);
    for (auto const& metric : kMetricsList) {
      auto index = fmt::format("{}_{}", metric, it.first);
      metricsContext->pushEvent(
          index + "_normalized",
          metricsContext->getCounter(index, 0.0f) / denom);
      summedMetrics[metric] += metricsContext->getCounter(index, 0.0f) / n;
    }
  }
  for (auto const& metric : kMetricsList) {
    auto index = fmt::format("{}_normalized", metric);
    metricsContext->pushEvent(index, summedMetrics[metric]);
  }
}

inline void logPerf(
    cpid::MetricsContext* metricsContext,
    std::map<UnitType, uint32_t> const& typeN,
    int epoch,
    int steps) {
  // Totals
  std::ostringstream ss;
  ss << fmt::format("valid {}/{}: ", epoch, steps);
  for (auto const& metric : kMetricsList) {
    auto index = fmt::format("{}_normalized", metric);
    ss << fmt::format(
              "{}:{:.04f}", metric, metricsContext->getLastEventValue(index))
       << " ";
  }
  VLOG(0) << ss.str();

  // By unit type
  for (auto const& it : typeN) {
    ss.str("");
    ss.clear();
    auto type = getUnitBuildType(it.first);
    ss << fmt::format("valid {}/{}: ", epoch, steps);
    for (auto const& metric : kMetricsList) {
      auto index = fmt::format("{}_{}_normalized", metric, type->unit);
      ss << fmt::format(
                "{}:{:.04f}", metric, metricsContext->getLastEventValue(index))
         << " ";
    }
    ss << "for " << it.second << " " << type->name
       << (it.second > 1 ? "s" : "");
    VLOG(0) << ss.str();
  }
}

} // namespace
