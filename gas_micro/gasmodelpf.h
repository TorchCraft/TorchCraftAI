/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "common.h"
#include "model.h"
#include <autogradpp/autograd.h>
#include <functional>
#include "features/unitsfeatures.h"
#include "modelpf.h"

/**
 * Potential field (PF) and neural network (NN) components/models.
 **/
namespace microbattles {
std::pair<torch::Tensor, std::vector<PFMicroActionModel::PFMicroAction>> decodeCardinalGasOutput(
  cherrypi::State* state,
  ag::Variant input,
  ag::Variant output,
  int lod,
  float epsilon,
  std::ranlux24 rngEngine
);

struct GasPotentialKernel {
  static std::vector<torch::Tensor> mesh_; // H x W x [y, x]
  virtual int numParams() = 0;
  // locs: U x (y, x); params: U x numParams()
  // output: H x W x U
  virtual torch::Tensor forward(torch::Tensor locs, torch::Tensor params) = 0;
};

/**
 * This potential looks like:
 * |
 * |---------
 * |         \
 * |          \
 * ------------------------
 *          |  |
 *          a  b
 * with parameters p0 = (a - 10) / 20 and p1 = (b - a - 10) / 20
 * With a minimum of 1 walktiles of spread an dropoff
 *
 * I'm hoping the division and bias helps it initialize it to sane values,
 * i.e. a cliff of 10 walktiles and spread of another 10
 **/
struct GasPiecewiseLinearPotential : GasPotentialKernel {
  auto static constexpr minDropOff = 1; // So you always get 1 at the location
  int numParams() override {
    return 2;
  }

  torch::Tensor forward(torch::Tensor locs, torch::Tensor params) override;

  virtual ~GasPiecewiseLinearPotential(){};
};

class GasPFModel : public ag::Container_CRTP<GasPFModel>,
                   public GASMicroActionModel {
 public:
  TORCH_ARG(int, numUnitFeatures) = MicroFeaturizer::kNumUnitChannels;
  TORCH_ARG(int, numMapFeatures) = MicroFeaturizer::kMapFeatures;
  TORCH_ARG(int, numPotentials) = 32;
  TORCH_ARG(std::shared_ptr<GasPotentialKernel>, kernel) =
      std::make_shared<GasPiecewiseLinearPotential>();
  TORCH_ARG(int, numMapEmbSize) = 8;
  ag::Container ourUnitBaseEncoder_, nmyUnitBaseEncoder_, ourPotHead_,
      nmyPotHead_, ourEmbHead_, nmyEmbHead_, stateValueHead_;
  std::vector<ag::Container> evalNetworks_;
  std::ranlux24 rngEngine_{42};

  void reset() override;
  ag::Variant forward(ag::Variant inp) override;

  virtual std::vector<PFMicroAction> decodeOutput(
      cherrypi::State*,
      ag::Variant input,
      ag::Variant output) override;
  virtual std::pair<torch::Tensor, std::vector<PFMicroAction>> decodeGasOutput(
      cherrypi::State*,
      ag::Variant input,
      ag::Variant output,
      int lod,
      float epsilon) override;
  virtual std::shared_ptr<MicroFeaturizer> getFeaturizer() override;
};

struct GasFeaturizer : public PFFeaturizer {
  GasFeaturizer() {
    gasUnitFeaturizer_ = cherrypi::UnitTypeGasFeaturizer();
  }
  cherrypi::UnitTypeGasFeaturizer gasUnitFeaturizer_;
  virtual ag::Variant featurize(cherrypi::State* state) override;
};
} // namespace microbattles
