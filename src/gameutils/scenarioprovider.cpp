/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "scenarioprovider.h"
#include "baseplayer.h"

namespace cherrypi {

bool ScenarioProvider::isFinished(int currentStep) {
  if (player1_ == nullptr || player2_ == nullptr) {
    return true;
  }
  if (maxFrame_ >= 0 && currentStep > maxFrame_) {
    return true;
  }
  int units1 = player1_->state()->unitsInfo().myUnits().size();
  int units2 = player2_->state()->unitsInfo().myUnits().size();
  return (units1 == 0) || (units2 == 0);
}

} // namespace cherrypi
