/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "trainingsetup.h"
#include "cpid/a2c.h"
#include "cpid/batcher.h"
#include "cpid/estrainer.h"
#include "cpid/optimizers.h"
#include "cpid/sampler.h"
#include "cpid/zeroordertrainer.h"
#include "flags.h"
#include "modeldefiler.h"
#include "modeldummy.h"
#include "modelpf.h"
#include "modelsimplecnn.h"

#include <autogradpp/autograd.h>
auto const vopts = &visdom::makeOpts;

namespace microbattles {

namespace {
void assertSampler() {
  if (FLAGS_sampler == "none") {
    throw std::runtime_error("a sampler must be given for defiler model");
  }
}
} // namespace

std::shared_ptr<PFMicroActionModel> TrainingSetup::selectModel() {
  auto modelName = FLAGS_model;
  if (modelName == "PF") {
    return PFModel().make();
  }
  if (modelName == "SimpleCNN") {
    return SimpleCNNModel().make();
  }
  if (modelName == "Dummy") {
    return DummyModel().make();
  }
  if (modelName == "DefilerConv") {
    assertSampler();
    return DefileConv2dModel()
        .plague_threshold(FLAGS_plague_threshold)
        .dark_swarm_threshold(FLAGS_dark_swarm_threshold)
        .make();
  }
  if (modelName == "DefileResNet") {
    assertSampler();
    return DefileResConv2dModelBT2()
        .plague_threshold(FLAGS_plague_threshold)
        .dark_swarm_threshold(FLAGS_dark_swarm_threshold)
        .make();
  }
  if (modelName == "DefilerBaseline") {
    assertSampler();
    return DefileResConv2dBaseLineModel()
        .plague_threshold(FLAGS_plague_threshold)
        .dark_swarm_threshold(FLAGS_dark_swarm_threshold)
        .make();
  }
  throw std::runtime_error("Unrecognized model: " + FLAGS_model);
}

void TrainingSetup::checkCompatibleFlags() {
  if (FLAGS_sampler != "multinomial" && FLAGS_trainer == "a2c") {
    throw std::runtime_error("a2c only works with multinomial");
  }
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
    ag::load(resultsCheckpoint, trainer);
    return true;
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

std::unique_ptr<cpid::BaseSampler> TrainingSetup::createSampler(
    std::string sampler) {
  if (sampler == "none") {
    return std::make_unique<cpid::BaseSampler>();
  } else if (sampler == "multinomial") {
    return std::make_unique<cpid::MultinomialSampler>();
  } else if (sampler == "max") {
    return std::make_unique<cpid::DiscreteMaxSampler>();
  } else {
    LOG(FATAL) << "Unknown sampler: " << sampler;
    return nullptr;
  }
}

std::unique_ptr<cpid::BaseSampler> TrainingSetup::createSampler() {
  return createSampler(FLAGS_sampler);
}

std::shared_ptr<cpid::Trainer> TrainingSetup::createTrainer(
    std::shared_ptr<PFMicroActionModel> model,
    ag::Optimizer optimizer) {
  if (FLAGS_trainer == "es") {
    auto trainer = std::make_shared<cpid::ESTrainer>(
        model,
        optimizer,
        createSampler(),
        FLAGS_es_sigma,
        FLAGS_batch_size,
        16,
        true,
        cpid::ESTrainer::RewardTransform::kRankTransform,
        true);
    trainer->setBatcher(model->createBatcher(FLAGS_batch_size));
    return trainer;
  } else if (FLAGS_trainer == "a2c") {
    trainerTakesPreviousActionAndState = true;
    modelProvidesValueKey = true;
    auto batcher = model->createBatcher(FLAGS_batch_size);
    batcher->setModel(model);
    return std::make_shared<cpid::A2C>(
        model,
        optimizer,
        createSampler(),
        /* batcher */ std::move(batcher),
        /* returnsLength */ FLAGS_returns_length,
        /* updateFreq */ 20,
        /* trainerBatchSize */ FLAGS_batch_size,
        /* discount */ 0.99,
        /* ratio_clamp */ 10.,
        /* entropy_ratio */ 0.1,
        /* policy_ratio */ 0.1,
        /* overlappingUpdates */ false,
        /* gpuMemoryEfficient */ true);
  }

  LOG(FATAL) << "Unknown trainer: " << FLAGS_trainer;
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
