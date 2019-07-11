#include "trainingstate.h"

#include <common/fsutils.h>
#include <nlohmann/json.hpp>

namespace dist = cpid::distributed;

void TrainingState::dumpBaselineRewards() {
  if (baselineDumpPath == "") {
    return;
  }
  std::ofstream os(baselineDumpPath);
  auto output = [&]() {
    std::lock_guard<std::mutex> lock(statMutex);
    return nlohmann::json{
        {"rewards", baselineRewards},
    };
  }();
  os << output;
}

void TrainingState::loadBaselineRewards() {
  if (baselineLoadPath == "") {
    return;
  }
  std::ifstream is(baselineLoadPath);
  nlohmann::json rewards;
  is >> rewards;
  std::lock_guard<std::mutex> lock(statMutex);
  baselineRewards = rewards.at("rewards").get<decltype(baselineRewards)>();
}

void TrainingState::saveModelParams() {
  previousModelParams.clear();
  for (auto& pair : setup->trainer->model()->named_parameters()) {
    previousModelParams.insert(pair.key(), pair.value().clone());
  }
}

std::unordered_map<std::string, float> TrainingState::getDelta() {
  std::unordered_map<std::string, float> delta;
  if (previousModelParams.size() == 0) {
    return delta;
  }
  for (auto& pair : setup->trainer->model()->named_parameters()) {
    delta.insert({pair.key(),
                  ((pair.value() - previousModelParams[pair.key()]) /
                   previousModelParams[pair.key()])
                      .abs()
                      .median()
                      .item<float>()});
  }
  return delta;
}

void TrainingState::clearTest() {
  testWins.clear();
  testKills.clear();
  testDamage.clear();
  testMetrics.clear();
  testCacheMetrics.clear();
}

std::string TrainingState::computeReplayPath(
    unsigned int threadId,
    uint64_t gamesPlayed) {
  if ((rand() % std::max<uint64_t>(FLAGS_dump_replays_rate, 1UL)) != 0) {
    return "";
  }
  if (FLAGS_dump_replays == "never") {
    return "";
  } else if (FLAGS_dump_replays == "eval" && !testing) {
    return "";
  } else if (FLAGS_dump_replays == "train" && testing) {
    return "";
  }
  std::string folder = fmt::format(
      "{}/replays-{}/upd{}",
      FLAGS_results,
      (testing ? "eval" : "train"),
      numUpdates.load());
  common::fsutils::mkdir(folder);
  return fmt::format(
      "{}/rank{}_thread{}_game{}.rep",
      folder,
      cpid::distributed::globalContext()->rank,
      threadId,
      gamesPlayed);
}

void TrainingState::addStatsTraining(std::shared_ptr<BasePlayer> player) {
  auto microModule = findMicroModule(player);
  auto frame = microModule->episodeEndFrame;
  auto reward = microModule->frameReward;

  // Do stats
  numTrainEpisodes++;
  metrics->pushEvents(
      "episodeStats",
      {float(numUpdates.load()),
       float(frame),
       float(numTrainEpisodes.load()),
       float(reward),
       microModule->lastAllyCount,
       microModule->firstAllyCount,
       microModule->lastAllyHp,
       microModule->firstAllyHp,
       microModule->lastEnemyCount,
       microModule->firstEnemyCount,
       microModule->lastEnemyHp,
       microModule->firstEnemyHp});
  metrics->incCounter("episodes");

  std::lock_guard<std::mutex> lock(statMutex);
  constexpr float kExpAvgDecay = 0.99;
  auto a = (1 - kExpAvgDecay) / (1 - std::pow(kExpAvgDecay, numTrainEpisodes));
  trainMetrics["avgReward"] = trainMetrics["avgReward"] * (1 - a) + reward * a;
  trainMetrics["avgSteps"] = trainMetrics["avgSteps"] * (1 - a) + frame * a;
  for (auto const& [key, val] : microModule->numericMetrics) {
    trainMetrics["avg" + key] = trainMetrics["avg" + key] * (1 - a) + val * a;
  }

  for (auto const& [key, val] : microModule->numericMetricsByUnit) {
    int unitIdx = 0;
    for (auto const& [unit, value] : val) {
      trainCacheMetrics["total" + key + std::to_string(unitIdx)] += 1;
      auto aStar = (1 - kExpAvgDecay) /
          (1 -
           std::pow(
               kExpAvgDecay,
               trainCacheMetrics["total" + key + std::to_string(unitIdx)]));
      trainMetrics["avg" + key + std::to_string(unitIdx)] =
          trainMetrics["avg" + key + std::to_string(unitIdx)] * (1 - aStar) +
          value * aStar;
      unitIdx++;
    }
  }

  for (auto& [key, val] : microModule->vectorMetrics) {
    auto metricsTensor =
        at::CPU(at::kFloat).tensorFromBlob(val.data(), val.size());
    trainMetrics["avg" + key + "Mean"] =
        trainMetrics["avg" + key + "Mean"] * (1 - a) +
        metricsTensor.mean().item<float>() * a;
  }
}

void TrainingState::addStatsTesting(std::shared_ptr<BasePlayer> player) {
  auto microModule = findMicroModule(player);
  auto frame = microModule->episodeEndFrame;
  auto reward = microModule->frameReward;
  auto won = (float)microModule->won;

  // Do stats
  metrics->incCounter("testEpisodes");
  metrics->pushEvents(
      "testEpisodeStats",
      {float(numUpdates.load()),
       float(frame),
       float(numTrainEpisodes.load()),
       float(reward),
       microModule->lastAllyCount,
       microModule->firstAllyCount,
       microModule->lastAllyHp,
       microModule->firstAllyHp,
       microModule->lastEnemyCount,
       microModule->firstEnemyCount,
       microModule->lastEnemyHp,
       microModule->firstEnemyHp});
  std::lock_guard<std::mutex> lock(statMutex);
  if (FLAGS_print_rewards) {
    baselineRewards[microModule->scenarioName] = microModule->frameRewards;
  }
  testMetrics["avgReward"] += reward / FLAGS_num_test_episodes;
  testMetrics["avgSteps"] += frame / FLAGS_num_test_episodes;
  testMetrics["avgWinLossReward"] +=
      (won == 0. ? -1. : 1.) / FLAGS_num_test_episodes;
  for (auto const& [key, val] : microModule->numericMetrics) {
    testMetrics["avg" + key] += val / FLAGS_num_test_episodes;
  }

  for (auto const& [key, val] : microModule->numericMetricsByUnit) {
    int unitIdx = 0;
    for (auto const& [unit, value] : val) {
      testMetrics["avg" + key + std::to_string(unitIdx)] += value;
      testCacheMetrics["total" + key + std::to_string(unitIdx)] += 1;
      unitIdx++;
    }
  }

  for (auto& [key, val] : microModule->vectorMetrics) {
    auto metricsTensor =
        at::CPU(at::kFloat).tensorFromBlob(val.data(), val.size());
    testMetrics["avg" + key + "Mean"] += metricsTensor.mean().item<float>();
  }
  testWins.push_back(won > 0);
  testKills.push_back(
      1 - microModule->lastEnemyCount / microModule->firstEnemyCount);
  testDamage.push_back(
      1 - microModule->lastEnemyHp / microModule->firstEnemyHp);
  testSurviveByHp.push_back(microModule->lastAllyHp / microModule->firstAllyHp);
  testSurviveByCount.push_back(
      microModule->lastAllyCount / microModule->firstAllyCount);
}

void TrainingState::printTestResult() {
  std::lock_guard<std::mutex> lock(statMutex);
  auto currentNumUpdates = numUpdates.load();
  auto testWinTensor =
      at::CPU(at::kFloat)
          .tensorFromBlob(testWins.data(), {(long)testWins.size()});
  auto testKillTensor =
      at::CPU(at::kFloat)
          .tensorFromBlob(testKills.data(), {(long)testKills.size()});
  auto testDamageTensor =
      at::CPU(at::kFloat)
          .tensorFromBlob(testDamage.data(), {(long)testDamage.size()});
  auto testSurviveByHpTensor =
      at::CPU(at::kFloat)
          .tensorFromBlob(
              testSurviveByHp.data(), {(long)testSurviveByHp.size()});
  auto testSurviveByCountTensor =
      at::CPU(at::kFloat)
          .tensorFromBlob(
              testSurviveByCount.data(), {(long)testSurviveByCount.size()});

  fmt::print(
      "TEST: "
      "rank {}\t"
      "update {}\t"
      "length {:.3f}\t"
      "reward {:.3f}\t"
      "winLossReward {:.3f}\t"
      "wins {:.3f}|{:.3f}\t"
      "kills {:.3f}|{:.3f}\t"
      "damage {:.3f}|{:.3f}\t"
      "surviveByHp {:.3f}|{:.3f}\t"
      "surviveByCount {:.3f}|{:.3f}\t"
      "\n",
      dist::globalContext()->rank,
      currentNumUpdates,
      testMetrics["avgSteps"],
      testMetrics["avgReward"],
      testMetrics["avgWinLossReward"],
      testWinTensor.mean().item<float>(),
      testWinTensor.std().item<float>(),
      testKillTensor.mean().item<float>(),
      testKillTensor.std().item<float>(),
      testDamageTensor.mean().item<float>(),
      testDamageTensor.std().item<float>(),
      testSurviveByHpTensor.mean().item<float>(),
      testSurviveByHpTensor.std().item<float>(),
      testSurviveByCountTensor.mean().item<float>(),
      testSurviveByCountTensor.std().item<float>());
  metrics->pushEvent("length@Testing", testMetrics["avgSteps"]);
  metrics->pushEvent("avgReward@Testing", testMetrics["avgReward"]);
  metrics->pushEvent(
      "avgWinLossReward@Testing", testMetrics["avgWinLossReward"]);
  metrics->pushEvent("avgWR@Testing", testWinTensor.mean().item<float>());

  if (worker) {
    worker->appendMetrics(
        "test",
        {{"winrate", testWinTensor.mean().item<float>()},
         {"avgSteps", testMetrics["avgSteps"] / FLAGS_num_test_episodes},
         {"avgReward", testMetrics["avgReward"] / FLAGS_num_test_episodes},
         {"update", currentNumUpdates},
         {"trainEpisodes", numTrainEpisodes.load()}});
  }

  if (dist::globalContext()->rank == 0 && setup->vs) {
    setup->updatePlot(
        "wins",
        "Average Wining Games @Testing",
        "percentage of games",
        currentNumUpdates,
        testWinTensor.mean().item<float>());
    setup->updatePlot(
        "kills",
        "Average Killings @Testing",
        "percentage of all kills",
        currentNumUpdates,
        testKillTensor.mean().item<float>());
    for (auto const& [key, val] : testMetrics) {
      setup->updatePlot(key, key + " @Testing", "", currentNumUpdates, val);
    }
  }
  clearTest();
}
