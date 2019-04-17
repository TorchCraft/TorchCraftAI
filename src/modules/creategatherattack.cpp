/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "modules/creategatherattack.h"

#include "player.h"
#include "state.h"
#include "utils.h"

#include <glog/logging.h>

namespace cherrypi {

REGISTER_SUBCLASS_0(Module, CreateGatherAttackModule);

void CreateGatherAttackModule::step(State* state) {
  auto board = state->board();

  // Is this the top module? Otherwise, consume the top module's UPC.
  // Check for UPC from top module
  int topUpcId = -1;
  if (player_->getTopModule().get() != this) {
    for (auto& upcs : board->upcsFrom(player_->getTopModule())) {
      topUpcId = upcs.first;
      break;
    }
    if (topUpcId < 0) {
      VLOG(0) << "Could not find UPC tuple from top module";
      return;
    }
  }

  if (!create_ || !gather_ || !attack_) {
    if (!create_) {
      create_ = std::make_shared<UPCTuple>();
    }
    create_->command.clear();
    create_->command[Command::Create] = 1;
    if (!gather_) {
      gather_ = std::make_shared<UPCTuple>();
    }
    gather_->command.clear();
    gather_->command[Command::Gather] = 1;
    if (!attack_) {
      attack_ = std::make_shared<UPCTuple>();
    }
    attack_->command.clear();
    attack_->command[Command::Delete] = 0.5;
    attack_->command[Command::Move] = 0.5;

    // Undefined (empty) position equals to uniform
    create_->position = UPCTuple::Empty();
    gather_->position = UPCTuple::Empty();
    attack_->position = UPCTuple::Empty();
  }

  // Gather UPC contains workers only. To avoid spamming of UPC filters,
  // we'll just include workers that aren't included in a task right now.
  gather_->unit.clear();
  auto& workers = state->unitsInfo().myWorkers();
  for (Unit* worker : workers) {
    if (board->taskWithUnit(worker) == nullptr) {
      gather_->unit[worker] = 1;
    }
  }

  // Gather UPC contains workers, the other UPCs are left with an empty unit
  // map signalling that we don't specify any unit.
  create_->unit.clear();
  attack_->unit.clear();

  // Repost UPC instances that aren't on the blackboard any more
  std::vector<std::shared_ptr<UPCTuple>> myUpcs;
  for (auto& it : board->upcsFrom(this)) {
    myUpcs.emplace_back(it.second);
  }

  bool consumed = false;
  auto postIfNotPresent = [&](std::shared_ptr<UPCTuple>& upc) {
    if (std::find(myUpcs.begin(), myUpcs.end(), upc) == myUpcs.end()) {
      // Consume top-level UPC (if any)
      if (!consumed && topUpcId >= 0) {
        board->consumeUPC(topUpcId, this);
        consumed = true;
      }

      // The UPC that ends up on the blackboard may not be the same one that we
      // posted (due to UPC filters), so we'll reassign the shared pointer after
      // posting. Since we don't need the original any more we can simply use
      // move()
      // for posting it.
      auto id = board->postUPC(std::move(upc), topUpcId, this);
      if (id > kInvalidUpcId) {
        upc = board->upcWithId(id);
      } else {
        upc = nullptr;
      }
    }
  };

  postIfNotPresent(create_);
  postIfNotPresent(gather_);
  postIfNotPresent(attack_);
}

} // namespace cherrypi
