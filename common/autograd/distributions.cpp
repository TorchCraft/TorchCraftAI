/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "distributions.h"
#include <cmath>

namespace {
const double kTwoPi = 2. * std::acos(-1);
}
namespace common {

torch::Tensor
normalPDF(torch::Tensor x, torch::Tensor mean, torch::Tensor std) {
  auto var = std * std;
  auto diff = mean - x;
  return (kTwoPi * var).sqrt() * ((-0.5 / var) * diff * diff).exp();
}
torch::Tensor normalPDF(torch::Tensor x, torch::Tensor mean, double std) {
  auto var = std * std;
  auto diff = mean - x;
  return sqrt((kTwoPi * var)) * ((-0.5 / var) * diff * diff).exp();
}

} // namespace common
