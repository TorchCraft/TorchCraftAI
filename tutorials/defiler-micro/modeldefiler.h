/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "features/features.h"
#include "features/unitsfeatures.h"
#include "flags.h"
#include "model.h"
#include "state.h"
#include "trainingsetup.h"
#include "utils.h"
#include <torchcraft/client.h>

namespace microbattles {
static auto constexpr embedSize = 16;
static auto constexpr kMapFeatures = 1;
static auto constexpr kDefilerFeatures = 1;
static auto constexpr kModelFeatures = embedSize * 2 * 3 + kDefilerFeatures +
    kMapFeatures +
    3; // 3 for darkswarm position, unit plagued and darkswarm timer

class ResidualBlock : public ag::Container_CRTP<ResidualBlock> {
 public:
  TORCH_ARG(int, in_channels);
  TORCH_ARG(int, out_channels);
  TORCH_ARG(int, kernel_size);
  TORCH_ARG(int, stride) = 1;
  TORCH_ARG(int, padding) = 0;
  TORCH_ARG(int, mid_channels) = 64;
  TORCH_ARG(bool, batchnorm) = true;
  TORCH_ARG(int, convs_replications) = 2;
  TORCH_ARG(std::function<at::Tensor(at::Tensor)>, nonlin) = torch::relu;
  virtual void reset() override;
  virtual ag::Variant forward(ag::Variant inp) override;

 protected:
  ag::Container block1_;
  ag::Container block2_;
};

struct DefileConv2dFeaturizer : public MicroFeaturizer {
  torch::Tensor lastUnitCounts;
  virtual ag::Variant featurize(cherrypi::State* state) override;
};

struct DefileConvNetFeaturizer : public DefileConv2dFeaturizer {
  torch::Tensor lastModelOutput;
  std::unordered_map<std::string, torch::Tensor> featuresFrom1;
  std::unordered_map<std::string, torch::Tensor> featuresFrom2;

  int res = torchcraft::BW::XYWalktilesPerBuildtile;
  int stride = torchcraft::BW::XYWalktilesPerBuildtile;
  cherrypi::UnitTypeDefoggerFeaturizer udf =
      cherrypi::UnitTypeDefoggerFeaturizer();
  //  buildtile res by default
  virtual ag::Variant featurize(cherrypi::State* state) override;
};

struct DefileConvNetModel : public virtual ag::ContainerImpl,
                            public PFMicroActionModel {
  // State, input, output
  TORCH_ARG(int, n_input_channels) = kModelFeatures;
  TORCH_ARG(float, plague_threshold) = 0.0;
  TORCH_ARG(float, dark_swarm_threshold) = 0.0;
  TORCH_ARG(int, stride) = torchcraft::BW::XYWalktilesPerBuildtile;
  TORCH_ARG(int, res) = torchcraft::BW::XYWalktilesPerBuildtile;
  TORCH_ARG(bool, mask_plague) = false;
  TORCH_ARG(bool, mask_dark_swarm) = false;
  virtual std::shared_ptr<MicroFeaturizer> getFeaturizer() override;
  virtual ag::Variant forward(ag::Variant inp) override;
  virtual void reset();
  virtual std::unique_ptr<cpid::AsyncBatcher> createBatcher(
      size_t batchSize) override {
    auto batcher = std::make_unique<cpid::SubBatchAsyncBatcher>(batchSize);
    batcher->allowPadding(true, 0);
    return batcher;
  }
  virtual std::vector<PFMicroActionModel::PFMicroAction> decodeOutput(
      cherrypi::State* state,
      ag::Variant input,
      ag::Variant output) override;

 protected:
  std::vector<ag::Container> convLayers_;
  ag::Container scatterSum_;
  ag::Container valuePooling_;
  ag::Container valueHead_;
};

class DefileConv2dModel : public ag::Container_CRTP<DefileConv2dModel>,
                          public DefileConvNetModel {
 public:
  TORCH_ARG(int, n_input_channels) = kModelFeatures;
  TORCH_ARG(float, plague_threshold) = 0.0;
  TORCH_ARG(float, dark_swarm_threshold) = 0.0;
  virtual void reset() override;
  virtual std::shared_ptr<MicroFeaturizer> getFeaturizer() override;
  virtual ag::Variant forward(ag::Variant inp) override;

 protected:
  ag::Container convnet_;
};

class DefileResConv2dModelBT2
    : public ag::Container_CRTP<DefileResConv2dModelBT2>,
      public DefileConvNetModel {
 public:
  TORCH_ARG(int, n_input_channels) = kModelFeatures;
  TORCH_ARG(float, plague_threshold) = 0.0;
  TORCH_ARG(float, dark_swarm_threshold) = 0.0;
  TORCH_ARG(bool, mask_plague) = false;
  TORCH_ARG(bool, mask_dark_swarm) = false;
  virtual void reset() override;
};

class DefileResConv2dBaseLineModel
    : public ag::Container_CRTP<DefileResConv2dBaseLineModel>,
      public DefileConvNetModel {
 public:
  TORCH_ARG(int, n_input_channels) = kModelFeatures;
  TORCH_ARG(float, plague_threshold) = 0.0;
  TORCH_ARG(float, dark_swarm_threshold) = 0.0;
  TORCH_ARG(bool, mask_plague) = false;
  TORCH_ARG(bool, mask_dark_swarm) = false;
  virtual void reset() override;
};

class DefileResEncoderDecoderModel
    : public ag::Container_CRTP<DefileResEncoderDecoderModel>,
      public DefileConvNetModel {
 public:
  TORCH_ARG(int, n_input_channels) = kModelFeatures;
  TORCH_ARG(float, plague_threshold) = 0.0;
  TORCH_ARG(float, dark_swarm_threshold) = 0.0;
  TORCH_ARG(bool, mask_plague) = false;
  TORCH_ARG(bool, mask_dark_swarm) = false;
  virtual void reset() override;
};

} // namespace microbattles
