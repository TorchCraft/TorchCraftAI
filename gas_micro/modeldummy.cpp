/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "modeldummy.h"
#include "common.h"

namespace microbattles {

std::vector<PFMicroActionModel::PFMicroAction> DummyModel::decodeOutput(
    cherrypi::State* state,
    ag::Variant /* input */,
    ag::Variant output) {
  std::vector<PFMicroActionModel::PFMicroAction> outputActions;
  return outputActions;
}

} // namespace microbattles
