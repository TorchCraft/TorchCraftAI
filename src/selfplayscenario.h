/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <memory>
#include <string>

#include <torchcraft/client.h>

namespace cherrypi {

class OpenBwProcess;

enum class GameType {
  Melee,
  UseMapSettings,
};

namespace detail {
struct FifoPipes {
  std::string pipe1;
  std::string pipe2;

  FifoPipes();
  ~FifoPipes();

 private:
  std::string root_;
};

template <typename procT>
std::shared_ptr<torchcraft::Client>
makeClient(procT&& proc, torchcraft::Client::Options opts, int timeout) {
  auto client = std::make_shared<torchcraft::Client>();
  if (!proc->connect(client.get(), timeout)) {
    throw std::runtime_error(
        std::string("Error establishing connection: ") + client->error());
  }

  // Perform handshake
  std::vector<std::string> upd;
  if (!client->init(upd, opts)) {
    throw std::runtime_error(
        std::string("Error initializing connection: ") + client->error());
  }

  return client;
}

inline char const* gameTypeName(GameType type) {
  switch (type) {
    case GameType::Melee:
      return "MELEE";
    case GameType::UseMapSettings:
      return "USE_MAP_SETTINGS";
    default:
      break;
  }
  throw std::runtime_error("Unknown game type");
}
} // namespace detail

class SelfPlayScenario {
 public:
  SelfPlayScenario(
      std::string const& map,
      torchcraft::BW::Race race1,
      torchcraft::BW::Race race2,
      GameType gameType = GameType::Melee,
      std::string const& replayPath = std::string(),
      bool forceGui = false);

  std::shared_ptr<torchcraft::Client> makeClient1(
      torchcraft::Client::Options opts = torchcraft::Client::Options());
  std::shared_ptr<torchcraft::Client> makeClient2(
      torchcraft::Client::Options opts = torchcraft::Client::Options());

 protected:
  detail::FifoPipes pipes_;
  std::shared_ptr<OpenBwProcess> proc1_;
  std::shared_ptr<OpenBwProcess> proc2_;
};

} // namespace cherrypi
