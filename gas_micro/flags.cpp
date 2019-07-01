/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gflags/gflags.h>

DEFINE_uint64(num_threads, 64, "How many threads to use");
DEFINE_string(model, "GasCNN", "Models");

#define VALID_OPPONENTS "stationary|attack_move|closest|weakest|selfplay|squad"
DEFINE_string(
    train_opponent,
    "stationary",
    "opponent used for training: " VALID_OPPONENTS);
DEFINE_string(
    eval_opponent,
    "stationary",
    "opponent used for evaluation: " VALID_OPPONENTS);
DEFINE_bool(
    relative_reward,
    true,
    "Use a reward relative to the scenario or not");
DEFINE_double(sigma, 1e-2, "Mutation step-size/learning rate for CES");
DEFINE_string(scenario, "customOutnumber", "Scenarios (refer to environment.cpp)");
DEFINE_bool(
    list_scenarios,
    false,
    "Just print out the list of available scenarios and exit.");
DEFINE_uint64(checkpoint_freq, 500, "Number of updates between checkpoints");
DEFINE_string(
    map_path_prefix,
    "./",
    "To run this from a different directory, you have to specify where the "
    "maps are");
DEFINE_uint64(batch_size, 32, "batch size");
DEFINE_uint64(max_frames, 1200, "Max number of frames per episode");
// -1 is the max for an unsigned int
DEFINE_uint64(max_episodes, -1, "Max number of episodes to train for");
DEFINE_uint64(updates_per_epoch, 500, "Number of trainer updates per epoch");
DEFINE_uint64(frame_skip, 25, "Frames between forward passes");
DEFINE_bool(enable_gui, false, "Enable GUI for first thread");
DEFINE_bool(illustrate, false, "Draw interesting circles 'n' lines 'n' stuff");
DEFINE_string(results, ".", "Results directory");
DEFINE_uint64(test_freq, 500, "Number of updates between test runs");
DEFINE_uint64(stats_freq, 50, "Number of updates between stats printouts");
DEFINE_uint64(num_test_episodes, 100, "Number of episodes for each test run");
DEFINE_double(
    realtime,
    -1,
    "BWAPI speed, as a multiple of human (fastest) speed. Negative values are "
    "unbounded speed.");
DEFINE_string(resume, "", "Path to a checkpoint to resume");
DEFINE_bool(evaluate, false, "Evaluation mode");
DEFINE_bool(gpu, true, "Use GPU");
DEFINE_bool(sample_command, true, "Sample or argmax on the command");
// Visualization and analytics
DEFINE_string(visdom_server, "http://localhost", "Visdom server address");
DEFINE_int32(visdom_port, 8097, "Visdom server port");
DEFINE_string(
    visdom_env,
    "",
    "Visdom environment (empty string disables visualization)");
DEFINE_uint64(plot_every, 1440, "Plot model output by frames");
DEFINE_string(
    dump_replays,
    "eval",
    "When to dump game replays (train|eval|always|never)");
DEFINE_uint64(
    dump_replays_rate,
    200,
    "Replays sampling rate (default = 200: will dump 0.5% of the games)");

DEFINE_bool(
    debug_update,
    false,
    "Give detailed plot of stats on each update of the model");
DEFINE_string(sampler, "none", "Sampler to use: none|multinomial|max");
DEFINE_string(trainer, "es", "Trainer to use: es|a2c");
DEFINE_uint64(max_lod, 0, "Number of splits in hierarchy of groups");
DEFINE_uint64(command_offset, 60, "Pixel offset for attack and move commands");
DEFINE_int32(override_action, -1, "override all actions with this");
DEFINE_uint64(min_lod, 0, "Initial number of splits");
DEFINE_uint64(lod_growth_length, 0, "Grow to max lod over this many updates");
DEFINE_uint64(lod_lead_in, 0, "Keep lod at min for this many updates");
DEFINE_bool(only_train_max_lod, false, "Only train max lod");
DEFINE_uint64(gas_on_plateau, 0, "Grow on plateau. If > 0, number of updates to wait for growth in test wr");
DEFINE_uint64(
    epsilon_decay_length,
    100000,
    "Number of trainer updates over which to decay epsilon");
DEFINE_double(epsilon_min, 0, "Floor of epsilon decay");
DEFINE_double(epsilon_max, 1.0, "Initial epsilon");
DEFINE_uint64(nsteps, 6, "Number of steps in trainer rollout chunks");
DEFINE_uint64(action_repeat, 1, "Number of times to repeat each action");
DEFINE_double(
    gradient_clipping,
    -1,
    "Gradient clipping factor for GAS trainer. Values <0 lead to no clipping");
DEFINE_double(reward_scale, 10, "Scale factor on reward");
DEFINE_bool(sparse_reward, false, "Only give reward at final frame");
DEFINE_double(dmg_taken_scale, 0.0, "Reward scale factors");
DEFINE_double(dmg_scale, 1.0, "Reward scale factors");
DEFINE_double(death_scale, 0.0, "Reward scale factors");
DEFINE_double(kill_scale, 4.0, "Reward scale factors");
DEFINE_double(win_scale, 8.0, "Reward scale factors");
DEFINE_string(custom_scenario_unit, "zg", "Unit type in a custom scenario");
DEFINE_string(custom_scenario_enemy, "mr", "Enemy unit type in a custom scenario");
DEFINE_int32(custom_scenario_num, 10, "Number of base units that both sides should have in the custom scenario");
DEFINE_int32(custom_scenario_sep, 60, "Separation of groups (0 for some appropriate default)");
DEFINE_int32(custom_scenario_advantage, 1, "Number of addtl units that the advantage side should have");
DEFINE_int32(custom_scenario_split, 1, "Number of addtl units that the split of units should have");
DEFINE_bool(
    gas_reuse_centroids,
    true,
    "Reuse centroids of groups during kmeans.");
DEFINE_bool(group_w_unittype, true, "For GAS, group units based on type in addition to position");
DEFINE_int32(unit_type_dist, 1000, "Distance between units of differing type");
DEFINE_bool(gas_max_targets, true, "Max over lower lods in targets");
DEFINE_uint64(act_grid_sz, 10, "H/W of action grid for global actions");
DEFINE_double(discount, 0.998, "RL discount factor");
DEFINE_double(time_penalty, 0.1, "Reward penalty per MDP timestep");
DEFINE_bool(a2c, false, "Turn on for softmax exploration with GAS");
DEFINE_double(entropy_loss_coef, 0.01, "Entropy loss coef for GAS A2C");
DEFINE_double(match_loss_coef, 0.0, "Coeff on KL matching loss");
DEFINE_bool(custom_scenario_angle, false, "vary the angle between groups");
DEFINE_bool(max_pool, false, "Use max pool opposed to mean pooling");
DEFINE_bool(state_value, true, "Use state value");
