/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#include "trainer.h"
#include <autogradpp/autograd.h>

namespace cpid {

/**
 * A sampler takes the output of the model, and outputs an action accordingly.
 * The exact shape of the action is dependent on the rest of the training loop.
 * For convenience, the base sampling function is the identity.
 */
class BaseSampler {
 public:
  BaseSampler(){};

  virtual ~BaseSampler() = default;
  virtual ag::Variant sample(ag::Variant in) {
    return in;
  };

  virtual ag::Variant computeProba(
      const ag::Variant& in,
      const ag::Variant& action) {
    throw std::runtime_error("Proba computation not implemented...");
    return ag::Variant(0);
  }
};

/**
 * This sampler expects as input an unordered_map<string, Variant>, which
 * contains an entry policyKey, which is a tensor of size [b, n]. It outputs the
 * same map, with a new key actionKey, a tensor of size [b] where each entry is
 * in [0,n-1], and is the result of multinomial sampling over pi. It also adds a
 * key pActionKey which corresponds to the probability of the sampled action.
 */
class MultinomialSampler : public BaseSampler {
 public:
  MultinomialSampler(
      const std::string& policyKey = kPiKey,
      const std::string& actionKey = kActionKey,
      const std::string& pActionKey = kPActionKey);
  ag::Variant sample(ag::Variant in) override;
  ag::Variant computeProba(const ag::Variant& in, const ag::Variant& action)
      override;

 protected:
  std::string policyKey_, actionKey_, pActionKey_;
};

/**
 * This sampler expects as input an unordered_map<string, Variant>,  containing
 * an entry QKey, which is a tensor of size [b, n]. It outputs the same map,
 * with a new key kActionKey, a tensor of size [b] where each entry is in
 * [0,n-1], and correspond to the action with the highest score.
 */
class DiscreteMaxSampler : public BaseSampler {
 public:
  DiscreteMaxSampler(
      const std::string& policyKey = kPiKey,
      const std::string& actionKey = kActionKey);
  ag::Variant sample(ag::Variant in) override;

 protected:
  std::string policyKey_, actionKey_;
};

/**
 * This sampler expects as input an unordered_map<string, Variant>, containing
 * an entry policyKey, which is a tensor of size [b, n]. It outputs the same
 * map, with a new key kActionKey, a tensor of size [b] where each entry
 * action[i] is sampled from a normal distribution centered in policy[i]. It
 * also expects the stdKey to be set, it will be used as the standard deviation
 * of the normal. It can be either a float/double, in which case the deviation
 * will be the same for the batch, or it can be the same shape as the policy,
 * for a finer control. It also adds a key pActionKey which corresponds to the
 * probability of the sampled action.
 */
class ContinuousGaussianSampler : public BaseSampler {
 public:
  ContinuousGaussianSampler(
      const std::string& policyKey = kPiKey,
      const std::string& stdKey = kSigmaKey,
      const std::string& actionKey = kActionKey,
      const std::string& pActionKey = kPActionKey);
  ag::Variant sample(ag::Variant in) override;
  ag::Variant computeProba(const ag::Variant& in, const ag::Variant& action)
      override;

 protected:
  std::string policyKey_, stdKey_;
  std::string actionKey_, pActionKey_;
};

/**
 * This sampler expects as input an unordered_map<string, Variant> containing an
 * entry policyKey, which is a tensor of size [b, n]. It outputs the same map,
 * with a new key kActionKey, a clone of the policy.
 */
class ContinuousDeterministicSampler : public BaseSampler {
 public:
  ContinuousDeterministicSampler(
      const std::string& policyKey = kPiKey,
      const std::string& actionKey = kActionKey);
  ag::Variant sample(ag::Variant in) override;

 protected:
  std::string policyKey_;
  std::string actionKey_;
};

/**
 * This sampler expects as input an unordered_map<string, Variant> containing an
 * entry QKey, which is a tensor of size [b, n]. It outputs the same map, with a
 * new key actionKey, which contains the best action with proba 1-eps, and a
 * random action with proba eps.
 */
class EpsGreedySampler : public BaseSampler {
 public:
  EpsGreedySampler(
      double eps = 0.07,
      const std::string& QKey = kQKey,
      const std::string& actionKey = kActionKey);

  ag::Variant sample(ag::Variant in) override;

  double eps_;
  std::string QKey_, actionKey_;
};
} // namespace cpid
