/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#include "test.h"

#include "common/language.h"

using namespace common;
using ms = std::chrono::milliseconds;

CASE("common/TimeoutGuard") {
  {
    auto triggered = false;
    TimeoutGuard guard([&] { triggered = true; }, ms(100));
    std::this_thread::sleep_for(ms(200));
    EXPECT(triggered);
  }
  {
    auto triggered = false;
    TimeoutGuard guard([&] { triggered = true; }, ms(100));
    std::this_thread::sleep_for(ms(20));
    EXPECT(!triggered);
  }
}
