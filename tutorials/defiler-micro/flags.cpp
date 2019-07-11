/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gflags/gflags.h>

DEFINE_uint64(num_threads, 20, "How many threads to use");
DEFINE_string(
    model,
    "PF",
    "Models:  "
    "PF|SimpleCNN|Dummy|DefileResNet|DefilerConv|DefilerBaseline|"
    "DefilerEncoderDecoder");

#define VALID_OPPONENTS "attack_move|closest|weakest|selfplay"
DEFINE_string(
    train_opponent,
    "closest",
    "opponent used for training: " VALID_OPPONENTS);
DEFINE_string(
    eval_opponent,
    "closest",
    "opponent used for evaluation: " VALID_OPPONENTS);
DEFINE_double(es_sigma, 1e-2, "Mutation step-size/learning rate for CES");
DEFINE_string(
    map_path_prefix,
    "./",
    "To run this from a different directory, you have to specify where the "
    "maps are");
DEFINE_uint64(batch_size, 64, "batch size");
DEFINE_uint64(max_frames, 24 * 60, "Max number of frames per episode");
// -1 is the max for an unsigned int
DEFINE_uint64(max_episodes, -1, "Max number of episodes to train for");
DEFINE_uint64(updates_per_epoch, 500, "Number of trainer updates per epoch");
DEFINE_uint64(frame_skip, 7, "Frames between forward passes");
DEFINE_bool(gui, false, "Enable GUI for first thread");
DEFINE_bool(illustrate, false, "Draw interesting circles 'n' lines 'n' stuff");
DEFINE_string(results, ".", "Results directory");
DEFINE_uint64(test_freq, 50, "Number of updates between test runs");
DEFINE_uint64(num_test_episodes, 100, "Number of episodes for each test run");
DEFINE_double(
    realtime,
    -1,
    "BWAPI speed, as a multiple of human (fastest) speed. Negative values are "
    "unbounded speed.");
DEFINE_string(resume, "", "Path to a checkpoint to resume");
DEFINE_bool(evaluate, false, "Evaluation mode");
DEFINE_bool(
    print_rewards,
    false,
    "Print reward for each scenario for baseline");
DEFINE_bool(train_on_baseline_rewards, false, "train on the delta of reward");
DEFINE_bool(gpu, true, "Use GPU");
// Visualization and analytics
DEFINE_string(visdom_server, "http://localhost", "Visdom server address");
DEFINE_int32(visdom_port, 8097, "Visdom server port");
DEFINE_string(
    visdom_env,
    "",
    "Visdom environment (empty string disables visualization)");
DEFINE_string(
    dump_replays,
    "eval",
    "When to dump game replays (train|eval|always|never)");
DEFINE_uint64(
    dump_replays_rate,
    200,
    "Replays sampling rate (default = 200: will dump 0.5% of the games)");
DEFINE_int32(returns_length, 4, "A2c only, the returnsLength");
DEFINE_double(plague_threshold, 0.1, "Defiler only");
DEFINE_double(dark_swarm_threshold, 0.1, "Defiler only");
DEFINE_bool(
    debug_update,
    false,
    "Give detailed plot of stats on each update of the model");
DEFINE_bool(defiler_rule, false, "Will use rule for defiler actions");
DEFINE_string(sampler, "none", "Sampler to use: none|multinomial|max");
DEFINE_string(trainer, "es", "Trainer to use: es|a2c");
