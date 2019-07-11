/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "modules/squadcombat.h"
#include "test.h"

using namespace cherrypi;

namespace {

SCENARIO("squadcombat/insertbefore") {
  BehaviorList behaviors{std::make_shared<BehaviorEngage>(),
                         std::make_shared<BehaviorLeave>()};

  squadcombat::insertBefore<BehaviorLeave>(
      behaviors, std::make_shared<BehaviorUnstick>());

  EXPECT(std::dynamic_pointer_cast<BehaviorEngage>(behaviors[0]) != nullptr);
  EXPECT(std::dynamic_pointer_cast<BehaviorUnstick>(behaviors[1]) != nullptr);
  EXPECT(std::dynamic_pointer_cast<BehaviorLeave>(behaviors[2]) != nullptr);

  EXPECT_THROWS(squadcombat::insertBefore<BehaviorML>(
      behaviors, std::make_shared<BehaviorUnstick>()));
}

SCENARIO("squadcombat/deleteall") {
  BehaviorList behaviors{std::make_shared<BehaviorUnstick>(),
                         std::make_shared<BehaviorEngage>(),
                         std::make_shared<BehaviorLeave>(),
                         std::make_shared<BehaviorLeave>()};

  squadcombat::removeAll<BehaviorLeave>(behaviors);

  EXPECT(std::dynamic_pointer_cast<BehaviorUnstick>(behaviors[0]) != nullptr);
  EXPECT(std::dynamic_pointer_cast<BehaviorEngage>(behaviors[1]) != nullptr);
  EXPECT(behaviors.size() == 2);
}

} // namespace