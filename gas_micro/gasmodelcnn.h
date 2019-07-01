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
#include "gasmodelpf.h"

/**
 * Potential field (PF) and neural network (NN) components/models.
 **/
namespace microbattles {

class GasCNNModel : public ag::Container_CRTP<GasCNNModel>,
                   public GASMicroActionModel {
 public:
  TORCH_ARG(int, numUnitFeatures) = MicroFeaturizer::kNumUnitChannels;
  TORCH_ARG(int, numMapFeatures) = MicroFeaturizer::kMapFeatures;
  TORCH_ARG(int, hidSz) = 64;
  TORCH_ARG(int, numMapEmbSize) = 8;
  TORCH_ARG(int, numUnitEmbSize) = 128;
  ag::Container ourUnitBaseEncoder_, nmyUnitBaseEncoder_, stateValueHead_;
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
  
  protected:
    std::vector<ag::Container> convLayers_;
};

} // namespace microbattles
