/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "state.h"

#include "modules/top.h"

namespace cherrypi {

REGISTER_SUBCLASS_0(Module, TopModule);

void TopModule::step(State* state) {
  auto board = state->board();

  // Check if any UPC instance from this module is still on blackboard
  if (board->upcsFrom(this).empty()) {
    auto upc = std::make_shared<UPCTuple>();
    upc->command = UPCTuple::uniformCommand();

    auto& myUnits = state->unitsInfo().myUnits();
    int n = myUnits.size();
    for (Unit* unit : myUnits) {
      upc->unit[unit] = 1.0f / n;
    }
    board->postUPC(std::move(upc), kRootUpcId, this);
  }
}

} // namespace cherrypi
