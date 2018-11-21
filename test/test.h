/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <lest.hpp>

// Enable auto-registration
#define CASE(name) lest_CASE(specification(), name)
#undef SCENARIO
#define SCENARIO(name) \
  lest_CASE(specification(), lest::text("Scenario: ") + name)

extern lest::tests& specification();
