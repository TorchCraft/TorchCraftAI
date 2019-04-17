/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * Legacy defogger featurizer.
 */

#pragma once

#include "cherrypi.h"
#include <array>
#include <torch/torch.h>

namespace cherrypi {

/**
 * Copypasted from defoggerFeaturizer on the defogger branch.
 */
class DefoggerFeaturizer {
 public:
  std::array<int, 234> typemapper;
  std::array<int, 234> itypemapper;
  size_t feature_size;
  size_t resX, resY;
  int32_t strideX, strideY;
  bool fullVision;

  DefoggerFeaturizer(
      size_t resX,
      size_t resY,
      int32_t strideX,
      int32_t strideY,
      bool fullVision = false)
      : resX(resX),
        resY(resY),
        strideX(strideX),
        strideY(strideY),
        fullVision(fullVision) {
    this->feature_size = 118;
    typemapper.fill(117);
    size_t i = 0;
    for (auto t : tc::BW::UnitType::_values()) {
      typemapper.at(t._to_integral()) = i;
      itypemapper.at(i) = t._to_integral();
      i++;
    }
  }

  torch::Tensor featurize(
      tc::Frame* frame,
      int mapX,
      int mapY,
      int playerId,
      at::Device device);
  void featurize_unit(torch::Tensor& feats, tc::Unit& u, int, int);
  void inc_feature(torch::Tensor& feature, int32_t c, int32_t x, int32_t y)
      const;
  static tc::Frame combine(const std::deque<tc::Frame>& frames, int playerId);
};

} // namespace cherrypi
