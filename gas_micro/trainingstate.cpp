#include "trainingstate.h"
#include "gasmicromodule.h"
#include "gas_trainer.h"

#include <common/fsutils.h>

namespace dist = cpid::distributed;

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
}

std::string TrainingState::computeReplayPath(
    unsigned int threadId,
    uint64_t gamesPlayed) {
  if ((rand() % std::max(FLAGS_dump_replays_rate, 1UL)) != 0) {
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
  auto microModule = player->findModule<MicroModule>();
  auto gasMicroModule = std::dynamic_pointer_cast<GasMicroModule>(microModule);
  auto frame = microModule->currentFrame_;
  auto reward = (gasMicroModule == nullptr) ? microModule->frameReward_
                                            : microModule->totalReward_;
  float aCount, eCount, aHp, eHp;
  std::tie(aCount, eCount, aHp, eHp) = getUnitCountsHealth(player->state());

  // Do stats
  numTrainEpisodes++;
  std::vector<float> events = {
      float(numUpdates.load()),
      float(frame),
      float(numTrainEpisodes.load()),
      float(reward),
      aCount,
      microModule->firstAllyCount_,
      aHp,
      microModule->firstAllyHp_,
      eCount,
      microModule->firstEnemyCount_,
      eHp,
      microModule->firstEnemyHp_,
  };
  if (gasMicroModule != nullptr) {
    events.push_back(gasMicroModule->epsilon_);
    events.push_back(gasMicroModule->actLod_);
  }
  metrics->pushEvents("episodeStats", events);
  metrics->incCounter("episodes");

  std::lock_guard<std::mutex> lock(statMutex);
  constexpr float kExpAvgDecay = 0.99;
  auto a = (1 - kExpAvgDecay) / (1 - std::pow(kExpAvgDecay, numTrainEpisodes));
  trainMetrics["avgReward"] = trainMetrics["avgReward"] * (1 - a) + reward * a;
  trainMetrics["avgSteps"] = trainMetrics["avgSteps"] * (1 - a) + frame * a;
  for (auto const& [key, val] : microModule->numericMetrics_) {
    trainMetrics["avg" + key] = trainMetrics["avg" + key] * (1 - a) + val * a;
    ;
  }
  for (auto& [key, val] : microModule->vectorMetrics_) {
    auto metricsTensor =
        at::CPU(at::kFloat).tensorFromBlob(val.data(), val.size());
    trainMetrics["avg" + key + "Mean"] =
        trainMetrics["avg" + key + "Mean"] * (1 - a) +
        metricsTensor.mean().item<float>() * a;
  }
}

void TrainingState::addStatsTesting(std::shared_ptr<BasePlayer> player) {
  auto microModule = player->findModule<MicroModule>();
  auto frame = microModule->currentFrame_;
  auto reward = microModule->frameReward_;
  float aCount, eCount, aHp, eHp;
  std::tie(aCount, eCount, aHp, eHp) = getUnitCountsHealth(player->state());

  // Do stats
  metrics->incCounter("testEpisodes");
  metrics->pushEvents(
      "testEpisodeStats",
      {float(numUpdates.load()),
       float(frame),
       float(numTrainEpisodes.load()),
       float(reward),
       aCount,
       microModule->firstAllyCount_,
       aHp,
       microModule->firstAllyHp_,
       eCount,
       microModule->firstEnemyCount_,
       eHp,
       microModule->firstEnemyHp_});
  std::lock_guard<std::mutex> lock(statMutex);
  testMetrics["avgReward"] += reward;
  testMetrics["avgSteps"] += frame;
  for (auto const& [key, val] : microModule->numericMetrics_) {
    testMetrics["avg" + key] += val;
  }
  for (auto& [key, val] : microModule->vectorMetrics_) {
    auto metricsTensor =
        at::CPU(at::kFloat).tensorFromBlob(val.data(), val.size());
    testMetrics["avg" + key + "Mean"] += metricsTensor.mean().item<float>();
  }
  testWins.push_back(eCount == 0);
  testKills.push_back(1 - eCount / microModule->firstEnemyCount_);
  testDamage.push_back(1 - eHp / microModule->firstEnemyHp_);
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
  fmt::print(
      "TEST: "
      "rank {}\t"
      "update {}\t"
      "length {:.3f}\t"
      "reward {:.3f}\t"
      "wins {:.3f}|{:.3f}\t"
      "kills {:.3f}|{:.3f}\t"
      "damage {:.3f}|{:.3f}\t"
      "\n",
      dist::globalContext()->rank,
      currentNumUpdates,
      testMetrics["avgSteps"] / FLAGS_num_test_episodes,
      testMetrics["avgReward"] / FLAGS_num_test_episodes,
      testWinTensor.mean().item<float>(),
      testWinTensor.std().item<float>(),
      testKillTensor.mean().item<float>(),
      testKillTensor.std().item<float>(),
      testDamageTensor.mean().item<float>(),
      testDamageTensor.std().item<float>());
  if (FLAGS_gas_on_plateau > 0) {
    auto t = std::dynamic_pointer_cast<cpid::GasTrainer>(setup->trainer);
    t->updateBestMetric(testWinTensor.mean().item<float>()); 
  }
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
      setup->updatePlot(
          key,
          key + " @Testing",
          "",
          currentNumUpdates,
          val / FLAGS_num_test_episodes);
    }
  }
  clearTest();
}
