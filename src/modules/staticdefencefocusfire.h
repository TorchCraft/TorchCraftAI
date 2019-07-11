/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "module.h"

namespace cherrypi {

struct Unit;

/**
 * This module issues direct attack commands to static defence (sunken
 * colonies, spore colonies, cannons and turrets) in order to focus fire and
 * kill targets more efficiently.
 */
class StaticDefenceFocusFireModule : public Module {
 public:
  virtual void step(State* s) override;
};

} // namespace cherrypi
