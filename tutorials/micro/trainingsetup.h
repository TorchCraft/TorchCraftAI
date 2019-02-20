/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "cpid/trainer.h"
#include "model.h"
#include <autogradpp/autograd.h>
#include <string>

namespace microbattles {

/**
 * The complete configuration of a micro training setup.
 */
struct TrainingSetup {
  std::shared_ptr<cpid::Trainer> trainer;
  ag::Optimizer optimizer;

  std::shared_ptr<MicroModel> model;

  TrainingSetup();

  /// Loads a model and metrics from a previous run
  /// The loaded model/metrics must have been from an identical TrainingSetup
  void loadModel(const std::string& resultsCheckpoint);

  /// Specify a path at which to serialize the model state.
  void setCheckpointLocation(const std::string& resultsCheckpoint);

 private:
  std::shared_ptr<MicroModel> selectModel();
  ag::Optimizer selectOptimizer();
};

} // namespace microbattles
