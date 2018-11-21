/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <tuple>

#include "cpid/metrics.h"
#include "state.h"

namespace microbattles {

constexpr int kMapHeight = 256; // Note: hard-coded - maps should be this size
constexpr int kMapWidth = 256;
constexpr double kMapDiagonal =
    sqrt(kMapWidth * kMapWidth + kMapHeight * kMapHeight);

std::tuple<float, float, float, float> getUnitCountsHealth(
    cherrypi::State* state);
double getMovementRadius(cherrypi::Unit* u);
at::Device defaultDevice();
}
