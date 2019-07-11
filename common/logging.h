#pragma once
#include <gflags/gflags.h>

DECLARE_string(vfilter);

namespace common {
// Needs to be called manually
void initLogging(
    const char* execName,
    std::string logSinkDir,
    bool logSinkToStderr);

/// Set a frame number to prefix log messages with
void setLoggingFrame(int frame);

/// Reset the frame number for log messages
void unsetLoggingFrame();

void shutdownLogging(bool logSinkToStderr);
} // namespace common
