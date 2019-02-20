/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifdef WITHOUT_POSIX
#include <ctime>
#include <tchar.h>
#include <windows.h>
// ERROR macro is defined in windows headers, hack for glog:
#define GLOG_NO_ABBREVIATED_SEVERITIES
#endif

#include "../buildorders/base.h"
#include "bandit.h"

#include <common/fsutils.h>
#include <common/rand.h>

#include <glog/logging.h>

#include <fstream>

namespace fsutils = common::fsutils;

namespace cherrypi {

namespace model {

std::vector<std::string> acceptableBuildOrders(
    const std::unordered_map<std::string, BuildOrderConfig>& configs,
    tc::BW::Race ourRace,
    tc::BW::Race enemyRace) {
  std::vector<std::string> acceptable;

  for (auto& configPair : configs) {
    auto& name = configPair.first;
    auto& config = configPair.second;

    if (!config.validOpening()) {
      continue;
    }

    auto enemyRaces = config.enemyRaces();
    if (std::find(enemyRaces.begin(), enemyRaces.end(), enemyRace) ==
        enemyRaces.end()) {
      continue;
    }

    auto ourRaces = config.ourRaces();
    if (std::find(ourRaces.begin(), ourRaces.end(), ourRace) ==
        ourRaces.end()) {
      continue;
    }

    acceptable.push_back(name);
  }
  return acceptable;
}

void BuildOrderCount::updateLastGame(bool won) {
  if (wins_.size() == 0) {
    throw std::runtime_error("Cannot update non-existing value");
  }
  wins_.pop_back();
  wins_.push_back(won);
}

EnemyHistory::EnemyHistory(
    std::string enemyName,
    std::string readFolder,
    std::string writeFolder)
    : enemyName_(std::move(enemyName)),
      readFolder_(std::move(readFolder)),
      writeFolder_(std::move(writeFolder)) {
  if (!fsutils::isdir(readFolder_)) {
    throw std::runtime_error("Read folder does not exist: " + readFolder_);
  }
  if (!fsutils::isdir(writeFolder_)) {
    throw std::runtime_error("Write folder does not exist: " + writeFolder_);
  }
  // load history if it exists
  // defaults to write if does not exist
  std::string filepath = readFilepath();
  std::ifstream ifs(filepath);
  if (ifs.good() && filepath != "") {
    cereal::JSONInputArchive ia(ifs);
    ia(buildOrderCounts);
  } else {
    VLOG(0) << "No history for opponent " << enemyName
            << ", initializing with default values";
  }
}

void EnemyHistory::addStartingGame(std::string buildOrder) {
  buildOrderCounts[buildOrder].addGame(false);
  write();
}

void EnemyHistory::updateLastGameToVictory(std::string buildOrder) {
  auto countIter = buildOrderCounts.find(buildOrder);
  if (countIter == buildOrderCounts.end()) {
    throw std::runtime_error(
        "updateLastGameToVictory should not be called if no game was started");
  }
  buildOrderCounts[buildOrder].updateLastGame(true);
  write();
}

void EnemyHistory::write() const {
  // write file
  std::string filepath = writeFilepath();
  std::ofstream ofs(filepath);
  if (!ofs.good()) {
    LOG(ERROR) << "Cannot write to " << filepath;
  } else {
    VLOG(0) << "Got |" << enemyName_ << "| saving history to " << filepath;
    cereal::JSONOutputArchive oa(ofs);
    oa(CEREAL_NVP(buildOrderCounts));
  }
}

void EnemyHistory::printStatus() const {
  VLOG(2) << "History status {";
  for (auto& count : buildOrderCounts) {
    VLOG(2) << "  " << count.first << " - " << count.second.statusString();
  }
  VLOG(2) << "} // History status";
}

namespace score {

///  Gets a sample from a Beta(a, b) if you have "x" a sample from a uniform in
///  0,1:
///    x^(a-1)*(1-x)^(b-1)/(gamma(a)*gamma(b)/gamma(a+b))
///    with gamma http://www.cplusplus.com/reference/cmath/tgamma/
float betaSampling(float x, float a, float b) {
  return std::pow(x, a - 1.) * std::pow(1. - x, b - 1.) /
      (tgamma(a) * tgamma(b) / tgamma(a + b));
}

float thompsonSamplingScore(
    cherrypi::model::BuildOrderCount const& count,
    float thompson_a,
    float thompson_b) {
  if (count.config.priority() == 0) { // we do not want to do this build, ever!
    return -1.0f;
  }
  float randval = static_cast<float>(common::Rand::rand()) /
      (static_cast<float>(RAND_MAX) + 1);
  return betaSampling(
      randval, count.numWins() + thompson_a, count.numLosses() + thompson_b);
}

float thompsonRollingSamplingScore(
    cherrypi::model::BuildOrderCount const& count,
    float thompson_a,
    float thompson_b,
    float thompson_gamma) {
  if (count.config.priority() == 0) { // we do not want to do this build, ever!
    return -1.0f;
  }
  float randval =
      static_cast<float>(std::rand()) / (static_cast<float>(RAND_MAX) + 1);
  float numWins = 0.0f;
  float numLosses = 0.0f;
  for (bool won : count.wins()) {
    numWins = thompson_gamma * numWins + (won ? 1.0 : 0.0);
    numLosses = thompson_gamma * numLosses + (won ? 0.0 : 1.0);
  }
  return betaSampling(randval, numWins + thompson_a, numLosses + thompson_b);
}

float ucb1Score(
    const cherrypi::model::BuildOrderCount& count,
    int allStrategyGamesCount,
    float ucb1_c) {
  if (count.config.priority() == 0) { // we do not want to do this build, ever!
    return -1.0f;
  }
  float score = 0.0f;
  if (count.numGames() < 1.) { // explore in order of ranking
    score = 10000.0 * count.config.priority();
  } else { // UCB1
    score = count.winRate() +
        std::sqrt(ucb1_c * std::log(allStrategyGamesCount) / count.numGames());
  }
  return score;
}

float ucb1RollingScore(
    const cherrypi::model::BuildOrderCount& count,
    int allStrategyGamesCount,
    float ucb1_c,
    float ucb1_gamma) {
  if (count.config.priority() == 0) { // we do not want to do this build, ever!
    return -1.0f;
  }
  float score = 0.0f;
  if (count.numGames() < 1.) { // explore in order of ranking
    score = 10000.0 * count.config.priority();
  } else { // UCB1
    float discountedNumGames = 0.0f;
    for (bool won : count.wins()) {
      score = ucb1_gamma * score + (1.0f - ucb1_gamma) * (won ? 1.0 : 0.0);
      discountedNumGames = ucb1_gamma * discountedNumGames + 1.0f;
    }
    float ratio = discountedNumGames / count.numGames();
    score += std::sqrt(
        ucb1_c * std::log(allStrategyGamesCount) / count.numGames() /
        ratio); // !!! /ratio hack
  }
  return score;
}

float expMooRollingSamplingScore(
    const cherrypi::model::BuildOrderCount& count,
    float moo_mult,
    float moo_gamma) {
  if (count.config.priority() == 0) { // we do not want to do this build, ever!
    return -1.0f;
  }
  float a = 1.0f - moo_gamma;
  float score = 0.0f;
  for (bool won : count.wins()) {
    score = (1 - a) * score + (won ? a : -a);
  }
  float weight = std::exp(score * moo_mult);
  auto dist = std::uniform_real_distribution<float>(0.0f, weight);
  return common::Rand::sample(dist);
}

float maxExploitScore(
    const cherrypi::model::BuildOrderCount& count,
    int allStrategyGamesCount,
    float ucb1_c) {
  float score = 0;
  if (count.config.priority() == 0) { // we do not want to do this build, ever!
    score = -1.0f;
  } else {
    if (count.winRate() > 0.969) { // exploit!
      score = cherrypi::kfInfty;
    } else if (count.numGames() < 1.) { // explore in order of ranking
      score = 10000.0 * count.config.priority();
    } else { // hedge, UCB1
      score = count.winRate() + // defensive
          std::sqrt(
                  ucb1_c * std::log(allStrategyGamesCount) / count.numGames());
    }
  }
  return score;
}

std::string chooseBuildOrder(
    const std::map<std::string, cherrypi::model::BuildOrderCount>&
        buildOrderCounts,
    std::string scoreAlgorithm,
    float ucb1_c,
    float bandit_gamma,
    float thompson_a,
    float thompson_b,
    float moo_mult) {
  float allStrategyGamesCount = 0;
  for (const auto& buildOrderCount : buildOrderCounts) {
    allStrategyGamesCount += buildOrderCount.second.numGames();
  }

  float bestScore = -1.0f;

  // default build order if things go wrong (should not happen)
  std::string bestBuildOrder = "5pool";
  VLOG(0) << "Selecting build order with scoring algorithm " << scoreAlgorithm;

  // only and all builds provided in buildOrderCounts are considered
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  for (const auto& buildOrderCount : buildOrderCounts) {
    float score = 0;
    if (scoreAlgorithm == kBanditUCB1) {
      score = ucb1Score(buildOrderCount.second, allStrategyGamesCount, ucb1_c);
    } else if (scoreAlgorithm == kBanditUCB1Rolling) {
      score = ucb1RollingScore(
          buildOrderCount.second, allStrategyGamesCount, ucb1_c, bandit_gamma);
    } else if (scoreAlgorithm == kBanditUCB1Exploit) {
      score = maxExploitScore(
          buildOrderCount.second, allStrategyGamesCount, ucb1_c);
    } else if (scoreAlgorithm == kBanditThompson) {
      score =
          thompsonSamplingScore(buildOrderCount.second, thompson_a, thompson_b);
    } else if (scoreAlgorithm == kBanditThompsonRolling) {
      score = thompsonRollingSamplingScore(
          buildOrderCount.second, thompson_a, thompson_b, bandit_gamma);
    } else if (scoreAlgorithm == kBanditExpMooRolling) {
      score = expMooRollingSamplingScore(
          buildOrderCount.second, moo_mult, bandit_gamma);
    } else if (scoreAlgorithm == kBanditNone) {
      score = 1.0f;
    } else if (scoreAlgorithm == kBanditRandom) {
      score = common::Rand::sample(dist);
    } else {
      throw std::invalid_argument("No scoreAlgorithm named: " + scoreAlgorithm);
    }
    VLOG(0) << fmt::format(
        "{0} ({1}) scored {2}.",
        buildOrderCount.first,
        buildOrderCount.second.statusString(),
        score);
    if (score > bestScore) {
      bestScore = score;
      bestBuildOrder = buildOrderCount.first;
    }
  }
  return bestBuildOrder;
}

} // namespace score

} // namespace model

} // namespace cherrypi
