/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "replayer.h"

#include "common/logging.h"
#include "gameutils/openbwprocess.h"
#include "state.h"

namespace cherrypi {

namespace {

std::unique_ptr<OpenBwProcess> startOpenBw(
    std::string replayPath,
    bool forceGui = false) {
  auto envVars = std::vector<EnvVar>{
      {"BWAPI_CONFIG_AUTO_MENU__GAME_TYPE", "MELEE", true},
      {"BWAPI_CONFIG_AUTO_MENU__MAP", replayPath.c_str(), true},
      {"OPENBW_ENABLE_UI", (forceGui ? "1" : "0"), forceGui},
  };
  return std::make_unique<OpenBwProcess>(std::move(envVars));
}

} // namespace

TCReplayer::TCReplayer(std::string replayPath)
    : TCReplayer(ReplayerConfiguration{replayPath}) {}

TCReplayer::TCReplayer(ReplayerConfiguration configuration)
    : configuration_(configuration) {
  openbw_ = startOpenBw(configuration_.replayPath, configuration_.forceGui);

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
  if (!tcstate()->replay) {
    throw std::runtime_error("Expected replay map");
  }
}

torchcraft::State* TCReplayer::tcstate() const {
  return client_->state();
}

void TCReplayer::init() {
  std::vector<tc::Client::Command> commands;
  commands.emplace_back(tc::BW::Command::SetSpeed, 0);
  commands.emplace_back(
      tc::BW::Command::SetCombineFrames,
      configuration_.combineFrames,
      configuration_.combineFrames);
  commands.emplace_back(tc::BW::Command::SetMaxFrameTimeMs, 0);
  commands.emplace_back(tc::BW::Command::SetBlocking, false);
  if (!client_->send(commands)) {
    throw std::runtime_error(
        std::string("Failed to send commands: ") + client_->error());
  }
  initialized_ = true;
}

void TCReplayer::step() {
  if (isComplete()) {
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

  onStep();
}

void TCReplayer::run() {
  init();
  do {
    step();
  } while (!isComplete());
}

// Replayer

Replayer::Replayer(std::string replayPath)
    : Replayer(ReplayerConfiguration{replayPath}) {}

Replayer::Replayer(ReplayerConfiguration configuration)
    : TCReplayer(configuration) {
  state_ = std::make_unique<State>(client_);
}

State* Replayer::state() {
  return state_.get();
}

void Replayer::setPerspective(PlayerId playerId) {
  state_->setPerspective(playerId);
}

void Replayer::onStep() {
  common::setLoggingFrame(tcstate()->frame_from_bwapi);

  state_->update();
  if (!isComplete()) {
    state_->board()->checkPostStep();
  }

  common::unsetLoggingFrame();
}

} // namespace cherrypi
