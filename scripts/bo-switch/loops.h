/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "models/bos/sample.h"

#include <common/parallel.h>

#include <cpid/centraltrainer.h>
#include <cpid/checkpointer.h>

#include <autogradpp/autograd.h>
#include <visdom/visdom.h>

DECLARE_int32(plot_every);

namespace cherrypi {

struct UpdateLoop {
  using EpisodeSamples = std::vector<bos::Sample>;

  ag::Container model;
  ag::Optimizer optim;
  cpid::CentralTrainer* trainer = nullptr;
  std::unique_ptr<cpid::Checkpointer> checkpointer;
  int batchSize;
  std::shared_ptr<visdom::Visdom> vs;

  bool dumpPredictions = false;
  bool train_ = true;
  int numBatches = 0;
  int saveModelInterval = 0;
  // Each entry is an entire episode
  std::vector<EpisodeSamples> episodes;

  UpdateLoop(int batchSize, std::shared_ptr<visdom::Visdom> vs = nullptr);
  virtual ~UpdateLoop();

  void setTrainer(cpid::CentralTrainer* trainer) {
    this->trainer = trainer;
    model = trainer->model();
    optim = trainer->optim();
  }

  void train() {
    model->train();
    train_ = true;
  }
  void eval() {
    model->eval();
    train_ = false;
  }

  virtual void operator()(EpisodeSamples episode);
  void flush(); // enqueue remaining samples
  void wait(); // until finished with current jobs
  void allreduceGradients(bool hasGrads = true);
  void updatePlot(
      std::string const& title,
      std::string const& ytitle,
      std::vector<float> values,
      std::vector<std::string> legend = {});
  void
  updatePlot(std::string const& title, std::string const& ytitle, float value) {
    updatePlot(title, ytitle, {value});
  }

  // Batch of features to (inputs, targets)
  virtual std::pair<ag::tensor_list, ag::tensor_list> preproc(
      std::vector<EpisodeSamples> episodes) = 0;
  // Model update
  virtual void update(ag::tensor_list inputs, ag::tensor_list targets) = 0;
  // Any past-wait stuff
  virtual void postWait();

 protected:
  // Keep a few counters for validation metrics, indexed by build and time
  using TwoDimVec = std::vector<std::vector<double>>;
  std::unordered_map<std::string, TwoDimVec> vcounters_;
  int64_t validMaxLen_ = 0;
  bool validCountsPlotted_ = false;
  std::map<int, std::string> indexToBo_;
  std::vector<std::string> boNames_;

 private:
  // For easier debugging, change the type of these to ...0>> fooC_;
  std::unique_ptr<common::BufferedConsumer<std::vector<EpisodeSamples>>>
      preprocC_;
  std::unique_ptr<
      common::BufferedConsumer<std::pair<ag::tensor_list, ag::tensor_list>>>
      updateC_;
  std::map<std::string, std::string> vsWindows_;
};

struct BpttUpdateLoop : UpdateLoop {
  int bptt;
  bool decisionsOnly;
  bool initialNonDecisionSamples = false;
  bool spatialFeatures = false;
  bool nonSpatialFeatures = true;

  BpttUpdateLoop(
      int batchSize,
      int bptt,
      bool decisionsOnly,
      std::shared_ptr<visdom::Visdom> vs = nullptr);
  virtual ~BpttUpdateLoop() = default;

  // Batch of features to (inputs, targets)
  virtual std::pair<ag::tensor_list, ag::tensor_list> preproc(
      std::vector<EpisodeSamples> episodes) override;
  // Model update
  virtual void update(ag::tensor_list inputs, ag::tensor_list targets) override;
  virtual void postWait() override;

 protected:
  void updateValidationMetrics(
      ag::tensor_list inputs,
      ag::tensor_list targets,
      ag::tensor_list outputs);
  void showPlots(
      ag::tensor_list inputs,
      ag::tensor_list targets,
      ag::tensor_list outputs,
      int index);
};

/**
 * This loop regards the usual batches as macro-batches and will perform
 * training on mini-batches instead.
 * Games of a whole batch will be featurized, the individual frames will be
 * shuffled and the model iterates over this using mini-batches, performing an
 * optimizer step after each one.
 *
 * It's advised to use a larger "normal" batch size here.
 */
struct MacroBatchUpdateLoop : UpdateLoop {
  int miniBatchSize;
  bool decisionsOnly;
  bool initialNonDecisionSamples = false;
  size_t numUpdates = 0;

  MacroBatchUpdateLoop(
      int batchSize,
      int miniBatchSize,
      bool decisionsOnly,
      std::shared_ptr<visdom::Visdom> vs = nullptr)
      : UpdateLoop(batchSize, vs),
        miniBatchSize(miniBatchSize),
        decisionsOnly(decisionsOnly) {}
  virtual ~MacroBatchUpdateLoop() = default;

  virtual void update(ag::tensor_list inputs, ag::tensor_list targets) override;
  virtual void postWait() override;

 protected:
  void updateValidationMetrics(
      ag::tensor_list inputs,
      ag::tensor_list targets,
      ag::tensor_list outputs);
};

/**
 * MacroBatchUpdateLoop with pre-propcessing for BosLinearModel.
 */
struct LinearModelUpdateLoop : MacroBatchUpdateLoop {
  using MacroBatchUpdateLoop::MacroBatchUpdateLoop;

  virtual std::pair<ag::tensor_list, ag::tensor_list> preproc(
      std::vector<EpisodeSamples> episodes) override;
};

struct IdleUpdateLoop : UpdateLoop {
  IdleUpdateLoop(int batchSize, std::shared_ptr<visdom::Visdom> vs = nullptr)
      : UpdateLoop(batchSize, vs){};
  virtual ~IdleUpdateLoop() = default;
  void operator()(EpisodeSamples episode) override;
  virtual std::pair<ag::tensor_list, ag::tensor_list> preproc(
      std::vector<EpisodeSamples> episodes) override;
  virtual void update(ag::tensor_list inputs, ag::tensor_list targets) override;
};

} // namespace cherrypi
