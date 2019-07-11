/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#pragma once

#include <fstream>
#include <thread>

#include <fmt/format.h>
#include <glog/logging.h>

#include "common/autograd.h"
#include "cpid/checkpointer.h"
#include "cpid/cpid2kworker.h"
#include "utils.h"

#include "flags.h"
#include "micromodule.h"

using namespace microbattles;
using namespace cherrypi;

struct TrainingState {
  // Training stats
  std::mutex statMutex;
  std::shared_ptr<cpid::MetricsContext> metrics =
      std::make_shared<cpid::MetricsContext>();
  std::atomic<long> throughputCounter;
  std::atomic<int> numTrainEpisodes;
  std::atomic<int> numUpdates;
  torch::OrderedDict<std::string, torch::Tensor> previousModelParams;
  std::map<std::string, float> trainMetrics;
  std::map<std::string, float> testMetrics;
  std::map<std::string, float> trainCacheMetrics;
  std::map<std::string, float> testCacheMetrics;
  std::unordered_map<std::string, std::vector<float>> baselineRewards;
  std::string baselineDumpPath = "";
  std::string baselineLoadPath = "";

  double avgSteps = 0;
  double avgReward = 0;

  std::vector<float> testWins;
  std::vector<float> testKills;
  std::vector<float> testDamage;
  std::vector<float> testSurviveByHp;
  std::vector<float> testSurviveByCount;

  // Actual state variables
  // Set to 'true' upon SIGINT (CTRL+C)
  std::atomic_bool shouldExit{false};

  std::shared_ptr<microbattles::TrainingSetup> setup;
  std::unique_ptr<cpid::Checkpointer> checkpointer;
  std::unique_ptr<cpid::Cpid2kWorker> worker;
  cherrypi::hires_clock::time_point startTime;

  bool testing = false;

  void clearTest();

  void dumpBaselineRewards();

  // Use for computing delta between model updates
  void saveModelParams();

  std::unordered_map<std::string, float> getDelta();

  std::string computeReplayPath(unsigned int threadId, uint64_t gamesPlayed);

  void addStatsTraining(std::shared_ptr<BasePlayer> player);

  void addStatsTesting(std::shared_ptr<BasePlayer> player);

  void printTestResult();

  void loadBaselineRewards();
};
