/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "trainingsetup.h"
#include "cpid/batcher.h"
#include "cpid/estrainer.h"
#include "cpid/optimizers.h"
#include "cpid/sampler.h"
#include "cpid/zeroordertrainer.h"
#include "modelpf.h"

#include <autogradpp/autograd.h>

namespace microbattles {

/// Decomposes a model into its MicroModel and Container components
template <class T>
std::tuple<std::shared_ptr<MicroModel>, ag::Container>
buildDecomposedMicroModel() {
  auto model = std::make_shared<T>();
  return std::make_tuple(
      std::dynamic_pointer_cast<MicroModel>(model), model->make());
}

std::shared_ptr<MicroModel> TrainingSetup::selectModel() {
  auto modelName = FLAGS_model;
  if (modelName == "PF") {
    return PFModel().make();
  }
  throw std::runtime_error("Unrecognized model: " + FLAGS_model);
}

TrainingSetup::TrainingSetup() {
  model = selectModel();
  model->to(at::kCUDA);
  optimizer = cpid::selectOptimizer(model);
  trainer = std::make_shared<cpid::ESTrainer>(
      model,
      optimizer,
      std::make_unique<cpid::BaseSampler>(),
      FLAGS_sigma,
      FLAGS_batch_size,
      16,
      true,
      cpid::ESTrainer::RewardTransform::kRankTransform,
      true);
  trainer->setCheckpointFrequency(FLAGS_checkpoint_freq);
  trainer->setTrain(!FLAGS_evaluate);
}

void TrainingSetup::loadModel(const std::string& resultsCheckpoint) {
  ag::load(resultsCheckpoint, trainer);
}

void TrainingSetup::setCheckpointLocation(
    const std::string& resultsCheckpoint) {
  trainer->setCheckpointLocation(resultsCheckpoint);
}

} // namespace microbattles
