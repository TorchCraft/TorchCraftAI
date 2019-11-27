/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "flags.h"

DEFINE_uint64(num_workers, 64, "How many workers to use");
DEFINE_uint64(batch_size, 32, "Model batchsize");

DEFINE_double(sigma, 1e-1, "Variance of the policy");
DEFINE_uint64(returns_length, 10, "Size of the returns on which we update");
DEFINE_double(discount, 0.999, "Discount factor of the returns");
DEFINE_double(
    ratio_clamp,
    10,
    "Maximal probability ratio in the off-policy correction");

DEFINE_double(
    policy_ratio,
    1.,
    "Normalization factor applied to the policy error");
DEFINE_string(scenario, "marine", "Scenario to use. Can be marine or wraith");
DEFINE_int32(scenario_size, 5, "Number of units to spawn on each side");
DEFINE_string(checkpoint, "", "Where to save");
DEFINE_bool(enable_gui, false, "Enable GUI for first thread");
DEFINE_double(
    realtime,
    -1,
    "BWAPI speed, as a multiple of human (fastest) speed. Negative values are "
    "unbounded speed.");

DEFINE_uint64(seed, 42, "Random seed");

DEFINE_uint64(epoch_size, 500, "Number of updates in an epoch");

DEFINE_string(visdom_server, "localhost", "Visdom server address");
DEFINE_int32(visdom_port, 8097, "Visdom server port");
DEFINE_string(
    visdom_env,
    "",
    "Visdom environment (empty string disables visualization)");

DEFINE_uint64(frame_skip, 6, "Frames between forward passes");
DEFINE_int64(map_dim, 100, "Size of the area of interest");
DEFINE_uint64(
    conv_embed_size,
    16,
    "size of the intermediate layers of the convolutions");

DEFINE_uint64(
    linear_embed_size,
    32,
    "size of the intermediate layers of the linear layers");

DEFINE_uint64(
    correlated_steps,
    5,
    "Number of consecutive steps where we correlate the actions");

DEFINE_string(
    model_type,
    "argmax_dm",
    "Model to use. Avail: argmax_dm, argmax_pem, lp_dm, lp_pem, quad_dm, "
    "quad_pem");

DEFINE_double(policy_momentum, 0.5, "policy momentum");

DEFINE_int32(num_episodes, 1000000, "number of episodes to play");
DEFINE_bool(eval, false, "whether to run in eval mode");
DEFINE_string(
    eval_policy,
    "",
    "can be random, closest or weakest_closest for a heuristic, otherwise must "
    "point to the bin of a model");

DEFINE_bool(
    use_pairwise_feats,
    false,
    "If true, we also featurize some pairwise features");
DEFINE_bool(
    use_embeddings,
    false,
    "If true, we also first embed the tasks and agents");
DEFINE_bool(
    clip_grad,
    false,
    "If true, the gradient norm is going to be clipped to 5");

DEFINE_bool(
    dump_replay,
    false,
    "If true, we dump a replay for each game played by the first thread during "
    "eval, or every 200 episodes during training");


DEFINE_bool(
    use_ga,
    false,
    "if true, we the quadratic optimization is done using a genetic algorithm");

DEFINE_int32(
    difficulty,
    0,
    "number of enemies to add on top of the vanilla scenario");
DEFINE_bool(
    normalize_dist,
    false,
    "if true, the distance feature is divided to be in a more acceptable "
    "range");

DEFINE_int32(
    warmup,
    -1,
    "If positive, we apply a learning rate schedule in the spirit of the "
    "transformer paper");

DEFINE_bool(cpu_only, false, "If true, all the computations are done on cpu");

DEFINE_bool(switch_side, false, "If true, the starting side of the players is random");


DEFINE_string(
              map_path_prefix,
              "./",
              "To run this from a different directory, you have to specify where the "
              "maps are");
