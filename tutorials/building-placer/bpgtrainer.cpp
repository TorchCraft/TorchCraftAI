/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "bpgtrainer.h"

#include <common/autograd.h>
#include <common/rand.h>
#include <cpid/distributed.h>

#include <fmt/format.h>

namespace {
float constexpr kImportanceRatioTruncation = 1.0f;
} // namespace

namespace cpid {
namespace dist = cpid::distributed;

ag::Variant BPGTrainer::forward(ag::Variant inp, EpisodeHandle const& handle) {
  MetricsContext::Timer forwardTimer(
      metricsContext_, "trainer:forward", kFwdMetricsSubsampling);
  std::shared_lock<std::shared_timed_mutex> lock(updateMutex_);
  return Trainer::forward(inp, handle);
}

void BPGTrainer::stepEpisode(
    GameUID const& id,
    EpisodeKey const& key,
    ReplayBuffer::Episode& episode) {
  {
    std::lock_guard<std::mutex> lock(newGamesMutex_);

    // The last frame of an episode is the "final" frame with no actual data
    auto episodeLength = ssize_t(episode.size()) - 1;
    for (auto i = 0U; i < episodeLength; i++) {
      newTransitions_.emplace_front(id, key, i);
      numActiveTransitions_.emplace(
          std::piecewise_construct,
          std::forward_as_tuple(id, key),
          std::forward_as_tuple(episodeLength));

      // If we run out of space in the replay buffer, simply throw out old
      // transitions.
      if (newTransitions_.size() > maxBufferSize_) {
        auto oldest = newTransitions_.back();
        auto handle = std::make_pair(oldest.gameId, oldest.episodeKey);
        --numActiveTransitions_[handle];
        if (numActiveTransitions_[handle] <= 0) {
          replayer_.erase(handle.first, handle.second);
          numActiveTransitions_.erase(handle);
        }
        newTransitions_.pop_back();
        if (metricsContext_) {
          metricsContext_->incCounter("trainer:transitions_replaced");
        }
      }
    }

    if (enoughTransitions_ != (newTransitions_.size() >= (size_t)batchSize_)) {
      enoughTransitions_ = true;
    }
  }
}

bool BPGTrainer::update() {
  // We want both enough transitions to form a full batch as well as at least
  // one new transition for the update.
  if (enoughTransitions_ && newTransitions_.size() > 0) {
    updateModel();
    return true;
  }
  return false;
}

void BPGTrainer::updateModel() {
  MetricsContext::Timer modelUpdateTimer(
      metricsContext_, "trainer:model_update");
  if (metricsContext_) {
    metricsContext_->pushEvent("trainer:batch");
  }
  auto rng = common::Rand::makeRandEngine<std::mt19937>();
  auto device = model_->options().device();

  std::vector<torch::Tensor> policyLoss;
  std::vector<torch::Tensor> entropyLoss;
  float batchReward = 0.0f;
  int numNewSamples = 0;
  for (auto b = 0; b < batchSize_; b++) {
    // Sample a transition from the replay buffer. We'll consume new transitions
    // first and fall back to random sampling from seen transitions otherwise.
    Transition transition;
    {
      std::lock_guard<std::mutex> nglock(newGamesMutex_);
      if (newTransitions_.size() > 0) {
        auto it = common::select_randomly(
            newTransitions_.begin(), newTransitions_.end(), rng);
        transition = std::move(*it);
        VLOG(3) << fmt::format(
            "Sampled transition {}/{}:{} from 'new' at {}",
            transition.gameId,
            transition.episodeKey,
            transition.frame,
            std::distance(newTransitions_.begin(), it));
        newTransitions_.erase(it);
        seenTransitions_.push_front(transition);
        ++numNewSamples;
      } else {
        auto it = common::select_randomly(
            seenTransitions_.begin(), seenTransitions_.end(), rng);
        transition = *it;
        VLOG(3) << fmt::format(
            "Sampled transition {}/{}:{} from 'seen' at {}",
            transition.gameId,
            transition.episodeKey,
            transition.frame,
            std::distance(seenTransitions_.begin(), it));
      }
    }

    auto episode = cast<BPGReplayBufferFrame>(
        replayer_.get(transition.gameId, transition.episodeKey));
    auto const& frame = episode[transition.frame];

    // Discounted sum of rewards from final to (including) the successor of this
    // transition's frame
    auto rturn = 0.0f;
    for (size_t i = episode.size() - 1; i > transition.frame; i--) {
      rturn = episode[i]->reward + gamma_ * rturn;
    }
    batchReward += rturn;

    // Model forward
    auto state = common::applyTransform(
        frame->state, [&](torch::Tensor x) { return x.to(device); });
    auto out = model_->forward(state);
    auto pdist = out["output"].squeeze(0); // get rid of mini-batch dimension
    auto mask = out["mask"].squeeze(0);

    // Policy loss
    auto pA = pdist[frame->action];
    auto importanceRatio = pA.item<float>() / frame->pAction;
    importanceRatio = std::min(importanceRatio, kImportanceRatioTruncation);
    policyLoss.push_back(-importanceRatio * rturn * pA.log());

    // Entropy loss
    if (eta_ >= 0) {
      auto numValidActions = mask.gt(0).sum().item<int32_t>();
      auto lambda = 1.0f / (eta_ + std::log(numValidActions - 1.0f));
      entropyLoss.push_back(lambda * (pdist * pdist.log()).sum());
    }
  }

  // Model backward
  auto policyLossMean = at::stack(policyLoss).mean();
  auto entropyLossMean = at::stack(entropyLoss).mean();
  auto totalLoss = (policyLossMean + entropyLossMean);
  totalLoss.backward();

  // Update stats
  if (metricsContext_) {
    metricsContext_->incCounter("trainer:model_updates");
    metricsContext_->pushEvent(
        "trainer:batch_policy_loss", policyLossMean.item<float>());
    metricsContext_->pushEvent(
        "trainer:batch_entropy_loss", entropyLossMean.item<float>());
    metricsContext_->pushEvent(
        "trainer:mean_batch_reward", batchReward / batchSize_);
    metricsContext_->pushEvent(
        "trainer:num_new_samples_per_update", numNewSamples);
  }

  // Update model parameters
  {
    std::lock_guard<std::mutex> lock(modelWriteMutex_);
    {
      MetricsContext::Timer timeAllreduce(
          metricsContext_, "trainer:allreduce_time");
      for (auto& var : model_->parameters()) {
        if (!var.grad().defined()) {
          continue;
        }
        dist::allreduce(var.grad());
        var.grad().div_(dist::globalContext()->size);
      }
    }
    {
      std::lock_guard<std::shared_timed_mutex> lock(updateMutex_);
      optim_->step();
    }
    optim_->zero_grad();
  }

  // Remove old transitions from seenTransitions_. If we removed all transitions
  // of an episode, we'll remove it from the replayer as well.
  while (seenTransitions_.size() > maxBufferSize_) {
    auto& oldest = seenTransitions_.back();
    auto handle = std::make_pair(oldest.gameId, oldest.episodeKey);
    --numActiveTransitions_[handle];
    if (numActiveTransitions_[handle] <= 0) {
      replayer_.erase(handle.first, handle.second);
      numActiveTransitions_.erase(handle);
      VLOG(0) << fmt::format(
          "No more active transitions from {}/{}, deleting from rpbuf",
          handle.first,
          handle.second);
    }
    seenTransitions_.pop_back();
  }
}

std::shared_ptr<ReplayBufferFrame> BPGTrainer::makeFrame(
    ag::Variant trainerOutput,
    ag::Variant state,
    float reward) {
  if (trainerOutput.getDict().empty() && state.getDict().empty()) {
    // Last frame in an episode: store reward only
    return std::make_shared<BPGReplayBufferFrame>(
        ag::VariantDict(), 0, 0.0f, reward);
  }

  auto action = trainerOutput["action"].item<int32_t>();
  auto prob = trainerOutput["output"][0][action].item<float>();
  return std::make_shared<BPGReplayBufferFrame>(state, action, prob, reward);
}

std::shared_ptr<Evaluator> BPGTrainer::makeEvaluator(
    size_t n,
    std::unique_ptr<BaseSampler> sampler) {
  return evaluatorFactory(
      model_,
      std::move(sampler),
      n,
      [this](ag::Variant inp, EpisodeHandle const&) {
        torch::NoGradGuard g;
        return model_->forward(inp);
      });
}

} // namespace cpid
