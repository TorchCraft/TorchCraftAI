/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "cherrypi.h"

#include <torchcraft/client.h>

#include <vector>

namespace cherrypi {

class OpenBwProcess;
class State;

/**
 * Playback a BroodWar replay using OpenBW.
 *
 * This class allows you to playback an existing BroodWar replay (.rep file)
 * step-by-step with access to the usual common bot state object.
 */
class Replayer {
 public:
  Replayer(std::string replayPath, bool forceGui = false);
  ~Replayer();

  /// Convenience wrapper for State::setPerspective()
  void setPerspective(PlayerId playerId);

  /// Combine n server-side frames before taking any action.
  /// Set this before calling init().
  void setCombineFrames(int n);

  State* state();
  size_t steps() const;

  void init();
  void step();
  void run();

 private:
  std::unique_ptr<OpenBwProcess> openbw_;
  std::shared_ptr<tc::Client> client_;
  std::unique_ptr<State> state_;
  size_t steps_ = 0;
  int combineFrames_ = 3;
  bool initialized_ = false;
};

} // namespace cherrypi
