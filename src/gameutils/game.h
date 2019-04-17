/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "cherrypi.h"
#include "fifopipes.h"
#include "gametype.h"
#include "openbwprocess.h"
#include "torchcraftclient.h"

#include "utils/syntax.h"
#include <torchcraft/client.h>

#include <memory>
#include <string>

namespace cherrypi {

class OpenBwProcess;
class PlayScript;

struct GamePlayerOptions {
  explicit GamePlayerOptions(torchcraft::BW::Race race) : race_(race) {}
  CPI_ARG(torchcraft::BW::Race, race);
  CPI_ARG(std::string, name);
};

struct GameOptions {
  explicit GameOptions(std::string map) : map_(map) {}
  CPI_ARG(std::string, map);
  CPI_ARG(std::string, replayPath);
  CPI_ARG(bool, forceGui) = false;
  CPI_ARG(GameType, gameType) = GameType::UseMapSettings;
};

class GameMultiPlayer {
 public:
  GameMultiPlayer(
      GameOptions const& gameOptions,
      GamePlayerOptions const& player1,
      GamePlayerOptions const& player2);
  // Deprecated constructor
  GameMultiPlayer(
      std::string const& map,
      tc::BW::Race race1,
      tc::BW::Race race2,
      GameType gameType = GameType::Melee,
      std::string const& replayPath = std::string(),
      bool forceGui = false);

  std::shared_ptr<torchcraft::Client> makeClient1(
      torchcraft::Client::Options opts = torchcraft::Client::Options());
  std::shared_ptr<torchcraft::Client> makeClient2(
      torchcraft::Client::Options opts = torchcraft::Client::Options());

 protected:
  detail::FifoPipes pipes_;
  std::shared_ptr<OpenBwProcess> proc1_;
  std::shared_ptr<OpenBwProcess> proc2_;
};

/**
 * A constructed gameplay scenario for training/testing purposes
 *
 * A scenario is defined by the commands that should be executed when the game
 * starts.
 * For example, it can spawn units, ask them to move at a certain place,...'
 */
class GameSinglePlayer {
 public:
  GameSinglePlayer(
      GameOptions const& gameOptions,
      GamePlayerOptions const& player1,
      GamePlayerOptions const& player2 = GamePlayerOptions(tc::BW::Race::None));
  GameSinglePlayer(GameSinglePlayer&&) = default;

  std::shared_ptr<torchcraft::Client> makeClient(
      torchcraft::Client::Options opts = torchcraft::Client::Options()) const;

 protected:
  std::unique_ptr<OpenBwProcess> startProcess() const;
  std::unique_ptr<OpenBwProcess> proc_;
  GameSinglePlayer() = default;
};

// Deprecated constructors, for backward compatibility
GameSinglePlayer GameSinglePlayerUMS(
    std::string const& map,
    std::string const& race,
    bool forceGui = false);
GameSinglePlayer GameSinglePlayerMelee(
    std::string map,
    std::string myRace,
    std::string enemyRace = std::string(),
    bool forceGui = false);
} // namespace cherrypi
