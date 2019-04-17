/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "cherrypi.h"
#include "src/gameutils/openbwprocess.h"

#include <torchcraft/client.h>

#include <vector>

namespace cherrypi {

class State;

struct ReplayerConfiguration {
  std::string replayPath;
  bool forceGui = false;
  int combineFrames = 3;
};

/**
 * Play back a Brood War replay using OpenBW
 *
 * Provides a TorchCraft view of the game state.
 */
class TCReplayer {
 public:
  TCReplayer(std::string replayPath);
  TCReplayer(ReplayerConfiguration);
  virtual ~TCReplayer(){};

  torchcraft::State* tcstate() const;

  void init();
  void step();
  void run();
  bool isComplete() {
    return tcstate()->game_ended;
  }

  virtual void onStep(){};

 protected:
  ReplayerConfiguration configuration_;
  std::unique_ptr<OpenBwProcess> openbw_;
  std::shared_ptr<torchcraft::Client> client_;
  bool initialized_ = false;
};

/**
 * Play back a Brood War replay using OpenBW
 *
 * Runs the bot alongside the replay, and provides access to the bot's state.
 */
class Replayer : public TCReplayer {
 public:
  Replayer(std::string replayPath);
  Replayer(ReplayerConfiguration);
  virtual ~Replayer() override{};

  /// Convenience wrapper for State::setPerspective()
  void setPerspective(PlayerId);

  State* state();

  virtual void onStep() override;

 protected:
  std::unique_ptr<State> state_;
};

} // namespace cherrypi
