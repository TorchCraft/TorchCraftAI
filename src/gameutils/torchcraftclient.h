/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <torchcraft/client.h>

#include <memory>
#include <string>
#include <vector>

namespace cherrypi {

template <typename procT>
std::shared_ptr<torchcraft::Client> makeTorchCraftClient(
    procT&& proc,
    torchcraft::Client::Options opts,
    int timeout) {
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

} // namespace cherrypi