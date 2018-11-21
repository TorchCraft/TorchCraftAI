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
DECLARE_string(opponent);
DECLARE_double(sigma);
DECLARE_double(lr);
DECLARE_uint64(batch_size);
DECLARE_string(scenario);
DECLARE_bool(list_scenarios);
DECLARE_string(map_path_prefix);
DECLARE_uint64(max_frames);
DECLARE_uint64(max_episodes);
DECLARE_uint64(frame_skip);
DECLARE_bool(enable_gui);
DECLARE_bool(illustrate);
DECLARE_string(results);
DECLARE_uint64(checkpoint_freq);
DECLARE_uint64(test_freq);
DECLARE_uint64(num_test_episodes);
DECLARE_double(realtime);
DECLARE_bool(resume);
DECLARE_bool(evaluate);
DECLARE_bool(gpu);
DECLARE_bool(sample_command);
