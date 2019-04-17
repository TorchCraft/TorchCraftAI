/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <autogradpp/autograd.h>

namespace visdom {
class Visdom;
}
namespace cpid {

class Trainer;
class Checkpointer {
  using hires_clock = std::chrono::steady_clock;

 public:
  Checkpointer(std::shared_ptr<Trainer> trainer);

  /// This is the entry point to be called by trainers
  void updateDone(int updateCount);

  /// Creates a checkpoint on the disk
  void checkpointTrainer(std::string const& suffix = "final");
  static void checkpointTrainer(
      std::shared_ptr<Trainer> trainer,
      std::string const& filename = "trainer_final.bin");

  /// Returns the path where the latest model would be saved. (It's not
  /// guaranteed that one have been saved yet)
  std::string getModelPath() const;

  /// Epoch length (in number of updates)
  TORCH_ARG(int, epochLength) = 500;

  /// Visdom server. Give nullptr to disable plotting
  TORCH_ARG(std::shared_ptr<visdom::Visdom>, visdom);

  /// List of metrics keys to plot
  TORCH_ARG(std::vector<std::string>, visdomKeys);

  /**
   * If true, the visdom visualization will happen at the end on the epoch, and
   * will print the mean of the parameters during that epoch. Otherwise, it will
   * plot the last value of the parameters, at the defined frequency
   */
  TORCH_ARG(bool, visdomOnEpoch) = true;

  /**
   * If visdomOnEpoch = false, this is the frequency at which visdom plots are
   * updated
   */
  TORCH_ARG(int, visdomPlotFreq) = -1;

  /// Where to save everything
  std::string checkpointPath_;
  Checkpointer& checkpointPath(std::string const& path);
  std::string const& checkpointPath() const;

  /// Metrics used to assess preformance of a model
  /// Disables performance based checkpoints if empty
  TORCH_ARG(std::string, compareMetric) = "";

  /// If true, print the mean of the metrics at each epoch
  TORCH_ARG(bool, printMetricsSummary) = true;

  /// If true, the metrics are aggregated over all workers
  TORCH_ARG(bool, aggregateMetrics) = true;

  /// If true, we clear the metrics at the end of the epoch
  TORCH_ARG(bool, flushMetrics) = false;

  /// If true, we dump the json of the metrics at each epoch
  TORCH_ARG(bool, dumpMetrics) = false;

  /// Choose a format for stdout metrics
  enum MetricsSummaryFormat {
    FORMAT_DEFAULT,
    FORMAT_TORCHBOARD,
  };
  TORCH_ARG(MetricsSummaryFormat, metricsSummaryFormat) = FORMAT_DEFAULT;

  /// If true, we reduce accross nodes using the max operator instead
  TORCH_ARG(bool, reduceMax) = true;

  using Hook = std::function<void(int)>;

  // Function to call at the end of every epoch
  TORCH_ARG(Hook, epochHook) = [](int) {};

  // Function to call at the end of every update
  TORCH_ARG(Hook, updateHook) = [](int) {};

 protected:
  void onUpdate(int updateCount);
  void onEpoch(int updateCount);
  void plotVisdom(const std::vector<float>& values, int count);
  void printSummary(
      std::unordered_map<std::string, float> means,
      std::unordered_map<std::string, float> mins,
      std::unordered_map<std::string, float> maxs);
  void reduceMetrics(std::vector<float>& values);
  std::shared_ptr<Trainer> trainer_;

  std::vector<std::string> visdomLines_;
  hires_clock::time_point lastEpochStamp_;
  int lastEpochUpdateNum_ = 0;
};
} // namespace cpid
