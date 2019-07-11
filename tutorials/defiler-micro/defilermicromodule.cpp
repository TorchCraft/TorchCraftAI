/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "defilermicromodule.h"

#include "common/autograd.h"
#include "cpid/batcher.h"
#include "cpid/distributed.h"
#include "cpid/estrainer.h"
#include "cpid/evaluator.h"
#include "cpid/sampler.h"
#include "cpid/zeroordertrainer.h"
#include "modules/cherryvisdumper.h"

#include "utils.h"

#include "common.h"
#include "flags.h"
#include "model.h"
#include "modeldefiler.h"

#include <visdom/visdom.h>

using namespace cherrypi;

namespace microbattles {

DefilerMicroModule::DefilerMicroModule(
    std::shared_ptr<TrainingSetup> setup,
    std::shared_ptr<cpid::Trainer> trainer,
    std::unique_ptr<Reward>&& reward)
    : MicroModule(setup, trainer, std::move(reward)) {
  setName("DefilerMicroLearner");
}

void DefilerMicroModule::onGameStart(State* state) {
  MicroModule::onGameStart(state);
  numericMetrics["model_launch"] = 0;
}

void DefilerMicroModule::forward(State* state) {
  if ((!started_ || !handle) && !inFullGame) {
    return;
  }

  if (!inFullGame && reward_->terminate(state)) {
    trainerStep(state, true);
    return;
  }

  auto defilers = state->unitsInfo().myUnitsOfType(buildtypes::Zerg_Defiler);
  if (defilers.empty()) {
    return;
  }
  if (episodeCurrentFrame(state) == 0 ||
      episodeCurrentFrame(state) - lastForwardFrame_ < FLAGS_frame_skip ||
      FLAGS_defiler_rule) {
    return;
  }
  numericMetrics["model_launch"] += 1;
  MicroModule::forward(state);
}
} // namespace microbattles
