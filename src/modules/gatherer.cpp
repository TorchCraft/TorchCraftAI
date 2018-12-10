/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <glog/logging.h>

#include "state.h"
#include "utils.h"

#include "modules/gatherer.h"
#include "modules/gatherer/gathererc.h"

#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cherrypi {

REGISTER_SUBCLASS_0(Module, GathererModule);

void GathererModule::step(State* state) {
  auto board = state->board();

  auto controller =
      SharedController::globalInstance<GathererController>(state, this);

  // Consume UPCs and add units to the controller
  for (auto& v : state->board()->upcsWithSharpCommand(Command::Gather)) {
    UpcId upcId = v.first;
    auto& upc = v.second;
    std::unordered_set<Unit*> units;

    for (auto uit : upc->unit) {
      // XXX We check for owner again since it could happen that the unit was
      // assigned to another task in the same frame...
      auto owner = board->taskDataWithUnit(uit.first).owner;
      if (uit.second > 0 && owner == nullptr) {
        units.insert(uit.first);
      }
    }

    board->consumeUPC(upcId, this);
    if (!units.empty()) {
      auto task = std::make_shared<SharedControllerTask>(
          upcId, std::move(units), state, controller);
      board->postTask(task, this, true);
    }
  }

  controller->step(state);
}

} // namespace cherrypi
