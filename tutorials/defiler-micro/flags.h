/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <gflags/gflags.h>

DECLARE_uint64(num_threads);
DECLARE_string(model);
DECLARE_string(train_opponent);
DECLARE_string(eval_opponent);
DECLARE_double(es_sigma);
DECLARE_uint64(batch_size);
DECLARE_string(map_path_prefix);
DECLARE_uint64(max_frames);
DECLARE_uint64(max_episodes);
DECLARE_uint64(updates_per_epoch);
DECLARE_uint64(frame_skip);
DECLARE_bool(gui);
DECLARE_bool(illustrate);
DECLARE_string(results);
DECLARE_uint64(test_freq);
DECLARE_uint64(num_test_episodes);
DECLARE_double(realtime);
DECLARE_string(resume);
DECLARE_bool(evaluate);
DECLARE_bool(print_rewards);
DECLARE_bool(train_on_baseline_rewards);
DECLARE_bool(gpu);
DECLARE_string(visdom_server);
DECLARE_int32(visdom_port);
DECLARE_string(visdom_env);
DECLARE_string(dump_replays);
DECLARE_uint64(dump_replays_rate);
DECLARE_int32(returns_length);
DECLARE_double(plague_threshold);
DECLARE_double(dark_swarm_threshold);
DECLARE_bool(debug_update);
DECLARE_bool(defiler_rule);
DECLARE_string(sampler);
DECLARE_string(trainer);
