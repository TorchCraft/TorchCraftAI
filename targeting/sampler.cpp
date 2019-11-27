/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "sampler.h"
#include "common/autograd.h"
#include "common/rand.h"
#include "flags.h"
#include "keys.h"
#include "parameters.h"
#include <autogradpp/autograd.h>

using namespace common;

CustomGaussianSampler::CustomGaussianSampler(
    const std::string& policyKey,
    const std::string& policyPlayKey,
    const std::string& stdKey,
    const std::string& actionKey,
    const std::string& pActionKey)
    : cpid::ContinuousGaussianSampler(policyKey, stdKey, actionKey, pActionKey),
      policyPlayKey_(policyPlayKey) {}

ag::Variant CustomGaussianSampler::sample(ag::Variant in) {
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
  auto piPlay = in[policyPlayKey_];
  if (pi.dim() > 2) {
    LOG(FATAL) << "Expected at most 2 dimensions, but found " << pi.dim()
               << " in " << common::tensorInfo(pi);
    throw std::runtime_error("Policy doesn't have expected shape");
  }
  if (pi.dim() == 1) {
    pi = pi.unsqueeze(0);
    piPlay = piPlay.unsqueeze(0);
  }

  auto device = pi.options().device();
  ag::Variant& stdVar = dict[stdKey_];
  if (stdVar.isDouble() || stdVar.isFloat()) {
    double dev = stdVar.isDouble() ? stdVar.getDouble() : stdVar.getFloat();
    // we do sampling on cpu for now
    dict[actionKey_] =
        ag::Variant(at::normal(
                        piPlay.to(at::kCPU),
                        dev / float(Parameters::get_int("correlated_steps")),
                        Rand::gen())
                        .to(device));
    dict[pActionKey_] =
        ag::Variant(common::normalPDF(dict[actionKey_].get(), pi, dev));
  } else {
    torch::Tensor dev = in[stdKey_];
    // we do sampling on cpu for now
    dict[actionKey_] = ag::Variant(
        at::normal(
            piPlay.to(at::kCPU),
            dev.to(at::kCPU) / float(Parameters::get_int("correlated_steps")),
            Rand::gen())
            .to(device));
    dict[pActionKey_] =
        ag::Variant(common::normalPDF(dict[actionKey_].get(), pi, dev));
  }

  return in;
}
CustomMultinomialSampler::CustomMultinomialSampler(
    const std::string& policyKey,
    const std::string& actionKey,
    const std::string& pActionKey)
    : cpid::MultinomialSampler(policyKey, actionKey, pActionKey) {}

ag::Variant CustomMultinomialSampler::computeProba(
    const ag::Variant& in,
    const ag::Variant& action) {
  auto& dict = in.getDict();
  if (dict.count(policyKey_) == 0) {
    throw std::runtime_error(
        "Policy key not found while computing action proba");
  }
  torch::Tensor actions = action.get().view({-1});
  torch::Tensor pi = in[policyKey_];
  auto num_allies = in[keys::kNumAllies].to(torch::kCPU);
  auto num_enemies = in[keys::kNumEnemies].to(torch::kCPU) + 1;
  const int bs = num_allies.size(0);
  torch::Tensor num_pairs = (num_allies * (num_enemies)).to(torch::kCPU);
  auto num_allies_acc = num_allies.accessor<long, 1>();
  auto num_enemies_acc = num_enemies.accessor<long, 1>();
  auto num_pairs_acc = num_pairs.accessor<long, 1>();
  auto device = actions.options().device();

  auto result = torch::zeros({actions.size(0)}).to(device);

  int currentStartPi = 0;
  int currentStartAct = 0;
  int currentOutput = 0;
  for (int i = 0; i < bs; ++i) {
    auto currentMat =
        pi.slice(0, currentStartPi, currentStartPi + num_pairs_acc[i])
            .view({num_allies_acc[i], num_enemies_acc[i]});
    currentStartPi += num_pairs_acc[i];

    auto currentAct =
        actions.slice(0, currentStartAct, currentStartAct + num_allies_acc[i])
            .view({num_allies_acc[i], 1});
    currentStartAct += num_allies_acc[i];
    auto probas = currentMat.gather(1, currentAct).squeeze();

    result.slice(0, currentOutput, currentOutput + num_allies_acc[i])
        .copy_(probas);
    currentOutput += num_allies_acc[i];
  }

  return ag::Variant(result);
}
