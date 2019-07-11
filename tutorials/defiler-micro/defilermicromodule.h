/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <map>
#include <memory>
#include <tuple>
#include <vector>

#include "gameutils/microscenarioproviderfixed.h"
#include "micromodule.h"
#include "modules.h"
#include "trainingsetup.h"

namespace microbattles {

class DefilerMicroModule : public MicroModule {
 public:
  DefilerMicroModule(
      std::shared_ptr<TrainingSetup>,
      std::shared_ptr<cpid::Trainer>,
      std::unique_ptr<cherrypi::Reward>&&);

  virtual void onGameStart(cherrypi::State* state) override;

 protected:
  virtual void forward(cherrypi::State* state) override;
};

} // namespace microbattles
