/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "sampler.h"
#include "common/autograd.h"
#include "common/rand.h"
#include <autogradpp/autograd.h>

using namespace common;
namespace cpid {

MultinomialSampler::MultinomialSampler(
    const std::string& policyKey,
    const std::string& actionKey,
    const std::string& pActionKey)
    : BaseSampler(),
      policyKey_(policyKey),
      actionKey_(actionKey),
      pActionKey_(pActionKey) {}

ag::Variant MultinomialSampler::sample(ag::Variant in) {
  torch::NoGradGuard g_;
  auto& dict = in.getDict();
  if (dict.count(policyKey_) == 0) {
    throw std::runtime_error("Policy key not found while sampling action");
  }
  torch::Tensor pi = in[policyKey_];
  if (pi.dim() > 2) {
    LOG(FATAL) << "Expected at most 2 dimensions, but found " << pi.dim()
               << " in " << common::tensorInfo(pi);
    throw std::runtime_error("Policy doesn't have expected shape");
  }
  bool hasBatch = pi.dim() >= 2;
  auto squeezeResult = [hasBatch](at::Tensor t) {
    return hasBatch ? t.squeeze(1) : t.squeeze();
  };
  if (pi.dim() == 1) {
    pi = pi.unsqueeze(0);
  }

  auto device = pi.options().device();
  // we do sampling on cpu for now
  dict[actionKey_] = ag::Variant(
      squeezeResult(pi.to(at::kCPU).multinomial(1, false, Rand::gen()))
          .to(device));
  dict[pActionKey_] = ag::Variant(
      squeezeResult(pi.gather(1, dict[actionKey_].get().view({-1, 1}))));
  return in;
}

ag::Variant MultinomialSampler::computeProba(
    const ag::Variant& in,
    const ag::Variant& action) {
  auto& dict = in.getDict();
  if (dict.count(policyKey_) == 0) {
    throw std::runtime_error("Policy key not found while sampling action");
  }
  torch::Tensor pi = in[policyKey_];
  if (pi.dim() > 2) {
    LOG(FATAL) << "Expected at most 2 dimensions, but found " << pi.dim()
               << " in " << common::tensorInfo(pi);
    throw std::runtime_error("Policy doesn't have expected shape");
  }
  bool hasBatch = pi.dim() >= 2;
  auto squeezeResult = [hasBatch](at::Tensor t) {
    return hasBatch ? t.squeeze(1) : t.squeeze();
  };
  if (pi.dim() == 1) {
    pi = pi.unsqueeze(0);
  }
  return ag::Variant(squeezeResult(pi.gather(1, action.get().view({-1, 1}))));
}

DiscreteMaxSampler::DiscreteMaxSampler(
    const std::string& policyKey,
    const std::string& actionKey,
    const std::string& pActionKey)
    : BaseSampler(),
      policyKey_(policyKey),
      actionKey_(actionKey),
      pActionKey_(pActionKey) {}

ag::Variant DiscreteMaxSampler::sample(ag::Variant in) {
  torch::NoGradGuard g_;
  auto& dict = in.getDict();
  if (dict.count(policyKey_) == 0) {
    throw std::runtime_error("Policy key not found while sampling action");
  }
  auto pi = in[policyKey_];
  if (pi.dim() > 2) {
    LOG(FATAL) << "Expected at most 2 dimensions, but found " << pi.dim()
               << " in " << common::tensorInfo(pi);
    throw std::runtime_error("Policy doesn't have expected shape");
  }
  if (pi.dim() == 1) {
    pi = pi.unsqueeze(0);
  }

  dict[actionKey_] = ag::Variant(std::get<1>(pi.max(1)));
  dict[pActionKey_] = ag::Variant(1);
  return in;
}

ContinuousGaussianSampler::ContinuousGaussianSampler(
    const std::string& policyKey,
    const std::string& stdKey,
    const std::string& actionKey,
    const std::string& pActionKey)
    : BaseSampler(),
      policyKey_(policyKey),
      stdKey_(stdKey),
      actionKey_(actionKey),
      pActionKey_(pActionKey) {}

ag::Variant ContinuousGaussianSampler::sample(ag::Variant in) {
  torch::NoGradGuard g_;
  auto& dict = in.getDict();
  if (dict.count(policyKey_) == 0) {
    throw std::runtime_error("Policy key not found while sampling action");
  }
  if (dict.count(stdKey_) == 0) {
    throw std::runtime_error(
        "Standard deviation key not found while sampling continuous action");
  }
  auto pi = in[policyKey_];
  if (pi.dim() > 2) {
    LOG(FATAL) << "Expected at most 2 dimensions, but found " << pi.dim()
               << " in " << common::tensorInfo(pi);
    throw std::runtime_error("Policy doesn't have expected shape");
  }
  if (pi.dim() == 1) {
    pi = pi.unsqueeze(0);
  }

  auto device = pi.options().device();
  ag::Variant& stdVar = dict[stdKey_];
  if (stdVar.isDouble() || stdVar.isFloat()) {
    double dev = stdVar.isDouble() ? stdVar.getDouble() : stdVar.getFloat();
    // we do sampling on cpu for now
    dict[actionKey_] =
        ag::Variant(at::normal(pi.to(at::kCPU), dev, Rand::gen()).to(device));
    dict[pActionKey_] =
        ag::Variant(common::normalPDF(dict[actionKey_].get(), pi, dev));
  } else {
    torch::Tensor dev = in[stdKey_];
    // we do sampling on cpu for now
    dict[actionKey_] = ag::Variant(
        at::normal(pi.to(at::kCPU), dev.to(at::kCPU), Rand::gen()).to(device));
    dict[pActionKey_] =
        ag::Variant(common::normalPDF(dict[actionKey_].get(), pi, dev));
  }

  return in;
}
ag::Variant ContinuousGaussianSampler::computeProba(
    const ag::Variant& in,
    const ag::Variant& action) {
  auto& dict = in.getDict();
  if (dict.count(policyKey_) == 0) {
    throw std::runtime_error("Policy key not found while sampling action");
  }
  torch::Tensor pi = in[policyKey_];
  if (pi.dim() > 2) {
    LOG(FATAL) << "Expected at most 2 dimensions, but found " << pi.dim()
               << " in " << common::tensorInfo(pi);
    throw std::runtime_error("Policy doesn't have expected shape");
  }
  if (pi.dim() == 1) {
    pi = pi.unsqueeze(0);
  }

  ag::Variant const& stdVar = dict.at(stdKey_);
  if (stdVar.isDouble() || stdVar.isFloat()) {
    double dev = stdVar.isDouble() ? stdVar.getDouble() : stdVar.getFloat();
    return ag::Variant(common::normalPDF(action.get(), pi, dev));
  }
  torch::Tensor dev = in[stdKey_];
  return ag::Variant(common::normalPDF(action.get(), pi, dev));
}

ContinuousDeterministicSampler::ContinuousDeterministicSampler(
    const std::string& policyKey,
    const std::string& actionKey,
    const std::string& pActionKey)
    : BaseSampler(),
      policyKey_(policyKey),
      actionKey_(actionKey),
      pActionKey_(pActionKey) {}

ag::Variant ContinuousDeterministicSampler::sample(ag::Variant in) {
  torch::NoGradGuard g_;
  auto& dict = in.getDict();
  if (dict.count(policyKey_) == 0) {
    throw std::runtime_error("Policy key not found while sampling action");
  }
  auto pi = in[policyKey_];
  if (pi.dim() > 2) {
    LOG(FATAL) << "Expected at most 2 dimensions, but found " << pi.dim()
               << " in " << common::tensorInfo(pi);
    throw std::runtime_error("Policy doesn't have expected shape");
  }
  if (pi.dim() == 1) {
    pi = pi.unsqueeze(0);
  }

  dict[actionKey_] = ag::Variant(pi.clone());
  dict[pActionKey_] = ag::Variant(1);
  return in;
}

EpsGreedySampler::EpsGreedySampler(
    double eps,
    const std::string& QKey,
    const std::string& actionKey)
    : eps_(eps), QKey_(QKey), actionKey_(actionKey){};

ag::Variant EpsGreedySampler::sample(ag::Variant in) {
  auto& dict = in.getDict();
  if (dict.count(QKey_) == 0) {
    throw std::runtime_error("Q key not found while sampling action");
  }
  auto Q = dict.at(QKey_).get();
  if (Q.dim() > 2) {
    LOG(FATAL) << "Expected at most 2 dimensions, but found " << Q.dim()
               << " in " << common::tensorInfo(Q);
    throw std::runtime_error("Q doesn't have expected shape");
  }
  if (Q.dim() == 1) {
    Q = Q.unsqueeze(0);
  }
  const auto batchSize = Q.size(0);
  const auto numAction = Q.size(1);
  // randomly break ties
  Q = at::normal(Q.to(torch::kCPU), 1e-5, common::Rand::gen());

  if (numAction < 1) {
    LOG(FATAL) << "Expected at lest one action";
  }
  // compute the greedy actions
  torch::Tensor actions = std::get<1>(Q.max(1));

  // sample random actions
  torch::Tensor randAction = torch::randint(
      0, numAction, actions.sizes(), common::Rand::gen(), torch::kLong);

  // proba of being random
  torch::Tensor rejectProba = torch::zeros(2);
  rejectProba[0] = 1. - eps_;
  rejectProba[1] = eps_;
  torch::Tensor reject =
      rejectProba.multinomial(batchSize, true, common::Rand::gen())
          .to(at::kByte);

  common::maskedCopy_(actions, reject, randAction);

  dict[actionKey_] = ag::Variant(actions);
  return in;
};
} // namespace cpid
