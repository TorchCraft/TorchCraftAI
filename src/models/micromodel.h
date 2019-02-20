/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "state.h"

namespace cherrypi {

struct MicroModel {
  virtual void forward(State* state) = 0;
  virtual MicroAction decode(Unit* unit) = 0;
};

} // namespace cherrypi