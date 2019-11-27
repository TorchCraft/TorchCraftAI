/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "src/module.h"
#include "src/utils/filter.h"

namespace cherrypi {
class MockTacticsModule : public Module {
 public:
  MockTacticsModule() : Module() {}
  void step(State* state) override {
    auto board = state->board();
    auto loc = utils::filterUnits(
        state->unitsInfo().enemyUnits(),
        [](Unit const* u) { return !u->dead && !u->type->isBuilding; });
    auto units = utils::filterUnits(
        state->unitsInfo().myUnits(),
        [](Unit const* u) { return u->active() && !u->type->isBuilding; });
    if (units.size() == 0) {
      return;
    }

    postUpc(state, 1, units, loc);
    board->post("target_posted", true);
  }

  void postUpc(
      State* state,
      int srcUpcId,
      std::vector<Unit*> const& units,
      std::vector<Unit*> const& targets) {
    auto upc = std::make_shared<UPCTuple>();
    for (Unit* u : units) {
      upc->unit[u] = 1.0f / units.size();
    }
    UPCTuple::UnitMap map;
    for (Unit* u : targets) {
      map[u] = 1.0f / targets.size();
    }
    upc->position = std::move(map);
    upc->command[Command::Delete] = 0.5;
    upc->command[Command::Move] = 0.5;

    state->board()->postUPC(std::move(upc), srcUpcId, this);
  }
};

} // namespace cherrypi
