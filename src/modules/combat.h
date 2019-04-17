/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "module.h"
#include "upc.h"

namespace cherrypi {

/**
 * A combat module that controls squads of units.
 *
 * Used only for unit tests.
 */
class CombatModule : public Module {
 public:
  virtual void step(State* s) override;

 protected:
  bool
  formNewSquad(State* s, std::shared_ptr<UPCTuple> sourceUpc, int sourceUpcId);
  void updateTask(State* s, std::shared_ptr<Task> task);
};

} // namespace cherrypi
