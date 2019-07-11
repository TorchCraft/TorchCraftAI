/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "microplayer.h"

#include <glog/logging.h>

namespace cherrypi {

MicroPlayer::MicroPlayer(std::shared_ptr<tc::Client> client)
    : BasePlayer(client, []() {
        StateConfig config;
        config.guaranteeEnemy = true;
        return config;
      }()) {}

void MicroPlayer::onGameStart() {
  if (!gameStarted_) {
    for (auto& module : modules_) {
      module->onGameStart(state_);
    }
  }
  lastStep_ = hires_clock::now();
  gameStarted_ = true;
}

void MicroPlayer::onGameEnd() {
  if (gameStarted_) {
    for (auto& module : modules_) {
      module->onGameEnd(state_);
    }
  }
  gameStarted_ = false;
}

} // namespace cherrypi
