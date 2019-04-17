/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "baseplayer.h"

namespace cherrypi {

/**
 * The main bot object for training scenarios.
 *
 * This class is used to play StarCraft Broodwar (TM) via the TorchCraft bridge.
 * The behavior and actions of the player are determined by a user-supplied list
 * of bot modules.
 *
 * In contrast to Player, this class does not provide convenience methods for
 * initializing a game and running it until the end -- it's assumed that users
 * of this class handle this. Instead, onGameStart() and onGameEnd() are
 * exposed, which call the respective functions of all bot modules that have
 * been added to the player. The rationale for this is to enable repeated
 * usage or instantiation of MicroPlayers during a single TorchCraft session.
 */
class MicroPlayer : public BasePlayer {
  using ClientCommands = std::vector<tc::Client::Command>;

 public:
  MicroPlayer(std::shared_ptr<tc::Client> client);
  MicroPlayer(const MicroPlayer&) = delete;
  MicroPlayer& operator=(const MicroPlayer&) = delete;

  void onGameStart();
  void onGameEnd();

 protected:
  bool gameStarted_ = false;
};

} // namespace cherrypi
