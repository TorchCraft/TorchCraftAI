/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "module.h"

#include <mapbox/variant.hpp>

namespace cherrypi {

/**
 * Lets you construct a lightweight module by providing your own step()
 * as an std::function.
 */
class LambdaModule : public Module {
 public:
  using StepFunctionState = std::function<void(State*)>;
  using StepFunctionStateModule = std::function<void(State*, Module*)>;

  LambdaModule(StepFunctionState fn, std::string name = std::string());
  LambdaModule(StepFunctionStateModule fn, std::string name = std::string());
  virtual ~LambdaModule() = default;

  virtual void step(State* state) override;

 protected:
  mapbox::util::variant<StepFunctionState, StepFunctionStateModule> fn_;
};

} // namespace cherrypi
