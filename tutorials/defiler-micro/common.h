/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <tuple>

#include "cpid/metrics.h"
#include "player.h"
#include "state.h"

namespace microbattles {

constexpr int kMapHeight =
    512; // Note: hard-coded - maps should be no larger than this in walktiles
constexpr int kMapWidth = 512;

std::tuple<float, float, float, float> getUnitCountsHealth(
    cherrypi::State* state);
at::Device defaultDevice();
} // namespace microbattles
