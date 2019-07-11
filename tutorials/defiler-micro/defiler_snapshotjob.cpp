/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "common/fsutils.h"
#include "defilersnapshotter.h"
#include "forkserver.h"
#include "replayer.h"

DEFINE_string(
    replays,
    "/checkpoint/starcraft/stardata_original_replays/0/",
    "Where to look for replay files");

DEFINE_string(
    snapshot_output,
    "",
    "Overrides the default location where snapshots are written");

DEFINE_int32(
    snapshot_cooldown,
    24 * 5,
    "How many frames between taking snapshots");

DEFINE_int32(snapshots_max, 40, "Maximum number of snapshots to take per game");

int main(int argc, char* argv[]) {
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  cherrypi::ForkServer::startForkServer();

  cherrypi::init();
  cherrypi::initLogging(argv[0], "", true);

  auto replayDirectory = FLAGS_replays;
  auto replayFilenames = common::fsutils::find(replayDirectory, "*.rep");
  VLOG(0) << "Found " << replayFilenames.size() << " replays at "
          << replayDirectory;

  int replays = 0;

  for (std::string replayFilename : replayFilenames) {
    ++replays;

    VLOG(0) << "Loading replay " << replayFilename;

    try {
      microbattles::DefilerSnapshotter snapshotter;
      snapshotter.cooldownFramesMax = FLAGS_snapshot_cooldown;
      snapshotter.maxSnapshots = FLAGS_snapshots_max;
      if (FLAGS_snapshot_output.length() > 0) {
        snapshotter.setOutputDirectory(FLAGS_snapshot_output);
      }

      cherrypi::TCReplayer replay(replayFilename);
      torchcraft::State* tcstate = replay.tcstate();
      replay.init();

      // Replay through up to 30 minutes of the game
      constexpr int maxFrames = 24 * 60 * 30;
      int frame = 0;
      while (!replay.isComplete()) {
        replay.step();

        // Should we snapshot this game?
        if (frame == 0) {
          // Skip games on maps > 128 in any dimension (these are rare)
          constexpr int maxDimension =
              128 * torchcraft::BW::XYWalktilesPerBuildtile;
          if (tcstate->map_size[0] > maxDimension) {
            VLOG(0) << "Skipping due to large map width";
            break;
          }
          if (tcstate->map_size[1] > maxDimension) {
            VLOG(0) << "Skipping due to large map height";
            break;
          }

          // Only scan games likely to have Defilers
          bool hasZerg = false;
          bool hasTerranOrProtoss = false;
          for (auto& playerUnits : tcstate->units) {
            for (auto& unit : playerUnits.second) {
              hasZerg =
                  hasZerg || unit.type == torchcraft::BW::UnitType::Zerg_Drone;
              hasTerranOrProtoss = hasTerranOrProtoss ||
                  unit.type == torchcraft::BW::UnitType::Terran_SCV ||
                  unit.type == torchcraft::BW::UnitType::Protoss_Probe;
            }
          }
          if (!hasZerg) {
            VLOG(0) << "Skipping due to lack of Zerg player";
            break;
          }
          if (!hasTerranOrProtoss) {
            VLOG(0) << "Skipping due to lack of Terran/Protoss player";
            break;
          }
          VLOG(0) << "Will play this game out.";
        }

        // Say cheese!
        snapshotter.step(replay.tcstate());

        frame = tcstate->frame_from_bwapi;
        if (frame >= maxFrames) {
          VLOG(0) << "Halting game at time limit.";
          break;
        }
      }

      if (frame > 0) {
        VLOG(0) << "Finished replaying game at " << frame / 24 / 60 << "m"
                << frame / 24 % 60 << "s";
      }

    } catch (std::exception const& exception) {
      LOG(WARNING) << "Exception running replay: " << exception.what();
    }

    VLOG_EVERY_N(0, 100) << "Snapshotted " << replays << " replays";
  }
}