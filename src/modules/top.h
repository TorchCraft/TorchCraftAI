/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "module.h"

namespace cherrypi {

/**
 * The first module run in each frame.
 *
 * Posts a single combined UPC tuple for all active units for other modules
 * to consume.
 */
class TopModule : public Module {
 public:
  TopModule() {}
  virtual ~TopModule() = default;

  virtual void step(State* s);
};

} // namespace cherrypi
