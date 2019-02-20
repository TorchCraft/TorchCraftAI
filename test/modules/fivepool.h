/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "buildtype.h"
#include "module.h"

namespace cherrypi {

/**
 * This module posts create UPCs for the relevant units of the "5 pool" opening
 * for Zerg. See http://wiki.teamliquid.net/starcraft/5_Pool_(vs._Protoss)
 *
 * Used only for unit tests.
 */
class FivePoolModule : public Module {
 public:
  FivePoolModule();
  virtual ~FivePoolModule() = default;

  virtual void step(State* state) override;

 protected:
  std::vector<const BuildType*> builds_;
};

} // namespace cherrypi
