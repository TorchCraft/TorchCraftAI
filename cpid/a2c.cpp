/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "a2c.h"
#include "batcher.h"
#include "common/autograd.h"
#include "sampler.h"

namespace {
const std::string kValueKey = "V";
const std::string kQKey = "Q";
const std::string kPiKey = "Pi";
const std::string kSigmaKey = "std";
const std::string kActionQKey = "actionQ";
const std::string kActionKey = "action";
const std::string kPActionKey = "pAction";
const std::string kPolSize = "pol_size";
} // namespace

namespace cpid {
A2C::A2C(
    ag::Container model,
    ag::Optimizer optim,
    std::unique_ptr<BaseSampler> sampler,
    std::unique_ptr<AsyncBatcher> batcher,
    int returnsLength,
    int updateFreq,
    int trainerBatchSize,
    float discount,
    float ratio_clamp,
    float entropy_ratio,
    float policy_ratio,
    bool overlappingUpdates,
    bool gpuMemoryEfficient,
    bool reduceGradients,
    float maxGradientNorm,
    const std::string& polSizeKey)
    : SyncTrainer(
          model,
          optim,
          std::move(sampler),
          std::move(batcher),
          returnsLength,
          updateFreq,
          trainerBatchSize,
          overlappingUpdates,
          false,
          gpuMemoryEfficient,
          reduceGradients,
          maxGradientNorm),
      discount_(discount),
      ratio_clamp_(ratio_clamp),
      entropy_ratio_(entropy_ratio),
      policy_ratio_(policy_ratio),
      polSizeKey_(polSizeKey) {}

torch::Tensor A2C::computePolicyLoss(
    std::shared_ptr<BatchedFrame> currentFrame,
    torch::Tensor advantage,
    int batchSize) {
  torch::Tensor currentActions = currentFrame->action.view({-1, 1});
  auto const& currentOut = currentFrame->forwarded_state;
  auto currentPolicy =
      currentOut[kPiKey]; //.view({currentactions.size(0), -1});

  auto pg_weights = advantage;
  std::vector<int64_t> pol_size;
  pg_weights =
      replicateAdvantage(pol_size, pg_weights, currentPolicy, currentOut);

  auto new_proba = sampler_->computeProba(currentOut, currentActions).get();
  auto old_proba = currentFrame->pAction.squeeze();

  auto importanceRatio =
      new_proba.detach().set_requires_grad(false) / old_proba;
  importanceRatio.clamp_max_(ratio_clamp_);
  metricsContext_->pushEvent(
      "importance_ratio", importanceRatio.mean().item<float>());

  pg_weights.mul_(importanceRatio);

  auto logPi = (currentPolicy + 1e-7).log();

  auto policyLoss = (-pg_weights * new_proba.log()).sum() / batchSize;
  auto entropyLoss = (logPi * currentPolicy).sum() / batchSize;
  metricsContext_->pushEvent("entropy_loss", entropyLoss.mean().item<float>());

  return policyLoss.sum() + entropyLoss * entropy_ratio_;
}

void A2C::setPolicyRatio(float pr) {
  policy_ratio_ = pr;
}

void A2C::doUpdate(
    const std::vector<std::shared_ptr<SyncFrame>>& seq,
    torch::Tensor terminal) {
  optim_->zero_grad();
  bool isCuda = model_->options().device().is_cuda();
  int batchSize = terminal.size(1);
  common::assertSize("terminal", terminal, {returnsLength_, batchSize});
  auto notTerminal = (1 - terminal);
  notTerminal = notTerminal.to(at::kFloat).set_requires_grad(false);

  torch::Tensor totValueLoss = torch::zeros({1});
  torch::Tensor totPolicyLoss = torch::zeros({1});

  if (isCuda) {
    notTerminal = notTerminal.to(model_->options().device());
    totValueLoss = totValueLoss.to(model_->options().device());
    totPolicyLoss = totPolicyLoss.to(model_->options().device());
  }
  computeAllForward(seq, batchSize);

  auto lastFrame = std::static_pointer_cast<BatchedFrame>(seq.back());
  ag::Variant const& lastOut = lastFrame->forwarded_state;

  auto V = lastOut[kValueKey].view({batchSize});
  common::assertSize("V", V, {batchSize});

  auto discounted_reward =
      V.detach().set_requires_grad(false).view({batchSize});
  common::assertSize("discounted_reward", discounted_reward, {batchSize});

  common::assertSize("notterminal", notTerminal, {returnsLength_, batchSize});
  for (int i = (int)seq.size() - 2; i >= 0; --i) {
    auto currentFrame = std::static_pointer_cast<BatchedFrame>(seq[i]);
    ag::Variant const& currentOut = currentFrame->forwarded_state;
    auto currentV = currentOut[kValueKey].view({batchSize});

    // add reward and break the chain for terminal state, otherwise decay
    // LOG(INFO) << "reward " << currentFrame->reward;
    discounted_reward =
        (discounted_reward * discount_ * notTerminal[i]) + currentFrame->reward;

    auto valueLoss = at::smooth_l1_loss(currentV, discounted_reward);
    totValueLoss = totValueLoss + valueLoss;

    // LOG(INFO) << "discounted " << discounted_reward;
    // LOG(INFO) << "currentV " << currentV;
    auto advantage =
        discounted_reward - currentV.detach().set_requires_grad(false);
    common::assertSize("advantage", advantage, {batchSize});

    {
      MetricsContext::Timer batchTimer(metricsContext_, "a2c:policyLoss");
      auto policyLoss = computePolicyLoss(currentFrame, advantage, batchSize);
      totPolicyLoss = totPolicyLoss + policyLoss;
    }
  }
  metricsContext_->pushEvent("value_loss", totValueLoss.item<float>());
  metricsContext_->pushEvent("policy_loss", totPolicyLoss.item<float>());
  {
    MetricsContext::Timer batchTimer(metricsContext_, "a2c:Backward");
    (totValueLoss + policy_ratio_ * totPolicyLoss).backward();
  }

  doOptimStep();
}

torch::Tensor A2C::replicateAdvantage(
    std::vector<int64_t>& polSizes,
    torch::Tensor const& pgWeights,
    torch::Tensor const& currentPolicy,
    ag::Variant const& currentOut) {
  if (pgWeights.size(0) == currentPolicy.size(0)) {
    polSizes = std::vector<int64_t>(currentPolicy.size(0), 1);
    return pgWeights.view(-1);
  }
  // in this case we have several actions per batch element.
  // we need to replicate the advantage tensor, so that we align the advantage
  // computed in one batch item to all the actions taken in that batch item.
  // we know the number of actions thanks to the key kPolSize.
  // we then use index_select to do the replication
  auto device = pgWeights.options().device();
  std::vector<torch::Tensor> indices;

  if (currentOut.getDict().count(kPolSize)) {
    torch::Tensor pol_size_tensor = currentOut[kPolSize].to(torch::kCPU);
    auto pol_sizeA = pol_size_tensor.accessor<int64_t, 1>();
    const int size = pol_size_tensor.size(0);
    for (int i = 0; i < size; ++i) {
      polSizes.push_back(pol_sizeA[i]);
    }
  } else {
    polSizes = SubBatchAsyncBatcher::findBatchInfo(
        currentOut.getDict().at(SubBatchAsyncBatcher::kBatchInfoKey), kPiKey);
  }

  LOG_IF(FATAL, polSizes.empty())
      << "It appears that there is more than one action taken per "
         "game. Please use the kPolSize key to describe that";

  for (auto i = 0U; i < polSizes.size(); ++i) {
    indices.push_back(
        torch::ones({polSizes[i]}, torch::kLong).to(device).fill_(int64_t(i)));
  }

  auto all_indices = torch::cat(indices);
  return pgWeights.view(-1).index_select(0, all_indices);
}

ContinuousA2C::ContinuousA2C(
    ag::Container model,
    ag::Optimizer optim,
    std::unique_ptr<BaseSampler> sampler,
    std::unique_ptr<AsyncBatcher> batcher,
    int returnsLength,
    int updateFreq,
    int trainerBatchSize,
    float discount,
    float ratio_clamp,
    float policy_ratio,
    float entropy_ratio,
    bool overlappingUpdates,
    bool gpuMemoryEfficient,
    bool reduceGradients,
    float maxGradientNorm)
    : A2C(model,
          optim,
          std::move(sampler),
          std::move(batcher),
          returnsLength,
          updateFreq,
          trainerBatchSize,
          discount,
          ratio_clamp,
          entropy_ratio,
          policy_ratio,
          overlappingUpdates,
          gpuMemoryEfficient,
          reduceGradients,
          maxGradientNorm) {}

torch::Tensor ContinuousA2C::computePolicyLoss(
    std::shared_ptr<BatchedFrame> currentFrame,
    torch::Tensor advantage,
    int batchSize) {
  auto const& currentOut = currentFrame->forwarded_state;
  torch::Tensor currentPolicy = currentOut[kPiKey].view({-1});
  torch::Tensor currentActions = currentFrame->action.view_as(currentPolicy);

  torch::Tensor sigma = currentOut[kSigmaKey].view_as(currentPolicy);

  auto var = sigma * sigma;

  auto pg_weights = advantage;
  std::vector<int64_t> pol_size;
  pg_weights =
      replicateAdvantage(pol_size, pg_weights, currentPolicy, currentOut);

  torch::Tensor new_likelihood =
      sampler_->computeProba(currentOut, currentActions)
          .get()
          .view_as(currentPolicy);

  torch::Tensor old_likelihood = currentFrame->pAction.view_as(currentPolicy);

  torch::Tensor importanceRatio =
      new_likelihood.detach().set_requires_grad(false) / old_likelihood;
  importanceRatio.clamp_max_(ratio_clamp_);
  importanceRatio.clamp_min_(1e-6);
  metricsContext_->pushEvent(
      "importance_ratio", importanceRatio.mean().item<float>());

  importanceRatio = importanceRatio.view_as(currentPolicy);

  pg_weights.mul_(importanceRatio);
  metricsContext_->pushEvent("pg_weights", pg_weights.mean().item<float>());
  metricsContext_->pushEvent(
      "new_likelihood", new_likelihood.mean().item<float>());

  auto logLikelihood = new_likelihood.log();
  metricsContext_->pushEvent(
      "loglikelihood", new_likelihood.log().mean().item<float>());

  auto policyLoss = (-pg_weights * logLikelihood).sum() / batchSize;

  metricsContext_->pushEvent(
      "frame_policy_loss", policyLoss.mean().item<float>());
  return policyLoss;
}

} // namespace cpid
