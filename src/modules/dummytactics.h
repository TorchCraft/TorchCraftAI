/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "module.h"
#include "state.h"
#include "task.h"
#include "tilesinfo.h"
#include "utils.h"

#include <random>

DECLARE_uint64(tactics_fight_or_flee_interval);

namespace cherrypi {

/**
 * A simple Tactics module that issues a globally-distributed Delete UPC
 */
class DummyTacticsModule : public Module {
  std::shared_ptr<UPCTuple> upc = std::make_shared<UPCTuple>();

 public:
  virtual void step(State* state) override {
    if (state->unitsInfo().myUnits().empty() ||
        state->unitsInfo().enemyUnits().empty()) {
      return;
    }

    upc->command[Command::Delete] = 1.0;
    upc->unit.clear();
    for (auto& unit : state->unitsInfo().myUnits()) {
      if (!unit->type->isWorker && !unit->type->isBuilding) {
        upc->unit[unit] = 1.0;
      }
    }

    upc->position = UPCTuple::UnitMap();
    for (auto& unit : state->unitsInfo().enemyUnits()) {
      upc->position.get_unchecked<UPCTuple::UnitMap>()[unit] = 1.0;
    }

    state->board()->postUPC(std::move(upc), kRootUpcId, this);
  }
};

} // namespace cherrypi
