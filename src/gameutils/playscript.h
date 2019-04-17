/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "forkserver.h"

#include <memory>
#include <string>
#include <vector>

namespace torchcraft {
class Client;
}

namespace cherrypi {

/*
 * Manages a series of games against opponent, cycling through maps.
 * This will create files on the local filesystem (check the play script)!
 * These files are not deleted.
 * The bot play script is external.
 */
class PlayScript {
 public:
  PlayScript(
      std::vector<EnvVar> const& vars,
      std::string script = "/workspace/scripts/ladder/play");
  ~PlayScript();

  /// Connect a TorchCraft client to this instance.
  /// Returns whether the client connected successfully connected.
  /// Note that this function can be called multiple times. After
  /// a game ends, it can be called again to start and connect to
  /// the next game in the series.
  bool connect(torchcraft::Client* client, int timeoutMs = -1);

 private:
  int waitReadyPipeFd_ = 0;
  int waitPipeFd_ = 0;
  int readPipeFd_ = 0;

  int nConnects_ = 0;

  int scriptPid_ = 0;
  int termPid_ = 0;
  int termPipeFd_ = 0;
};

} // namespace cherrypi
