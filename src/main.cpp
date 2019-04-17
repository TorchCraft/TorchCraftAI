/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "upcstorage.h"

#include <glog/logging.h>
#include <torchcraft/client.h>

#include "botcli-inl.h"

using namespace cherrypi;

DEFINE_string(hostname, "127.0.0.1", "Host running TorchCraft server");
DEFINE_int32(port, 11111, "Port that TorchCraft server is listening to");
DEFINE_string(file_socket, "", "File socket to use");

namespace {

// Establish connection and perform initial handshake
std::shared_ptr<tc::Client> makeClient() {
  // Establish connection
  auto client = std::make_shared<tc::Client>();
  if (FLAGS_file_socket.size() > 0) {
    if (!client->connect(FLAGS_file_socket, FLAGS_timeout)) {
      throw std::runtime_error(
          std::string("Error establishing connection: ") + client->error());
    }
    VLOG(0) << "Using TorchCraft server at " << FLAGS_file_socket;
  } else {
    if (!client->connect(FLAGS_hostname, FLAGS_port, FLAGS_timeout)) {
      throw std::runtime_error(
          std::string("Error establishing connection: ") + client->error());
    }
    VLOG(0) << "Using TorchCraft server at " << FLAGS_hostname << ":"
            << FLAGS_port;
  }

  // Perform handshake
  tc::Client::Options opts;
  std::vector<std::string> upd;
  if (!client->init(upd, opts)) {
    throw std::runtime_error(
        std::string("Error initializing connection: ") + client->error());
  }
  if (client->state()->replay) {
    throw std::runtime_error("Expected non-replay map");
  }

  return client;
}

} // namespace

int main(int argc, char** argv) {
  cherrypi::init();
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (FLAGS_seed >= 0) {
    common::Rand::setSeed(FLAGS_seed);
  }

  // We need to init the logging after we have parsed the command
  // line flags since it depends on flags set by it
  cherrypi::initLogging(argv[0], FLAGS_logsinkdir, FLAGS_logsinktostderr);

  try {
    Player bot(makeClient());
    setupPlayerFromCli(&bot);

    // In normal playing mode we don't need to save UPC-related data longer than
    // necessary
    bot.state()->board()->upcStorage()->setPersistent(false);

    bot.run();

    if (bot.state()->won()) {
      LOG(WARNING) << "Final result: Victory!!!";
    } else if (bot.state()->currentFrame() == 0) {
      LOG(WARNING) << "Game ended on frame 0";
      LOG(WARNING) << "Final result: Inconclusive???";
    } else {
      LOG(WARNING) << "Oh noes we lost :( -- with "
                   << bot.state()->unitsInfo().myBuildings().size()
                   << " buildings left";
      LOG(WARNING) << "Final result: Defeat!!!";
    }
  } catch (std::exception& e) {
    LOG(DFATAL) << "Exception: " << e.what();
    // FATAL terminates the program, though
    cherrypi::shutdown(FLAGS_logsinktostderr);
    return EXIT_FAILURE;
  }

  cherrypi::shutdown(FLAGS_logsinktostderr);
  return EXIT_SUCCESS;
}
