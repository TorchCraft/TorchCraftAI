/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "trainingsetup.h"
#include "cpid/batcher.h"
#include "cpid/checkpointer.h"
#include "cpid/estrainer.h"
#include "cpid/metrics.h"
#include "cpid/optimizers.h"
#include "cpid/sampler.h"
#include "cpid/zeroordertrainer.h"
#include "flags.h"
#include "gas_trainer.h"
#include "gas_trainer_impala.h"
#include "gasmodelpf.h"
#include "gasmodel_global.h"
#include "modeldummy.h"
#include "modelpf.h"
#include "modelsimplecnn.h"
#include "gasmodelcnn.h"
#include "gasmodel_globalcnn.h"

#include <autogradpp/autograd.h>
auto const vopts = &visdom::makeOpts;

namespace microbattles {

std::shared_ptr<PFMicroActionModel> TrainingSetup::selectModel() {
  auto modelName = FLAGS_model;
  if (modelName == "GasPF") {
    gasMode = true;
    return GasPFModel().make();
  }
  if (modelName == "GasCNN") {
    gasMode = true;
    return GasCNNModel().make();
  }
  if (modelName == "GasGlobalPF") {
    gasMode = true;
    return GasGlobalModel().make();
  }
  if (modelName == "GasGlobalCNN") {
    gasMode = true;
    return GasGlobalCNNModel().make();
  }
  if (modelName == "PF") {
    return PFModel().make();
  }
  if (modelName == "SimpleCNN") {
    return SimpleCNNModel().make();
  }
  if (modelName == "Dummy") {
    return DummyModel().make();
  }

  throw std::runtime_error("Unrecognized model: " + FLAGS_model);
}

void TrainingSetup::checkCompatibleFlags() {
}

TrainingSetup::TrainingSetup() {
  model = selectModel();
  setupWithModel(model);
}

bool TrainingSetup::loadModel(const std::string& resultsCheckpoint) {
  try {
    ag::load(resultsCheckpoint, model);
    setupWithModel(model);
    return true;
  } catch (const std::exception& e) {
    return false;
  }
}

bool TrainingSetup::loadTrainer(const std::string& resultsCheckpoint) {
  try {
    if (auto s = std::dynamic_pointer_cast<cpid::SyncTrainer>(trainer)) {
      ag::load(resultsCheckpoint, s);
    }
    else {
      ag::load(resultsCheckpoint, trainer);
    }
    if (auto g = std::dynamic_pointer_cast<cpid::GasTrainer>(trainer)) {
      g->updateTargetModel();
    }
    if (auto m = std::dynamic_pointer_cast<PFMicroActionModel>(trainer->model())) {
      model = m;
      return true;
    }
    return false;
  } catch (const std::exception& e) {
    return false;
  }
}

void TrainingSetup::setupWithModel(std::shared_ptr<PFMicroActionModel> model) {
  model->to(FLAGS_gpu ? at::kCUDA : at::kCPU);
  optimizer = cpid::selectOptimizer(model);
  trainer = createTrainer(model, optimizer);
  trainer->setTrain(!FLAGS_evaluate);
  checkCompatibleFlags();
}

std::unique_ptr<cpid::BaseSampler> TrainingSetup::createSampler() {
  if (FLAGS_sampler == "none") {
    return std::make_unique<cpid::BaseSampler>();
  } else if (FLAGS_sampler == "multinomial") {
    return std::make_unique<cpid::MultinomialSampler>();
  } else if (FLAGS_sampler == "max") {
    return std::make_unique<cpid::DiscreteMaxSampler>();
  } else {
    LOG(FATAL) << "Unknown sampler: " << FLAGS_sampler;
  }
}

std::shared_ptr<cpid::Trainer> TrainingSetup::createTrainer(
    std::shared_ptr<PFMicroActionModel> model,
    ag::Optimizer optimizer) {
  if (FLAGS_trainer == "es") {
    auto trainer = std::make_shared<cpid::ESTrainer>(
        model,
        optimizer,
        createSampler(),
        FLAGS_sigma,
        FLAGS_batch_size,
        16,
        true,
        cpid::ESTrainer::RewardTransform::kRankTransform,
        true);
    trainer->setBatcher(model->createBatcher(FLAGS_batch_size));
    return trainer;
  } else if (FLAGS_trainer == "gas") {
    trainerTakesPreviousActionAndState = true;
    modelProvidesValueKey = true;
    auto batcher = std::make_unique<cpid::AsyncBatcher>(
        model, FLAGS_batch_size, -1, false);
    batcher->setModel(model);
    auto gasTrainer = std::make_shared<cpid::GasTrainer>(
        model,
        optimizer,
        createSampler(),
        /* batcher */ std::move(batcher),
        /* returnsLength */ FLAGS_nsteps,
        // /* updateFreq */ 20,
        /* trainerBatchSize */ FLAGS_batch_size,
        /* gradient clipping */ FLAGS_gradient_clipping,
        /* discount */ FLAGS_discount,
        /* overlappingUpdates */ false,
        /* gpuMemoryEfficient */ true);
    return gasTrainer;
  } else if (FLAGS_trainer == "impala") {
    FLAGS_a2c = true;
    trainerTakesPreviousActionAndState = true;
    modelProvidesValueKey = false;
    auto batcher = std::make_unique<cpid::AsyncBatcher>(
        model, FLAGS_batch_size, -1, false);
    batcher->setModel(model);
    auto gasTrainer = std::make_shared<cpid::GasTrainerA2C>(
        model,
        optimizer,
        createSampler(),
        /* batcher */ std::move(batcher),
        /* returnsLength */ FLAGS_nsteps,
        // /* updateFreq */ 20,
        /* trainerBatchSize */ FLAGS_batch_size,
        /* gradient clipping */ FLAGS_gradient_clipping,
        /* discount */ FLAGS_discount,
        /* valueLossCoef */ 0.5,
        /* entropyLossCoef */ FLAGS_entropy_loss_coef,
        /* overlappingUpdates */ false,
        /* gpuMemoryEfficient */ true);
    return gasTrainer;
  } else {
    LOG(FATAL) << "Unknown trainer: " << FLAGS_trainer;
  }
}

void TrainingSetup::setVisdom(
    visdom::ConnectionParams vparams,
    std::string visdomEnv) {
  vs = std::make_shared<visdom::Visdom>(vparams, visdomEnv);
}

void TrainingSetup::updatePlot(
    std::string const& window,
    std::string const& title,
    std::string const& ytitle,
    float numUpdates,
    float value) {
  visdomWindows[window] = vs->line(
      torch::tensor(value),
      torch::tensor(float(numUpdates)),
      visdomWindows[window],
      vopts({{"title", title}, {"xtitle", "Updates"}, {"ytitle", ytitle}}),
      visdomWindows[window].empty() ? visdom::UpdateMethod::None
                                    : visdom::UpdateMethod::Append);
}
} // namespace microbattles
