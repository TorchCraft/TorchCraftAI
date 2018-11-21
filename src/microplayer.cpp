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
    : BasePlayer(client) {}

void MicroPlayer::onGameStart() {
  for (auto& module : modules_) {
    module->onGameStart(state_);
  }
  lastStep_ = hires_clock::now();
}

void MicroPlayer::onGameEnd() {
  for (auto& module : modules_) {
    module->onGameEnd(state_);
  }
}

} // namespace cherrypi
