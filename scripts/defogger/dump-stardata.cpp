/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * Reads the .rep from -input and dumps it to -output.
 */

#include "state.h"
#include "replayer.h"

#include <torchcraft/replayer.h>

#include <gflags/gflags.h>
#include <glog/logging.h>

using namespace cherrypi;

DEFINE_string(input, ".", "Use this starcraft replay file");
DEFINE_string(output, ".", "Dump it out here");

int main(int argc, char** argv) {
  cherrypi::init();
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  cherrypi::initLogging(argv[0], "", true);

  torchcraft::replayer::Replayer tcrep;

  ReplayerConfiguration replayerConfiguration;
  replayerConfiguration.replayPath = FLAGS_input;
  replayerConfiguration.combineFrames = 3;
  TCReplayer replay(replayerConfiguration);
  replay.init();
  
  tcrep.setMapFromState(replay.tcstate());

  while (!replay.isComplete()) {
    replay.tcstate()->frame->creep_map.clear();
    tcrep.push(replay.tcstate()->frame);
    replay.step();
  }
  tcrep.push(replay.tcstate()->frame);
  tcrep.setKeyFrame(-1);

  tcrep.save(FLAGS_output, true);

  return 0;
}
