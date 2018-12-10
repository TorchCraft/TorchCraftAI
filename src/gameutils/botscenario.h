/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <memory>
#include <string>

#include <torchcraft/client.h>

#include "forkserver.h"
#include "selfplayscenario.h"

namespace cherrypi {

class OpenBwProcess;

class PlayScript;

/**
 * Launches a game against a DLL bot using OpenBW.
 *
 * This scenario is backed by OpenBwProcess.
 */
class BotScenario {
 public:
  BotScenario(
      std::string const& map,
      torchcraft::BW::Race myRace,
      std::string const& enemyBot,
      GameType gameType = GameType::Melee,
      std::string const& replayPath = std::string(),
      bool forceGui = false);

  std::shared_ptr<torchcraft::Client> makeClient(
      torchcraft::Client::Options opts = torchcraft::Client::Options());

 private:
  detail::FifoPipes pipes_;
  std::shared_ptr<OpenBwProcess> proc1_;
  std::shared_ptr<OpenBwProcess> proc2_;
};

/**
 * Launches a game series against a bot using StarCraft: Brood War(TM) via Wine.
 *
 * makeClient() can be called repeatedly to advance the series after each game.
 *
 * This scenario is backed by PlayScript.
 */
class PlayScriptScenario {
 public:
  PlayScriptScenario(
      std::vector<std::string> maps,
      std::string enemyBot,
      std::string outputPath = "playoutput",
      std::vector<EnvVar> vars = {});
  PlayScriptScenario(
      std::string map,
      std::string enemyBot,
      std::string outputPath = "playoutput",
      std::vector<EnvVar> vars = {});
  ~PlayScriptScenario();

  /// Construct a client to connect to a new game in the series.
  std::shared_ptr<torchcraft::Client> makeClient(
      torchcraft::Client::Options opts = torchcraft::Client::Options());

  /// Whether to automatically delete the series path when destructing the
  /// instance.
  void setAutoDelete(bool autoDelete);

  /// The number of games that have been started in this series.
  /// Calling makeClient() will increase this counter.
  size_t numGamesStarted() const {
    return numGamesStarted_;
  }

  /// Path to playoutput directory for this series
  std::string path() const {
    return path_;
  }

  /// Enemy bot as specified in the constructor
  std::string enemyBot() const {
    return enemyBot_;
  }

 private:
  std::shared_ptr<PlayScript> proc_;
  std::string enemyBot_;
  size_t numGamesStarted_ = 0;
  std::string path_;
  bool autoDelete_ = false;
};

} // namespace cherrypi
