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

/**
 * Potential field (PF) and neural network (NN) components/models.
 **/
namespace microbattles {

struct PotentialKernel {
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
struct PiecewiseLinearPotential : PotentialKernel {
  auto static constexpr minDropOff = 1; // So you always get 1 at the location
  int numParams() override {
    return 2;
  }

  torch::Tensor forward(torch::Tensor locs, torch::Tensor params) override;
};

class PFModel : public ag::Container_CRTP<PFModel>, public MicroModel {
 public:
  TORCH_ARG(int, numUnitFeatures) = MicroFeaturizer::kNumUnitChannels;
  TORCH_ARG(int, numMapFeatures) = MicroFeaturizer::kMapFeatures;
  TORCH_ARG(int, numPotentials) = 32;
  TORCH_ARG(std::shared_ptr<PotentialKernel>, kernel) =
      std::make_shared<PiecewiseLinearPotential>();
  TORCH_ARG(int, numMapEmbSize) = 8;
  ag::Container unitBaseEncoder_, ourPotHead_, nmyPotHead_, ourEmbHead_,
      nmyEmbHead_;
  ag::Container commandNetwork_, movementNetwork_, attackNetwork_, mapEncoder_;
  int numActions_ = 2;

  void reset() override;
  ag::Variant forward(ag::Variant inp) override;

  // State, input, output
  // I'm not actually what this should really do, perhaps it should also return
  // the thing to pass back into trainers that it supports...
  // Right now I just want things to train so ES means I don't have to care xd

  virtual std::vector<MicroAction> decodeOutput(
      cherrypi::State*,
      ag::tensor_list input,
      ag::tensor_list output) override;
  virtual std::shared_ptr<MicroFeaturizer> getFeaturizer() override;
};

} // namespace microbattles
