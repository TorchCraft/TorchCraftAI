/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "common/assert.h"
#include "test.h"
#include <gflags/gflags.h>

CASE("assertions") {
  bool old = FLAGS_continue_on_assert;
  FLAGS_continue_on_assert = true;
  EXPECT_NO_THROW(ASSERT(true));
  EXPECT_NO_THROW(ASSERT(true, "message"));
  EXPECT_THROWS_AS(ASSERT(false), common::AssertionFailure);
  EXPECT_THROWS_AS(ASSERT(false, "message"), common::AssertionFailure);
  FLAGS_continue_on_assert = old;
}
