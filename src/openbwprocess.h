/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <atomic>
#include <future>
#include <string>
#include <unordered_map>
#include <vector>

namespace torchcraft {
class Client;
}

namespace cherrypi {

/**
 * Launches and manages an OpenBW process.
 */
class OpenBwProcess {
 public:
  struct EnvVar {
    std::string key;
    std::string value;
    bool overwrite = false;
    template <class Archive>
    void serialize(Archive& archive) {
      archive(key, value, overwrite);
    }
  };

  // Totally thread safe!
  OpenBwProcess(std::vector<EnvVar> const& vars);
  OpenBwProcess(std::string, std::vector<EnvVar> const& vars);
  ~OpenBwProcess();

  /// Connect a TorchCraft client to this instance.
  /// The timeout is passed to the client _and_ the future to wait on openbw,
  /// and
  /// it is executed in sequence, so the total timeout might be 2 * timeout
  /// Returns whether the client connected successfully connected.
  bool connect(torchcraft::Client* client, int timeoutMs = -1);

  /// Starts a fork server, and uses this to spawn future openbw instances
  /// instead.
  /// After this function has been called, instantiating an OpenBwProcess will
  /// transparently launch the respective processes via the fork server.
  /// At program exit, the server will automatically be shut down.
  static void startForkServer();
  /// Manual shutdown of fork server.
  static void endForkServer();

 private:
  void redirectOutput();

  int pid_;
  bool launchedWithForkServer_;
  std::string socketPath_;
  int fd_ = -1;
  int wfd_ = -1;
  std::future<void> goodf_;
  std::promise<void> goodp_;
  // Need to keep a variable alive for thread to continue running
  std::future<void> outputThread_;
  std::atomic_bool running_;
};

} // namespace cherrypi
