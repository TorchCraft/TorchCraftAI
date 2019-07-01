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

#include "gameutils/microfixedscenario.h"
#include  "gameutils/microfixedscenariopool.h"
#include "gas_trainer.h"
#include "gas_trainer_impala.h"
#include "micromodule.h"
#include "modules.h"
#include "trainingsetup.h"

namespace cp = cherrypi;

namespace microbattles {

class GasMicroModule : public MicroModule {
 public:
  float epsilon_ = 0.;
  float actLod_ = 0.;
  std::ranlux24 rngEngine_;
  int numGroups_ = 1;
  GasMicroModule(
      std::shared_ptr<TrainingSetup>,
      std::shared_ptr<cpid::Trainer>,
      std::unique_ptr<cp::Reward>&&);
  void onGameStart(cp::State* state) override;

 protected:
  std::pair<torch::Tensor, torch::Tensor> twoMeans(torch::Tensor locs, std::pair<int,int> lod_grp);
  void virtual act(cp::State* state) override;
  void doLastFrame(cp::State* state);
  torch::Tensor lastGrpCommands_;
  std::vector<PFMicroActionModel::PFMicroAction> lastActions_;
  uint64_t actionRepeatCounter_ = 0;
  std::map<std::pair<int, int>, torch::Tensor> groupMeans_;
};
} // namespace microbattles
