
/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "sarsa.h"
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
} // namespace

namespace cpid {
Sarsa::Sarsa(
    ag::Container model,
    ag::Optimizer optim,
    std::unique_ptr<BaseSampler> sampler,
    std::unique_ptr<AsyncBatcher> batcher,
    int returnsLength,
    int trainerBatchSize,
    float discount,
    bool gpuMemoryEfficient)
    : SyncTrainer(
          model,
          optim,
          std::move(sampler),
          std::move(batcher),
          returnsLength,
          1,
          trainerBatchSize,
          false,
          true,
          gpuMemoryEfficient),
      discount_(discount) {}

void Sarsa::doUpdate(
    const std::vector<std::shared_ptr<SyncFrame>>& seq,
    torch::Tensor terminal) {
  optim_->zero_grad();
  bool isCuda = model_->options().device().is_cuda();
  int batchSize = terminal.size(1);
  common::assertSize("terminal", terminal, {returnsLength_, batchSize});

  auto notTerminal = (1 - terminal);
  notTerminal = notTerminal.to(at::kFloat).set_requires_grad(false);

  torch::Tensor totValueLoss = torch::zeros({1});

  if (isCuda) {
    notTerminal = notTerminal.to(model_->options().device());
    totValueLoss = totValueLoss.to(model_->options().device());
  }

  // We will query the model for the current value of the action played at
  // eval time
  for (const auto& f : seq) {
    auto frame = std::static_pointer_cast<BatchedFrame>(f);
    frame->state = ag::VariantDict(
        {{"state", frame->state}, {kActionQKey, frame->action}});
  }
  computeAllForward(seq, batchSize);

  auto lastFrame = std::static_pointer_cast<BatchedFrame>(seq.back());

  ag::Variant& lastOut = lastFrame->forwarded_state;

  auto Q = lastOut[kQKey].view({batchSize});
  common::assertSize("Q", Q, {batchSize});

  auto discounted_reward =
      Q.detach().set_requires_grad(false).view({batchSize});
  common::assertSize("discounted_reward", discounted_reward, {batchSize});

  common::assertSize("notterminal", notTerminal, {returnsLength_, batchSize});
  for (int i = (int)seq.size() - 2; i >= 0; --i) {
    auto currentFrame = std::static_pointer_cast<BatchedFrame>(seq[i]);
    // ag::tensor_list currentOut = model_->forward(currentFrame->state);
    ag::Variant& currentOut = currentFrame->forwarded_state;
    auto currentQ = currentOut[kQKey].view({batchSize});

    // break the chain for terminal state, otherwise decay
    discounted_reward =
        (discounted_reward * discount_ * notTerminal[i]) + currentFrame->reward;

    auto valueLoss = at::smooth_l1_loss(currentQ, discounted_reward);
    totValueLoss = totValueLoss + valueLoss;
  }
  metricsContext_->pushEvent("value_loss", totValueLoss.item<float>());
  totValueLoss.backward();

  doOptimStep();
}
} // namespace cpid
