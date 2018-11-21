/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gflags/gflags.h>

DEFINE_uint64(num_threads, 20, "How many threads to use");
DEFINE_string(model, "PF", "Models: PF");
DEFINE_string(opponent, "closest", "opponent: attack_move|closest|weakest");
DEFINE_bool(
    relative_reward,
    true,
    "Use a reward relative to the scenario or not");
DEFINE_double(sigma, 1e-2, "Mutation step-size/learning rate for CES");
DEFINE_string(scenario, "5vu_10zl", "Scenarios (refer to environment.cpp)");
DEFINE_bool(
    list_scenarios,
    false,
    "Just print out the list of available scenarios and exit.");
DEFINE_string(
    map_path_prefix,
    "./",
    "To run this from a different directory, you have to specify where the "
    "maps are");
DEFINE_uint64(batch_size, 64, "batch size");
DEFINE_uint64(max_frames, 24 * 60, "Max number of frames per episode");
// -1 is the max for an unsigned int
DEFINE_uint64(max_episodes, -1, "Max number of episodes to train for");
DEFINE_uint64(frame_skip, 7, "Frames between forward passes");
DEFINE_bool(enable_gui, false, "Enable GUI for first thread");
DEFINE_bool(illustrate, false, "Draw interesting circles 'n' lines 'n' stuff");
DEFINE_string(results, ".", "Results directory");
DEFINE_uint64(checkpoint_freq, 50, "Number of updates between checkpoints");
DEFINE_uint64(test_freq, 50, "Number of updates between test runs");
DEFINE_uint64(num_test_episodes, 100, "Number of episodes for each test run");
DEFINE_double(
    realtime,
    -1,
    "BWAPI speed, as a multiple of human (fastest) speed. Negative values are "
    "unbounded speed.");
DEFINE_bool(resume, false, "Resume training from previous checkpoint");
DEFINE_bool(evaluate, false, "Evaluation mode");
DEFINE_bool(gpu, true, "Use GPU");
DEFINE_bool(sample_command, true, "Sample or argmax on the command");
