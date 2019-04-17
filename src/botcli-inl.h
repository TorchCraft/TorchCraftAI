/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "cherrypi.h"
#include "common/rand.h"
#include "modules.h"
#include "player.h"
#include "utils.h"

#include <gflags/gflags.h>

/**
 * This file pulls in command-line flags that are useful for programs that run
 * the full player-> There's also a setupPlayerFromCli() method that sets up a
 * `Player` instance accordingly.
 */

// Command line flags
DEFINE_string(
    modules,
    cherrypi::kDefaultModules,
    "Comma-separated list of bot modules");
DEFINE_int32(frameskip, 1, "Frame skip for screen updates");
DEFINE_int32(timeout, 120000, "Timeout for TorchCraft connection");
DEFINE_int32(seed, -1, "Random seed. Used default seed if -1");
DEFINE_int32(
    realtime_factor,
    -1,
    "Delay execution to achieve desired realtime factor");
DEFINE_bool(
    warn_if_slow,
    true,
    "Warn if stepping through modules takes too long");
DEFINE_bool(
    blocking,
    false,
    "Run bot step in main thread, possibly blocking game execution");
DEFINE_bool(logsinktostderr, true, "Log sink to stderror.");
DEFINE_string(logsinkdir, "", "Optional directory to write sink log files");
DEFINE_bool(consistency, true, "Run consistency checks during bot execution");
DEFINE_bool(timers, true, "Measure elapsed time in bot modules");
DEFINE_bool(log_failed_commands, false, "Log failed Torchcraft/BWAPI commands");
DEFINE_bool(draw, false, "Enable drawing");
DEFINE_string(
    trace_along_replay_file,
    "",
    "Path to a replay file (.rep) along which we will trace the bot internal "
    "state. Disabled if empty");
DECLARE_string(umm_path);
DEFINE_bool(map_hack, false, "Enable map hack");

namespace cherrypi {

void setupPlayerFromCli(Player* player) {
  // Configure according to flags defined above
  player->setFrameskip(FLAGS_frameskip);
  player->setRealtimeFactor(FLAGS_realtime_factor);
  player->setWarnIfSlow(FLAGS_warn_if_slow);
  player->setNonBlocking(!FLAGS_blocking);
  player->setCheckConsistency(FLAGS_consistency);
  player->setCollectTimers(FLAGS_timers);
  player->setLogFailedCommands(FLAGS_log_failed_commands);
  player->setDraw(FLAGS_draw);
  player->setMapHack(FLAGS_map_hack);

  // Add modules
  player->addModule(Module::make(kAutoTopModule));
  for (auto name : utils::stringSplit(FLAGS_modules, ',')) {
    if (!name.empty()) {
      player->addModule(Module::make(name));
    }
  }
  player->addModule(Module::make(kAutoBottomModule));
  if (!FLAGS_trace_along_replay_file.empty()) {
    player->dumpTraceAlongReplay(FLAGS_trace_along_replay_file);
  }
}

} // namespace cherrypi
