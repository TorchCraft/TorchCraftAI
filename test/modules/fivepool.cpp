/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "player.h"
#include "state.h"
#include "utils.h"

#include "modules/fivepool.h"

namespace cherrypi {

REGISTER_SUBCLASS_0(Module, FivePoolModule);

FivePoolModule::FivePoolModule() : Module() {
  builds_.emplace_back(buildtypes::Zerg_Drone);
  builds_.emplace_back(buildtypes::Zerg_Spawning_Pool);
  builds_.emplace_back(buildtypes::Zerg_Drone);
  builds_.emplace_back(buildtypes::Zerg_Drone);
  builds_.emplace_back(buildtypes::Zerg_Zergling);
  builds_.emplace_back(buildtypes::Zerg_Zergling);
  builds_.emplace_back(buildtypes::Zerg_Zergling);
  builds_.emplace_back(buildtypes::Zerg_Overlord);
  for (int k = 0; k < 100; ++k) {
    builds_.emplace_back(buildtypes::Zerg_Zergling);
  }
}

void FivePoolModule::step(State* state) {
  auto board = state->board();

  // Find 'Create' UPC with empty state
  int srcUPCId = -1;
  std::shared_ptr<UPCTuple> srcUPC = nullptr;
  for (auto& upcs : board->upcsWithSharpCommand(Command::Create)) {
    if (upcs.second->state.is<UPCTuple::Empty>()) {
      srcUPCId = upcs.first;
      srcUPC = upcs.second;
      break;
    }
  }
  if (srcUPCId < 0) {
    VLOG(0) << "No suitable source UPC";
    return;
  }

  if (builds_.empty()) {
    VLOG(3) << "Build is done";
    return;
  }

  auto p = builds_.front();

  VLOG(1) << "Post new UPC for " << p->name;
  auto upc = std::make_shared<UPCTuple>();
  auto& builders = state->unitsInfo().myCompletedUnitsOfType(p->builder);
  // Avoid posting UPCs with probability 1 for a single builder since
  // UPCToCommand will directly issue a command.
  auto prob = std::min(1.0f / builders.size(), 0.5f);
  for (Unit* u : builders) {
    upc->unit[u] = prob;
  }
  upc->scale = 1;
  upc->command[Command::Create] = 1;
  upc->state = UPCTuple::BuildTypeMap{{p, 1}};

  board->consumeUPC(srcUPCId, this);
  board->postUPC(std::move(upc), srcUPCId, this);
  builds_.erase(builds_.begin());
}

} // namespace cherrypi
