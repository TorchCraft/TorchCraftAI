/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "replayer.h"

#include "openbwprocess.h"
#include "state.h"

namespace cherrypi {

namespace {

std::unique_ptr<OpenBwProcess> startOpenBw(
    std::string replayPath,
    bool forceGui = false) {
  auto envVars = std::vector<OpenBwProcess::EnvVar>{
      {"BWAPI_CONFIG_AUTO_MENU__GAME_TYPE", "MELEE", true},
      {"BWAPI_CONFIG_AUTO_MENU__MAP", replayPath.c_str(), true},
      {"OPENBW_ENABLE_UI", (forceGui ? "1" : "0"), forceGui},
  };
  return std::make_unique<OpenBwProcess>(std::move(envVars));
}

} // namespace

Replayer::Replayer(std::string replayPath, bool forceGui) {
  openbw_ = startOpenBw(std::move(replayPath), forceGui);

  // Setup client
  client_ = std::make_shared<tc::Client>();
  if (!openbw_->connect(client_.get(), 10000)) {
    throw std::runtime_error(
        std::string("Error establishing connection: ") + client_->error());
  }

  // Perform handshake
  tc::Client::Options opts;
  std::vector<std::string> upd;
  if (!client_->init(upd, opts)) {
    throw std::runtime_error(
        std::string("Error initializing connection: ") + client_->error());
  }
  if (!client_->state()->replay) {
    throw std::runtime_error("Expected replay map");
  }

  state_ = std::make_unique<State>(client_);
}

Replayer::~Replayer() {}

void Replayer::setPerspective(PlayerId playerId) {
  state_->setPerspective(playerId);
}

void Replayer::setCombineFrames(int n) {
  if (initialized_) {
    throw std::runtime_error("Set combineFrames before calling init()");
  }
  combineFrames_ = n;
}

State* Replayer::state() {
  return state_.get();
}

size_t Replayer::steps() const {
  return steps_;
}

void Replayer::init() {
  steps_ = 0;

  std::vector<tc::Client::Command> comms;
  comms.emplace_back(tc::BW::Command::SetSpeed, 0);
  comms.emplace_back(
      tc::BW::Command::SetCombineFrames, combineFrames_, combineFrames_);
  comms.emplace_back(tc::BW::Command::SetMaxFrameTimeMs, 0);
  comms.emplace_back(tc::BW::Command::SetBlocking, false);
  if (!client_->send(comms)) {
    throw std::runtime_error(std::string("Send failure: ") + client_->error());
  }
  initialized_ = true;
}

void Replayer::step() {
  if (state_->gameEnded()) {
    // Return here if the game is over. Otherwise, client_->receive() will
    // just wait and time out eventually.
    VLOG(0) << "Game did end already";
    return;
  }

  std::vector<std::string> updates;
  if (!client_->receive(updates)) {
    throw std::runtime_error(
        std::string("Receive failure: ") + client_->error());
  }
  setLoggingFrame(client_->state()->frame_from_bwapi);

  state_->update();
  if (state_->gameEnded()) {
    unsetLoggingFrame();
    return;
  }

  state_->board()->checkPostStep();
  unsetLoggingFrame();
}

void Replayer::run() {
  init();
  do {
    step();
  } while (!state_->gameEnded());
}

} // namespace cherrypi
