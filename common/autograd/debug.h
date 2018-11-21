/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <autogradpp/autograd.h>
#include <glog/logging.h>
#include <torch/torch.h>

#define ASSERT_SIZE(T, ...) ::common::assertSize(#T, T, __VA_ARGS__)

/*
 * Useful helpers for neural networks expressed with Torch.
 */
namespace common {

/**
 * Returns a string containing the tensor type and sizes
 */
std::string tensorInfo(torch::Tensor x);

/**
 * Returns a string describing the content of a variant
 */
std::string variantInfo(ag::Variant x);

/**
 * Returns a string containing the tensor info, the max/min/mean and sum
 */
std::string tensorStats(torch::Tensor x);

/**
 * Throws if the given float tensor has a NaN or +/- infinity.
 */
void checkTensor(torch::Tensor x, bool logOnError = true);

/**
 * Verifies that a tensor's dimension sizes match expectations.
 * If a dimension is negative (e.g. -1) it won't be checked.
 * Throws a std::range_error if they don't.
 */
void assertSize(
    const std::string& name,
    const torch::Tensor& tensor,
    at::IntList sizes);

/**
 * Collects metrics about a container's weights
 */
struct WeightSummary {
  WeightSummary(torch::nn::Module&);
  long weights = 0;
  long zeroes = 0;
  long nans = 0;
  float norm1 = 0.0;
  float norm2 = 0.0;
  std::string toString() const;
};
std::ostream& operator<<(std::ostream& out, const WeightSummary& summary);

} // namespace common
