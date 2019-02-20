/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "player.h"

#include "utils.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <thread>
#include <tuple>

#include <glog/logging.h>

namespace cherrypi {

Player::Player(std::shared_ptr<tc::Client> client) : BasePlayer(client) {}

void Player::setFrameskip(int n) {
  if (initialized_) {
    throw std::runtime_error("Set frameskip before calling init()");
  }
  frameskip_ = n;
}

void Player::setCombineFrames(int n) {
  if (initialized_) {
    throw std::runtime_error("Set combineFrames before calling init()");
  }
  combineFrames_ = n;
}

void Player::setNonBlocking(bool nonBlocking) {
  if (initialized_) {
    throw std::runtime_error("Set nonBlocking before calling init()");
  }
  nonBlocking_ = nonBlocking;
}

void Player::init() {
  steps_ = 0;

  // Don't allow picking up existing games
  if (state_->currentFrame() > 0) {
    throw std::runtime_error(
        "Expecting fresh game in Player::init(), but current frame is " +
        std::to_string(state_->currentFrame()));
  }

  // Initial setup
  ClientCommands comms;
  comms.emplace_back(tc::BW::Command::SetSpeed, 0);
  comms.emplace_back(tc::BW::Command::SetGui, 1);
  comms.emplace_back(tc::BW::Command::SetCombineFrames, combineFrames_);
  comms.emplace_back(tc::BW::Command::SetFrameskip, frameskip_);
  comms.emplace_back(tc::BW::Command::SetBlocking, !nonBlocking_);
  comms.emplace_back(
      tc::BW::Command::SetMaxFrameTimeMs,
      std::chrono::duration_cast<std::chrono::milliseconds>(
          kMaxInitialStepDuration)
          .count());
  if (mapHack_) {
    comms.emplace_back(tc::BW::Command::MapHack);
  }
  if (!client_->send(comms)) {
    throw std::runtime_error(std::string("Send failure: ") + client_->error());
  }

  for (auto& module : modules_) {
    module->onGameStart(state_);
  }

  lastStep_ = hires_clock::now();
  initialized_ = true;
  firstStepDone_ = false;
}

void Player::run() {
  init();
  do {
    step();
  } while (!state_->gameEnded());
}

} // namespace cherrypi
