/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * This is pretty much equivalent to the main executable, but instead of
 * connecting to a server we'll spin it up ourselves and play against another
 * bot.
 */

#include "gameutils/botscenario.h"
#include "upcstorage.h"

#include <glog/logging.h>

#include "botcli-inl.h"
#include "gameutils/openbwprocess.h"

#include <regex>

using namespace cherrypi;

DEFINE_string(race, "Zerg", "Play as this race");
DEFINE_string(opponent, "", "Play against this opponent");
DEFINE_string(map, "", "Play on this map");
DEFINE_string(
    replay_path,
    "bwapi-data/replays/%BOTNAME%_%BOTRACE%.rep",
    "Where to save resulting replays");
DEFINE_bool(forkserver, false, "Use a fork server");
DEFINE_bool(gui, false, "Enable OpenBW GUI");

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (FLAGS_forkserver) {
    OpenBwProcess::startForkServer();
  }
  cherrypi::init();

  if (FLAGS_seed >= 0) {
    common::Rand::setSeed(FLAGS_seed);
  }

  // We need to init the logging after we have parsed the command
  // line flags since it depends on flags set by it
  cherrypi::initLogging(argv[0], FLAGS_logsinkdir, FLAGS_logsinktostderr);

  FLAGS_replay_path = std::regex_replace(
      FLAGS_replay_path, std::regex("\\$PID"), std::to_string(getpid()));
  try {
    auto opponent = std::make_unique<BotScenario>(
        FLAGS_map,
        tc::BW::Race::_from_string(FLAGS_race.c_str()),
        FLAGS_opponent,
        GameType::Melee,
        FLAGS_replay_path,
        FLAGS_gui);
    Player bot(opponent->makeClient());
    if (!FLAGS_replay_path.empty() && FLAGS_trace_along_replay_file.empty()) {
      FLAGS_trace_along_replay_file = FLAGS_replay_path;
    }
    setupPlayerFromCli(&bot);

    // In normal playing mode we don't need to save UPC-related data longer than
    // necessary
    bot.state()->board()->upcStorage()->setPersistent(false);

    bot.run();

    if (bot.state()->won()) {
      LOG(WARNING) << "Victory!!";
    } else {
      LOG(WARNING) << "Oh noes we lost :( -- with "
                   << bot.state()->unitsInfo().myBuildings().size()
                   << " buildings left";
    }

    const int kills = std::count_if(
        bot.state()->unitsInfo().allUnitsEver().begin(),
        bot.state()->unitsInfo().allUnitsEver().end(),
        [](Unit* u) { return u->dead && u->isEnemy; });
    LOG(WARNING) << "We killed " << kills << " units";
  } catch (std::exception& e) {
    LOG(DFATAL) << "Exception: " << e.what();
    // FATAL terminates the program, though
    cherrypi::shutdown(FLAGS_logsinktostderr);
    return EXIT_FAILURE;
  }

  cherrypi::shutdown(FLAGS_logsinktostderr);
  return EXIT_SUCCESS;
}
