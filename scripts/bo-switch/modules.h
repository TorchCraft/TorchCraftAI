/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "models/bos/runner.h"

#include "module.h"

namespace cherrypi {

class BosModule : public Module {
 public:
  using Module::Module;
  virtual ~BosModule() = default;

  virtual void onGameStart(State* s) override;
  virtual void step(State* s) override;

 protected:
  virtual ag::Variant forward(State* s);
  virtual std::string selectBuild(State* s);

  std::unique_ptr<bos::ModelRunner> runner_ = nullptr;
  ag::Variant output_;
  FrameNum nextSelectionFrame_ = 0;
  FrameNum nextForwardFrame_ = 0;
  bool sawEnoughEnemyUnits_ = false;
  bool canRunBos_ = true;
  float startTime_ = 0.0f;
};

} // namespace cherrypi
