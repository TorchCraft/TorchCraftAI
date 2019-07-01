/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "cpid/checkpointer.h"
#include "cpid/trainer.h"
#include "model.h"
#include <autogradpp/autograd.h>
#include <string>
#include <visdom/visdom.h>

namespace microbattles {

/**
 * The complete configuration of a micro training setup.
 */
struct TrainingSetup {
  std::shared_ptr<cpid::Trainer> trainer;
  std::unique_ptr<cpid::Checkpointer> checkpointer;
  ag::Optimizer optimizer;

  std::shared_ptr<PFMicroActionModel> model;
  std::shared_ptr<visdom::Visdom> vs;
  std::map<std::string, std::string> visdomWindows;
  bool gasMode = false;
  bool trainerTakesPreviousActionAndState = false; // true for SyncTrainer
  bool modelProvidesValueKey = false;

  TrainingSetup();

  void setupWithModel(std::shared_ptr<PFMicroActionModel> model);

  /// Loads a model and metrics from a previous run
  /// The loaded model/metrics must have been from an identical TrainingSetup
  bool loadModel(const std::string& resultsCheckpoint);

  /// Loads a trainer and metrics from a previous run
  bool loadTrainer(const std::string& resultsCheckpoint);

  void setVisdom(visdom::ConnectionParams vparams, std::string visdomEnv);
  void updatePlot(
      std::string const& window,
      std::string const& title,
      std::string const& ytitle,
      float index,
      float value);

  std::unique_ptr<cpid::BaseSampler> createSampler();
  std::shared_ptr<cpid::Trainer> createTrainer(
      std::shared_ptr<PFMicroActionModel> model,
      ag::Optimizer optimizer);

 private:
  std::shared_ptr<PFMicroActionModel> selectModel();
  ag::Optimizer selectOptimizer();
  void checkCompatibleFlags();
};

} // namespace microbattles
