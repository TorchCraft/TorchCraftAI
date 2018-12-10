/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <functional>
#include <list>

#include "cherrypi.h"
#include "module.h"

namespace cherrypi {

struct UPCTuple;

/**
 * Generates separate, unspecific UPCTuples for Create, Gather and Delete/Move.
 *
 * This module should be used as the first or second module (after TopModule) in
 * a Player.
 */
class CreateGatherAttackModule : public Module {
 public:
  CreateGatherAttackModule() : Module() {}

  virtual void step(State* s);

 private:
  std::shared_ptr<UPCTuple> create_;
  std::shared_ptr<UPCTuple> gather_;
  std::shared_ptr<UPCTuple> attack_;
};

} // namespace cherrypi
