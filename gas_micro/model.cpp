/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "model.h"
#include "common.h"
#include "features/features.h"
#include "features/unitsfeatures.h"
#include "state.h"

namespace {
struct Fan {
  explicit Fan(torch::Tensor& tensor) {
    const auto dimensions = tensor.ndimension();
    AT_CHECK(
        dimensions >= 2,
        "Fan in and fan out can not be computed for tensor with fewer than 2 "
        "dimensions");

    if (dimensions == 2) {
      in = tensor.size(1);
      out = tensor.size(0);
    } else {
      in = tensor.size(1) * tensor[0][0].numel();
      out = tensor.size(0) * tensor[0][0].numel();
    }
  }

  int64_t in;
  int64_t out;
};
} // namespace

namespace microbattles {

ag::Variant MicroFeaturizer::featurize(cherrypi::State* state) {
  torch::NoGradGuard guard;
  auto unitFeaturizer = cherrypi::UnitStatFeaturizer();
  auto myUnitFeatures =
      unitFeaturizer.extract(state, state->unitsInfo().myUnits());
  auto nmyUnitFeatures =
      unitFeaturizer.extract(state, state->unitsInfo().enemyUnits());

  auto mapFeatures = featurizePlain(
      state,
      {cherrypi::PlainFeatureType::Walkability, // THIS SHOULD ALWAYS BE FIRST,
                                                // we rely on it in modelpf.cpp
       cherrypi::PlainFeatureType::Buildability,
       cherrypi::PlainFeatureType::OneHotGroundHeight,
       cherrypi::PlainFeatureType::FogOfWar},
      cherrypi::Rect(
          {-mapOffset(), -mapOffset()},
          {kMapHeight + mapOffset(), kMapWidth + mapOffset()}));
  auto mesh =
      at::stack(
          {torch::arange(0, kMapHeight, defaultDevice()).repeat({kMapWidth, 1}),
           torch::arange(0, kMapWidth, defaultDevice())
               .repeat({kMapHeight, 1})
               .t()},
          0)
          .toType(at::kFloat)
          .div_(512);
  auto xygrid = -1 *
      torch::ones({2, kMapHeight + mapPadding(), kMapWidth + mapPadding()});
  xygrid.slice(1, mapOffset(), kMapHeight + mapOffset())
      .slice(2, mapOffset(), kMapHeight + mapOffset())
      .copy_(mesh);
  auto mapTensor = torch::cat({mapFeatures.tensor, xygrid}, 0);
  assert(mapTensor.size(0) == kMapFeatures);
  return {
      mapTensor,
      myUnitFeatures.positions,
      myUnitFeatures.data,
      nmyUnitFeatures.positions,
      nmyUnitFeatures.data,
  };
}

int MicroFeaturizer::kNumUnitChannels =
    cherrypi::UnitStatFeaturizer::kNumChannels;

torch::Tensor kaiming_normal_(torch::Tensor tensor, double gain) {
  // Use Fan_in as default
  torch::NoGradGuard guard;
  Fan fan(tensor);
  const auto std = gain / std::sqrt(fan.in);
  return tensor.normal_(0, std);
}

void ResidualBlock::reset() {
  auto block1 = ag::Sequential();
  block1.append(ag::Conv2d(in_channels_, mid_channels_, kernel_size_)
                    .padding(padding_)
                    .stride(stride_)
                    .make());
  if (batchnorm_) {
    block1.append(ag::BatchNorm(mid_channels_).stateful(true).make());
  }
  block1.append(ag::Functional(nonlin_).make());
  block1.append(ag::Conv2d(mid_channels_, in_channels_, kernel_size_)
                    .padding(padding_)
                    .stride(stride_)
                    .make());
  if (batchnorm_) {
    block1.append(ag::BatchNorm(in_channels_).stateful(true).make());
  }
  block1_ = add(block1.make(), "block1");
  auto block2 = ag::Sequential();
  block2.append(ag::Functional(nonlin_).make());
  if (in_channels_ != out_channels_) {
    block2.append(ag::Conv2d(in_channels_, out_channels_, 1).make());
    block2.append(ag::Functional(nonlin_).make());
  }
  block2_ = add(block2.make(), "block2");
  for (auto& parameter : parameters()) {
    parameter.detach().normal_(0, 1);
  }
}

ag::Variant ResidualBlock::forward(ag::Variant inp) {
  // assume input
  torch::Tensor res;
  if (inp.isTensorList()) {
    if (inp.getTensorList().size() != 1) {
      throw std::runtime_error(
          "Malformed model input: " +
          std::to_string(inp.getTensorList().size()) + " inputs");
    }
    res = inp.getTensorList()[0];
  } else if (inp.isTensor()) {
    res = inp.get();
  } else {
    throw std::runtime_error("Forward received unsupported type");
  }
  auto output = block1_->forward(res)[0];
  return block2_->forward({output + res});
}
} // namespace microbattles
