/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "baseplayer.h"
#include "cherrypi.h"
#include "module.h"
#include "state.h"

namespace cherrypi {

/**
 * The main bot object for complete games of StarCraft.
 *
 * This class is used to play StarCraft Broodwar (TM) via the TorchCraft bridge.
 * The behavior and actions of the player are determined by a user-supplied list
 * of bot modules.
 */
class Player : public BasePlayer {
  using ClientCommands = std::vector<tc::Client::Command>;

 public:
  Player(std::shared_ptr<tc::Client> client, const StateConfig& = {});
  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;

  /// UI update frequency of Broodwar instance. Set this before calling init().
  void setFrameskip(int n);
  /// Combine n server-side frames before taking any action.
  /// Set this before calling init().
  void setCombineFrames(int n);
  /// Run bot step in separate thread to prevent blocking game execution.
  /// Defaults to false.
  void setNonBlocking(bool nonBlocking);

  void setMapHack(bool on) {
    mapHack_ = on;
  }

  virtual void init() override;
  void run();

 protected:
  void init_();

 private:
  bool mapHack_ = false;
};

} // namespace cherrypi
