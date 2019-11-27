/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "scenarioprovider.h"
#include "baseplayer.h"

namespace {
// estimation of the number of played frames needed to propagate detection. This
// is 36 frames, assuming a frame skip of 3.
int constexpr kDetectionDelay = 12;
} // namespace

namespace cherrypi {

bool ScenarioProvider::isFinished(int currentStep, bool checkAttack) {
  if (player1_ == nullptr || player2_ == nullptr) {
    return true;
  }
  if (maxFrame_ >= 0 && currentStep > maxFrame_) {
    return true;
  }
  int units1 = player1_->state()->unitsInfo().myUnits().size();
  int units2 = player2_->state()->unitsInfo().myUnits().size();

  if (units1 == 0 || units2 == 0) {
    // trivial termination conditions
    return true;
  }

  // We consider the scenario to be finished when no pair of units can attack
  // each other. We need to remind the last step on which we could attack,
  // because detection takes a while to be propagated, hence we need to wait to
  // see if attacks are going to be possible again. If the last attack step is
  // uninitialized, or higher than the current step, we assume that we are at a
  // beginning of an episode and start counting from now.
  if (lastPossibleAttack_ < 0 || lastPossibleAttack_ > currentStep) {
    lastPossibleAttack_ = currentStep;
  }
  auto canAttack = [](const auto& allyUnits, const auto& enemyUnits) {
    for (const auto& u : allyUnits) {
      for (const auto& v : enemyUnits) {
        if (u->canAttack(v)) {
          return true;
        }
      }
    }
    return false;
  };

  bool canAttack1 = canAttack(
      player1_->state()->unitsInfo().myUnits(),
      player1_->state()->unitsInfo().enemyUnits());
  bool canAttack2 = canAttack(
      player2_->state()->unitsInfo().myUnits(),
      player2_->state()->unitsInfo().enemyUnits());

  bool possibleAttack = canAttack1 || canAttack2;
  // we might not be able to attack yet, for example in case the detection
  // status has not been updated yet. That's why we need to track the last time
  // we could attack to avoid premature ending.
  if (possibleAttack) {
    lastPossibleAttack_ = currentStep;
  }

  if (checkAttack && !possibleAttack) {
    return (currentStep - lastPossibleAttack_ > kDetectionDelay);
  }
  return false;
}

} // namespace cherrypi
