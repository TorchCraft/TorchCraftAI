/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

// __attribute__((unused)) is a GCC-specific feature
#ifdef __GNUC__
#define ATTR_UNUSED __attribute__((unused))
#else
#define ATTR_UNUSED
#endif

#include "basetypes.h"
#include "registry.h"

#include <torchcraft/state.h>

#include <chrono>
#include <string>

/*** FOR PARAMETER OPTIMIZATION */
#define DFOASG(mean, var) \
  mean // this  is a gaussian with given mean and variance

/**
 * Main namespace for bot-related code.
 */
namespace cherrypi {

namespace tc = ::torchcraft;

using Duration = std::chrono::nanoseconds;
// We want a steady high-resolution clock (ideally with sub-ms precision)
// TODO: Use a different clock on systems where steady_clock's resolution is
// too low (VS2013?)
using hires_clock = std::chrono::steady_clock;

void init(int64_t randomSeed);
void init();
// Needs to be called manually
void initLogging(
    const char* execName,
    std::string logSinkDir,
    bool logSinkToStderr);
// Called by init(); a noop on non-POSIX systems
void installSignalHandlers();
void shutdown(bool logSinkToStderr);
} // namespace cherrypi
