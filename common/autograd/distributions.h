/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <autogradpp/autograd.h>

namespace common {

/// Compute the PDF of the normal law
torch::Tensor normalPDF(torch::Tensor x, torch::Tensor mean, torch::Tensor std);
torch::Tensor normalPDF(torch::Tensor x, torch::Tensor mean, double std);

} // namespace common
