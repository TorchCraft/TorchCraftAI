/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "checkpointer.h"
#include "distributed.h"
#include "trainer.h"
#include <common/assert.h>
#include <common/fsutils.h>
#include <fmt/format.h>
#include <visdom/visdom.h>

auto const vopts = &visdom::makeOpts;
constexpr const auto vappend = visdom::UpdateMethod::Append;
constexpr const auto vnone = visdom::UpdateMethod::None;
namespace cpid {
namespace dist = distributed;
namespace fsutils = common::fsutils;

Checkpointer::Checkpointer(std::shared_ptr<Trainer> trainer)
    : trainer_(trainer) {
  lastEpochStamp_ = hires_clock::now();
}

std::string Checkpointer::getModelPath() const {
  return checkpointPath_ + "trainer_latest.bin";
}

void Checkpointer::updateDone(int updateCount) {
  onUpdate(updateCount);

  if ((updateCount / epochLength_) > (lastEpochUpdateNum_ / epochLength_)) {
    lastEpochUpdateNum_ = updateCount;
    onEpoch(updateCount);
  }
}

void Checkpointer::printSummary(
    std::unordered_map<std::string, float> means,
    std::unordered_map<std::string, float> mins,
    std::unordered_map<std::string, float> maxs) {
  std::vector<std::pair<std::string, float>> sortedMeans(
      means.begin(), means.end()),
      sortedMins(mins.begin(), mins.end()),
      sortedMaxs(maxs.begin(), maxs.end());
  std::sort(sortedMeans.begin(), sortedMeans.end());
  std::sort(sortedMins.begin(), sortedMins.end());
  std::sort(sortedMaxs.begin(), sortedMaxs.end());

  std::vector<float> values(sortedMeans.size()), valuesMin(sortedMeans.size()),
      valuesMax(sortedMeans.size());
  auto valueGet = [](const auto& pair) { return pair.second; };
  std::transform(
      sortedMeans.begin(), sortedMeans.end(), values.begin(), valueGet);
  std::transform(
      sortedMins.begin(), sortedMins.end(), valuesMin.begin(), valueGet);
  std::transform(
      sortedMaxs.begin(), sortedMaxs.end(), valuesMax.begin(), valueGet);

  if (aggregateMetrics_) {
    reduceMetrics(values);
    dist::allreduce(valuesMin, dist::ReduceOp::MIN);
    dist::allreduce(valuesMax, dist::ReduceOp::MAX);
  }
  if (dist::globalContext()->rank == 0) {
    for (size_t i = 0; i < values.size(); ++i) {
      switch (metricsSummaryFormat_) {
        case FORMAT_DEFAULT:
          LOG(INFO) << sortedMeans[i].first << " " << values[i]
                    << " (min: " << valuesMin[i] << " max: " << valuesMax[i]
                    << ")";
          break;
        case FORMAT_TORCHBOARD:
          LOG(INFO) << fmt::format(
              "TORCHBOARD_METRICS[{}] = {} (min: {}, max: {})",
              sortedMeans[i].first,
              values[i],
              valuesMin[i],
              valuesMax[i]);
          break;
      }
    }
  }
}

void Checkpointer::reduceMetrics(std::vector<float>& values) {
  if (reduceMax_) {
    dist::allreduce(values, dist::ReduceOp::MAX);
  } else {
    dist::allreduce(values);
    for (float& v : values) {
      v /= dist::globalContext()->size;
    }
  }
};

// Visdom related functions
void Checkpointer::plotVisdom(
    const std::vector<float>& values,
    int updateCount) {
  if (!visdom_ || visdomKeys_.size() == 0) {
    return;
  }
  visdomLines_.resize(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    torch::Tensor y = torch::ones({1}).fill_(values[i]);
    torch::Tensor x = torch::ones({1}).fill_((float)updateCount);
    if (!std::isfinite(y.sum().item<float>())) {
      y.fill_(1e8);
    }
    visdomLines_[i] = visdom_->line(
        y,
        x,
        visdomLines_[i],
        vopts({{"title", visdomKeys_[i]},
               {"xtitle", "Updates"},
               {"ytitle", visdomKeys_[i]}}),
        visdomLines_[i].empty() ? vnone : vappend);
  }
}

void Checkpointer::onUpdate(int updateCount) {
  auto metrics = trainer_->metricsContext();
  updateHook_(updateCount);

  if (metrics && visdomKeys_.size() != 0 && !visdomOnEpoch_ &&
      updateCount % visdomPlotFreq_ == 0) {
    std::vector<float> values;
    for (const auto& key : visdomKeys_) {
      if (metrics->hasEvent(key)) {
        values.push_back(metrics->getLastEventValue(key));
      } else {
        values.push_back(0);
        LOG(WARNING) << "Unknown key: " << key;
      }
    }
    if (aggregateMetrics_) {
      reduceMetrics(values);
    }
    if (dist::globalContext()->rank == 0) {
      plotVisdom(values, updateCount);
    }
  }
}

void Checkpointer::onEpoch(int updateCount) {
  auto metrics = trainer_->metricsContext();
  if (dist::globalContext()->rank == 0) {
    switch (metricsSummaryFormat_) {
      case FORMAT_DEFAULT:
        LOG(INFO) << "EPOCH " << updateCount / epochLength_ << " done.";
        break;
      case FORMAT_TORCHBOARD:
        LOG(INFO) << fmt::format(
            "TORCHBOARD_METRICS[epoch] = {}", updateCount / epochLength_);
        break;
    }
  }
  std::unordered_map<std::string, float> means;
  if (metrics) {
    means = metrics->getMeanEventValues();
    std::vector<float> sampleCount;
    if (metrics->hasCounter("sampleCount")) {
      sampleCount.push_back(metrics->getCounter("sampleCount"));
      if (aggregateMetrics_) {
        dist::allreduce(sampleCount);
      } else {
        // if we don't reduce, use an estimate of the sampleCount
        sampleCount[0] *= dist::globalContext()->size;
      }
    } else {
      sampleCount.push_back(0);
    }

    if (dist::globalContext()->rank == 0) {
      hires_clock::time_point now = hires_clock::now();
      std::chrono::duration<double, std::milli> dur = now - lastEpochStamp_;
      auto updatesPerSec = double(epochLength_) / (dur.count() / 1000.);
      auto framesPerSec = double(sampleCount[0]) / (dur.count() / 1000.);
      switch (metricsSummaryFormat_) {
        case FORMAT_DEFAULT:
          LOG(INFO) << "Speed: " << updatesPerSec << " updates/s    "
                    << framesPerSec << " frames/s";
          break;
        case FORMAT_TORCHBOARD:
          LOG(INFO) << fmt::format(
              "TORCHBOARD_METRICS[updatesPerSec] = {}", updatesPerSec);
          LOG(INFO) << fmt::format(
              "TORCHBOARD_METRICS[framesPerSec] = {}", framesPerSec);
          break;
      }
      lastEpochStamp_ = now;
    }

    auto means = metrics->getMeanEventValues();
    if (printMetricsSummary_ || (visdomKeys_.size() != 0 && visdomOnEpoch_)) {
      if (visdomKeys_.size() != 0 && visdomOnEpoch_) {
        std::vector<float> values;
        for (const auto& key : visdomKeys_) {
          if (means.count(key) > 0) {
            values.push_back(means[key]);
          } else {
            LOG(WARNING) << "Unknown key: " << key;
          }
        }
        if (aggregateMetrics_) {
          reduceMetrics(values);
        }
        if (dist::globalContext()->rank == 0) {
          plotVisdom(values, updateCount);
        }
      }
      if (printMetricsSummary_) {
        auto minRed = [](float a, float b) { return std::min(a, b); };
        auto maxRed = [](float a, float b) { return std::max(a, b); };
        auto mins = metrics->reduceEventValues(minRed, 1e20);
        auto maxs = metrics->reduceEventValues(maxRed, -1e20);
        if (dist::globalContext()->rank == 0) {
          LOG(INFO) << "Metrics summary:";
        }
        printSummary(means, mins, maxs);
        auto means_inter = metrics->getMeanIntervals();
        auto mins_inter = metrics->reduceIntervals(minRed, 1e20);
        auto maxs_inter = metrics->reduceIntervals(maxRed, -1e20);
        if (dist::globalContext()->rank == 0) {
          LOG(INFO) << "Timings summary:";
        }
        printSummary(means_inter, mins_inter, maxs_inter);
      }
    }

    if (dist::globalContext()->rank == 0) {
      LOG(INFO) << "";
    }
  }

  epochHook_(updateCount);

  if (dumpMetrics_) {
    ASSERT(metrics);
    metrics->dumpJson(
        checkpointPath_ + std::to_string(dist::globalContext()->rank) +
        "-epoch_" + std::to_string(updateCount / epochLength_) +
        "-metrics.json");
  }
  if (flushMetrics_) {
    ASSERT(metrics);
    metrics->clear();
  }
  if (dist::globalContext()->rank == 0) {
    checkpointTrainer("latest");
    if (compareMetric_ != "") {
      bool should_save = false;
      double new_perf = 0;
      if (means.count(compareMetric_) == 0) {
        LOG(WARNING) << "Warning: the comparison metric " << compareMetric_
                     << " seems unavailable.";
      } else {
        new_perf = means[compareMetric_];
      }
      if (fsutils::exists(checkpointPath_ + "perf.txt")) {
        std::ifstream old_perf_f(checkpointPath_ + "perf.txt");
        float old_perf;
        old_perf_f >> old_perf;
        should_save = old_perf < new_perf;
      } else {
        should_save = true;
      }
      if (should_save) {
        std::string suffix = std::to_string(new_perf);
        while (
            fsutils::exists(checkpointPath_ + "trainer_" + suffix + ".bin")) {
          suffix += "_" + std::to_string(updateCount / epochLength_);
        }
        checkpointTrainer(suffix);
        checkpointTrainer("best");
        std::ofstream perf_f(checkpointPath_ + "perf.txt");
        perf_f << new_perf << std::endl;
      }
    }
  }
}

Checkpointer& Checkpointer::checkpointPath(std::string const& path) {
  checkpointPath_ = path;
  if (path.length() && path[path.length() - 1] != fsutils::kPathSep) {
    checkpointPath_ += fsutils::kPathSep;
  }
  fsutils::mkdir(checkpointPath_);
  ASSERT(
      fsutils::isdir(checkpointPath_),
      fmt::format(
          "Unable to create checkpoint path directory: {}", checkpointPath_));
  return *this;
}

std::string const& Checkpointer::checkpointPath() const {
  return checkpointPath_;
}

void Checkpointer::checkpointTrainer(std::string const& suffix) {
  std::string path = checkpointPath_ + "trainer_" + suffix + ".bin";
  checkpointTrainer(trainer_, path);
}

void Checkpointer::checkpointTrainer(
    std::shared_ptr<Trainer> trainer,
    std::string const& filename) {
  fsutils::rmrf(filename);
  ag::save(filename, trainer.get());
}

} // namespace cpid
