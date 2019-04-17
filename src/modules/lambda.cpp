/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "modules/lambda.h"

namespace cherrypi {

LambdaModule::LambdaModule(StepFunctionState fn, std::string name)
    : Module(), fn_(fn) {
  setName(std::move(name));
}

LambdaModule::LambdaModule(StepFunctionStateModule fn, std::string name)
    : Module(), fn_(fn) {
  setName(std::move(name));
}

void LambdaModule::step(State* state) {
  fn_.match(
      [&](StepFunctionState fn) { fn(state); },
      [&](StepFunctionStateModule fn) { fn(state, this); });
}

} // namespace cherrypi
