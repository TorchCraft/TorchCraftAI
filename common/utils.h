/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>
#include <functional>

#include "language.h"
#include "mathutils.h"
#include "str.h"

namespace common {
inline void setCurrentThreadName(std::string const& name) {
#ifdef __APPLE__
  pthread_setname_np(name.c_str());
#elif __linux__
  pthread_setname_np(pthread_self(), name.c_str());
#else
  // Unsupported
#endif
}

double memoryUsage();

inline double timestamp(
    std::chrono::system_clock::time_point tp =
        std::chrono::system_clock::now()) {
  auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                    tp.time_since_epoch())
                    .count();
  return double(millis) / 1000;
}

struct Timer {
  using CallbackFunction = std::function<void(std::string, double)>;
  static void GlogFunc(std::string, double);
  using clock = std::chrono::steady_clock;
  clock::time_point start_;
  std::string key_;
  bool sync_;
  CallbackFunction func_;
  Timer(
      std::string key_,
      CallbackFunction f = GlogFunc,
      bool deviceSync = false);
  Timer(std::string key, bool deviceSync);
  ~Timer();
};

} // namespace common
