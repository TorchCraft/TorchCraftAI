/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#include "common.h"
#include "flags.h"
#include <autogradpp/autograd.h>

namespace cpid {
class MetricsContext;
}
/** Computes the indices to allow hetereogenous crossproduct computation.

    The use-case is the following: you have two tensors U and V, that contains
   vectors, comming from a batch. Now, each batch-item has several vectors
   associated. If we denote U_{i,j} the jth element of the ith item of the
   batch, then U contains the vectors U_{0,0}, U_{0,1}, ... U_{0,N(0)}, ... U{B,
   N(B)}, where N(i) is the number of vectors associated with item i of the
   batch and B is the batchsize.

    Similarly, V contains the vectors V_{0,0}, V_{0,1}, ... V_{0,N'(0)}, ...
   V{B, N'(B)}. Note that in general N(i) != N'(i).

    This function returns two list of indices indU and indV, of size \sum_{i =
   0}^B N(i)*N'(i), such that if you iterate simultaneously on indU and indV,
   you obtain all the possible combinations of one vector of U and one vector of
   B that belong to the same batch item

    The parameters of this function are N and N'
 */
std::pair<torch::Tensor, torch::Tensor> crossProduct_indices(
    torch::Tensor indA,
    torch::Tensor indB,
    const c10::Device& device);

AUTOGRAD_CONTAINER_CLASS(Identity) {
 public:
  void reset() override;
  ag::Variant forward(ag::Variant x) override;
};

namespace cherrypi {

AUTOGRAD_CONTAINER_CLASS(TargetingModel) {
 public:
  ag::Container policyTrunk_;
  ag::Container valueTrunk_, valueHead_;

  ag::Container lpWeightsMLP_;
  ag::Container quadWeightsMLP_;

  ag::Container agentEmbed_;
  ag::Container taskEmbed_;


  int agentEmbedSize_;
  int taskEmbedSize_;

  std::shared_ptr<cpid::MetricsContext> metrics_;

  void reset() override;

  ag::Variant forward(ag::Variant inp) override;

  TORCH_ARG(ModelType, model_type);
  TORCH_ARG(bool, zeroLastLayer) = true;

  TORCH_ARG(int, inFeatures);
  TORCH_ARG(int, inPairFeatures) = 0;
};

} // namespace cherrypi
