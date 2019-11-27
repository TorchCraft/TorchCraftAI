/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#include <gflags/gflags.h>

DECLARE_uint64(num_workers);
DECLARE_uint64(batch_size);

DECLARE_double(sigma);
DECLARE_uint64(returns_length);
DECLARE_double(discount);
DECLARE_double(ratio_clamp);
DECLARE_double(policy_ratio);

DECLARE_string(scenario);
DECLARE_int32(scenario_size);
DECLARE_string(checkpoint);
DECLARE_bool(enable_gui);
DECLARE_double(realtime);

DECLARE_uint64(seed);

DECLARE_uint64(epoch_size);

DECLARE_string(visdom_server);
DECLARE_int32(visdom_port);
DECLARE_string(visdom_env);

DECLARE_uint64(frame_skip);

DECLARE_int64(map_dim);
DECLARE_uint64(conv_embed_size);

DECLARE_uint64(linear_embed_size);

DECLARE_uint64(correlated_steps);

DECLARE_string(model_type);

DECLARE_double(policy_momentum);

DECLARE_int32(num_episodes);
DECLARE_bool(eval);
DECLARE_string(eval_policy);

DECLARE_bool(use_pairwise_feats);
DECLARE_bool(use_embeddings);

DECLARE_bool(clip_grad);

DECLARE_bool(dump_replay);

DECLARE_bool(use_ga);

DECLARE_int32(difficulty);

DECLARE_bool(normalize_dist);

DECLARE_int32(warmup);

DECLARE_bool(cpu_only);

DECLARE_bool(switch_side);

DECLARE_string(map_path_prefix);
