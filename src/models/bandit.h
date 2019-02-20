/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "cherrypi.h"
#include "fmt/format.h"
#include "utils.h"

#include <cereal/archives/json.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/polymorphic.hpp>
#include <cereal/types/vector.hpp>

#include <numeric>
#include <random>

static const std::string kBanditNone("none");
static const std::string kBanditRandom("random");
static const std::string kBanditUCB1("ucb1");
static const std::string kBanditUCB1Rolling("ucb1rolling");
static const std::string kBanditUCB1Exploit("ucb1exploit");
static const std::string kBanditThompson("thompson");
static const std::string kBanditThompsonRolling("thompsonrolling");
static const std::string kBanditExpMooRolling("expmoorolling");

namespace cherrypi {

namespace model {

/**
 * Defines a build order, from the standpoint of strategy selection.
 */
struct BuildOrderConfig {
  /// Whether this build order can be used from the beginning of the game
  CPI_ARG(bool, validOpening) = false;

  /// Whether Build Order Switch is allowed to swap into this
  CPI_ARG(bool, validSwitch) = false;

  /// Whether Build Order Switch is enabled with this opening
  CPI_ARG(bool, switchEnabled) = true;

  /// priority for UCB1 when testing unplayed builds
  CPI_ARG(int, priority) = 1;

  /// Which of our races are allowed to use this build order
  CPI_ARG(std::vector<tc::BW::Race>, ourRaces) = {tc::BW::Race::Zerg};

  /// Against which enemy races this build order is valid
  CPI_ARG(std::vector<tc::BW::Race>, enemyRaces) = {tc::BW::Race::Terran,
                                                    tc::BW::Race::Protoss,
                                                    tc::BW::Race::Zerg,
                                                    tc::BW::Race::Unknown};
};

typedef std::unordered_map<std::string, BuildOrderConfig>
    BuildOrderConfigurations;

/// Implements the default configuration of each build order
/// This function needs to be modified in order to update
/// the configuration.
BuildOrderConfigurations buildOrdersForTraining();

/// Returns tournament-specific build order configurations given an opponent
BuildOrderConfigurations buildOrdersForTournament(
    const std::string& rawOpponentName);

/// Returns a vector of acceptable build orders for fighting against
/// a given race.
std::vector<std::string> acceptableBuildOrders(
    const std::unordered_map<std::string, BuildOrderConfig>& configs,
    tc::BW::Race ourRace,
    tc::BW::Race enemyRace);

/// Handle on a vector of victory status for each game, giving
/// easy access to relevant figures
class BuildOrderCount {
 public:
  BuildOrderCount() {}

  /// Adds a value to the win history vector
  void addGame(bool won) {
    wins_.push_back(won);
  }
  /// Updates the last value of the win history vector
  void updateLastGame(bool won);
  int numWins() const {
    return std::accumulate(wins_.begin(), wins_.end(), 0);
  }
  int numGames() const {
    return wins_.size();
  }
  int numLosses() const {
    return numGames() - numWins();
  }
  float winRate() const {
    return numGames() == 0 ? 0.0f : static_cast<float>(numWins()) / numGames();
  }
  const std::vector<bool>& wins() const {
    return wins_;
  }

  /// Returns a string of type "{numWins}/{numGames}" which
  /// is only useful for fast debugging and testing
  std::string statusString() const {
    return fmt::format("{0}/{1}", numWins(), numGames());
  }
  template <class Archive>
  void serialize(Archive& ar) {
    ar(CEREAL_NVP(wins_));
  }

  /// Configuration for the build,
  /// providing acceptable races and priors
  /// This is not serialized, because the configuration
  /// needs to be implemented in one and only one
  /// location (see buildOrderConfigurations).
  /// It must therefore be populated when required.
  BuildOrderConfig config;

 private:
  std::vector<bool> wins_; // vector of "won" status per game
};

/// Class holding a playedGames vector for a given enemy.
/// History is loaded at instanciation from either read folder
/// (in priority) or write folder, or a new empty history is
/// created.
/// An updated version is saved when calling either "addPlayedGame",
/// "write" or "updateLastGameToVictory" methods.
/// Beware: it does not check that the file
/// was updated after it was loaded (i.e. at instance creation).
class EnemyHistory {
 public:
  EnemyHistory(
      std::string enemyName,
      std::string readFolder = "bwapi-data/read/",
      std::string writeFolder = "bwapi-data/write/");

  /// Records a failed game for the given build order, which will be updated on
  /// game end with the actual win status.
  /// This is done so that in case of crash, the game is accounted for as a
  /// crash.
  /// Updates the opponent file.
  void addStartingGame(std::string buildOrder);

  /// In case of won games, this modifies the last
  /// history into a won game (while it was set to
  /// loss as default)
  /// Updates the opponent file.
  void updateLastGameToVictory(std::string buildOrder);

  /// Writes the current win history for all builds
  /// into the opponnent file
  void write() const;

  /// Prints all strategies and their counts (for debugging)
  void printStatus() const;

  /// map from build order to its counts (number of played games,
  /// won games etc...)
  std::unordered_map<std::string, BuildOrderCount> buildOrderCounts;

  /// Path to the file where the history
  /// is recorded
  /// Defaults to returning writeFilepath if
  /// readFilepath does not exist.
  std::string readFilepath() const {
    return readFolder_ + "/" + enemyName_ + ".json";
  }

  /// Path to the file where the history
  /// is recorded (used as default if
  /// readFilepath() does not exists)
  std::string writeFilepath() const {
    return writeFolder_ + "/" + enemyName_ + ".json";
  }

 private:
  friend class cereal::access;
  std::string enemyName_;
  std::string readFolder_;
  std::string writeFolder_;
};

namespace score {

/// Computes a score for build order j based on Thompson sampling (stochastic)
///
/// For each opening you keep S and F
/// (Successes and Failures, or S and N and F = N-S).
///
/// a and b are hyperparameters. A good initial guess is a=1, b=1.
/// In a game, for each build order i that we can start with:
///   sample p_i in Beta(S_i + a, F_i + b) // here you get your stochasticity
///   j = argmax over all p_i
///   play with build order j
///   update S_j and F_j
float thompsonSamplingScore(
    cherrypi::model::BuildOrderCount const& count,
    float thompson_a,
    float thompson_b);

/// Computes a Thompson sampling score on a rolling average
/// (w/ exponential decay)
float thompsonRollingSamplingScore(
    cherrypi::model::BuildOrderCount const& count,
    float thompson_a,
    float thompson_b,
    float thompson_gamma);

/// Computes UCB1 score providing build order j:
/// (win_j / total_j) + sqrt(2 * log(sum(total)) / total_j)
/// Untested build orders get a priority
float ucb1Score(
    cherrypi::model::BuildOrderCount const& count,
    int allStrategyGamesCount,
    float ucb1_c);

/// Computes UCB1 score on a rolling average (w/ exponential decay)
float ucb1RollingScore(
    cherrypi::model::BuildOrderCount const& count,
    int allStrategyGamesCount,
    float ucb1_c,
    float ucb1_gamma);

/// Computes Exp Moo score on a rolling average (w/ exponential decay)
float expMooRollingSamplingScore(
    cherrypi::model::BuildOrderCount const& count,
    float moo_mult,
    float moo_gamma);

/// Computes UCB1-style score providing build order j:
/// (win_j / total_j) + sqrt(2 * log(sum(total)) / total_j)
/// but builds with high win rate get first priority
/// Untested build orders get second priority
float maxExploitScore(
    cherrypi::model::BuildOrderCount const& count,
    int allStrategyGamesCount,
    float ucb1_c);

/// Chooses the build order with maximum score according
/// to the provided scoring algorithm
/// The assumption is that this is called once per game,
/// or at least acted upon based on the last call!
std::string chooseBuildOrder(
    std::map<std::string, cherrypi::model::BuildOrderCount> const&
        buildOrderCounts,
    std::string scoreAlgorithm,
    float ucb1_c,
    float bandit_gamma,
    float thompson_a,
    float thompson_b,
    float moo_mult);

} // namespace score

} // namespace model

} // namespace cherrypi
