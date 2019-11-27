/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#include "cpid/sampler.h"
#include <autogradpp/autograd.h>

/**
 *  This sampler expects as input an unordered_map<string, Variant>, containing
 * an entry policyKey, which is a tensor of size [b, n]. It outputs the same
 * map, with a new key "action", a tensor of size [b] where each entry action[i]
 * is sampled from a normal distribution centered in policy[i]. It also expects
 * the stdKey to be set, it will be used as the standard deviation of the
 * normal. It can be either a float/double, in which case the deviation will be
 * the same for the batch, or it can be the same shape as the policy, for a
 * finer control. It also adds a key pActionKey which corresponds to the
 * probability of the sampled action
 *
 * It behaves as CustomGaussianSampler, exepct that it divides the sampling
 * variance by FLAGS_correlated_steps
 */
class CustomGaussianSampler : public cpid::ContinuousGaussianSampler {
 public:
  CustomGaussianSampler(
      const std::string& policyKey = "Pi",
      const std::string& policyPlayKey = "PiPlay",
      const std::string& stdKey = "std",
      const std::string& actionKey = "action",
      const std::string& pActionKey = "pAction");
  ag::Variant sample(ag::Variant in) override;

 protected:
  std::string policyPlayKey_;
};

class CustomMultinomialSampler : public cpid::MultinomialSampler {
 public:
  CustomMultinomialSampler(
      const std::string& policyKey = "Pi",
      const std::string& actionKey = "action",
      const std::string& pActionKey = "pAction");
  ag::Variant computeProba(const ag::Variant& in, const ag::Variant& action)
      override;
};
