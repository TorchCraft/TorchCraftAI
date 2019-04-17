/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "policygradienttrainer.h"

#include "batcher.h"
#include "common/autograd.h"
#include "distributed.h"
#include "sampler.h"

namespace {
constexpr const float kImportanceRatioTruncation = 1.f;
std::string const kValueKey = "V";
std::string const kActionKey = "action";
std::string const kPActionKey = "pAction";
std::string const kHiddenKey = "hidden";
} // namespace

namespace cpid {

BatchedPGTrainer::BatchedPGTrainer(
    ag::Container model,
    ag::Optimizer optim,
    std::unique_ptr<BaseSampler> sampler,
    double gamma,
    int batchSize,
    std::size_t maxBatchSize,
    std::unique_ptr<AsyncBatcher> batcher)
    : Trainer(model, optim, std::move(sampler), std::move(batcher)),
      batchSize_(batchSize),
      maxBatchSize_(maxBatchSize),
      gamma_(gamma) {}

ag::Variant BatchedPGTrainer::forward(
    ag::Variant x,
    EpisodeHandle const& handle) {
  MetricsContext::Timer forwardTimer(
      metricsContext_, "trainer:forward", kFwdMetricsSubsampling);
  std::shared_lock<std::shared_timed_mutex> lock(updateMutex_);
  return Trainer::forward(x, handle);
}

void BatchedPGTrainer::stepEpisode(
    GameUID const& id,
    EpisodeKey const& k,
    ReplayBuffer::Episode&) {
  {
    std::lock_guard<std::mutex> lock(newGamesMutex_);
    newGames_.emplace_front(std::make_pair(id, k));
    if (newGames_.size() > (size_t)maxBatchSize_) {
      auto handle = newGames_.back();
      replayer_.erase(handle.first, handle.second);
      newGames_.pop_back();
      if (metricsContext_) {
        metricsContext_->incCounter("trainer:games_replaced");
      }
    }
    if (enoughEpisodes_ != (replayer_.sizeDone() >= (size_t)batchSize_)) {
      enoughEpisodes_ = true;
    }
  }

  if (onlineUpdates_) {
    updateModel();
  }
}

bool BatchedPGTrainer::update() {
  if (!onlineUpdates_ && enoughEpisodes_ && newGames_.size() > 0) {
    updateModel();
    return true;
  }
  return false;
}

void BatchedPGTrainer::updateModel() {
  namespace dist = distributed;
  MetricsContext::Timer modelUpdateTimer(
      metricsContext_, "trainer:model_update");
  if (metricsContext_) {
    metricsContext_->pushEvent("trainer:batch");
  }
  auto defaultOptions = model_->options().dtype(at::kFloat);
  std::vector<float> rturns;
  float policyLossSum = 0.0f;
  float valueLossSum = 0.0f;
  float meanBatchReward = 0.0f;
  for (auto b = 0; b < batchSize_; b++) {
    std::vector<BatchedPGReplayBufferFrame const*> episode;
    {
      std::lock_guard<std::mutex> lock(newGamesMutex_);
      if (newGames_.size() == 0) {
        auto eps = replayer_.sample()[0];
        episode = cast<BatchedPGReplayBufferFrame>(eps.second.get());
      } else {
        episode = cast<BatchedPGReplayBufferFrame>(
            replayer_.get(newGames_.back().first, newGames_.back().second));
        seenGames_.push(std::move(newGames_.back()));
        newGames_.pop_back();
        episodes_++;
      }
    }
    if (episode.size() == 0) {
      LOG(WARNING) << "Empty episode selected for update; skipping";
      continue;
    }

    rturns.clear();
    rturns.resize(episode.size() - 1);

    auto R = 0.;
    for (int i = episode.size() - 1; i >= 1; i--) {
      R = episode[i]->reward + gamma_ * R;
      rturns[i - 1] = R;
      meanBatchReward += episode[i]->reward;
    }

    // The second reward is associated with the first action
    ag::Variant prevOut;
    for (std::size_t i = 0; i < rturns.size(); i++) {
      auto state = common::applyTransform(
          episode[i]->state,
          [&](torch::Tensor t) { return t.to(defaultOptions.device()); });
      // Propagate hidden state if required by model
      if (state.isDict() && prevOut.isDict()) {
        auto& d = prevOut.getDict();
        if (d.find(kHiddenKey) != d.end()) {
          state.getDict().insert(
              std::make_pair(kHiddenKey, prevOut.getDict()[kHiddenKey]));
        }
      }

      auto taken_action = episode[i]->action.to(defaultOptions.device());
      auto& p_a = episode[i]->pAction;
      auto rturn = rturns[i];

      ag::Variant out;
      if (batcher_) {
        out = std::move(batcher_->unBatch(
            model_->forward(batcher_->makeBatch({state})), false, -1)[0]);
      } else {
        out = model_->forward(state);
      }

      torch::Tensor value = out[kValueKey];
      if (value.dim() > 1) {
        value.squeeze(0);
      }
      torch::Tensor newProba = sampler_->computeProba(out, taken_action).get();

      auto a = rturn - value.item<float>();
      auto importanceRatio = newProba.item<float>() / p_a;
      importanceRatio = std::min(importanceRatio, kImportanceRatioTruncation);

      auto policyLoss = -importanceRatio * a * newProba.log();
      auto valueLoss =
          at::mse_loss(value, torch::tensor(rturn, value.options()));
      (policyLoss + valueLoss).backward();

      policyLossSum += policyLoss.item<float>();
      valueLossSum += valueLoss.item<float>();
      std::swap(out, prevOut);
    }
  }

  if (metricsContext_) {
    metricsContext_->incCounter("trainer:model_updates");
    metricsContext_->pushEvent("trainer:batch_policy_loss", policyLossSum);
    metricsContext_->pushEvent("trainer:batch_value_loss", valueLossSum);
    auto loss = (policyLossSum + valueLossSum) / batchSize_;
    metricsContext_->pushEvent("trainer:batch_loss", loss);
    metricsContext_->pushEvent(
        "trainer:mean_batch_reward", meanBatchReward / batchSize_);
    metricsContext_->snapshotCounter("steps", "trainer:steps_per_batch", 0);
  }

  {
    std::lock_guard<std::mutex> lock(modelWriteMutex_);
    for (auto& var : model_->parameters()) {
      if (!var.grad().defined()) {
        continue;
      }
      {
        MetricsContext::Timer timeAllreduce(
            metricsContext_, "trainer:network_time");
        dist::allreduce(var.grad());
      }
      var.grad().div_(dist::globalContext()->size);
    }
    {
      std::lock_guard<std::shared_timed_mutex> lock(updateMutex_);
      optim_->step();
    }
    optim_->zero_grad();
    /*
    for (auto& pair : model_ ->parameters()) {
      std::cout << "Rank " << dist::globalContext()->rank << " " << pair.first
                << " norm: " << pair.second.norm().item<float>()
                << std::endl;
    }
    */
  }

  // Remove old episodes from replay buffer
  while (seenGames_.size() > maxBatchSize_) {
    auto& t = seenGames_.front();
    replayer_.erase(t.first, t.second);
    seenGames_.pop();
  }
}

void BatchedPGTrainer::doOnlineUpdatesInstead() {
  onlineUpdates_ = true;
}

std::shared_ptr<Evaluator> BatchedPGTrainer::makeEvaluator(
    size_t n,
    std::unique_ptr<BaseSampler> sampler) {
  return evaluatorFactory(
      model_,
      std::move(sampler),
      n,
      [this](ag::Variant inp, EpisodeHandle const&) {
        torch::NoGradGuard g;
        return this->model_->forward(inp);
      });
}

std::shared_ptr<ReplayBufferFrame> BatchedPGTrainer::makeFrame(
    ag::Variant trainerOutput,
    ag::Variant state,
    float reward) {
  if (trainerOutput.getDict().empty() && state.getDict().empty()) {
    // Last frame in an episode: store reward only
    return std::make_shared<BatchedPGReplayBufferFrame>(
        state, torch::Tensor(), 0.0f, reward);
  }

  auto action = trainerOutput[kActionKey];
  auto value = trainerOutput[kValueKey];
  float prob = 1.0f;
  if (trainerOutput.getDict().count(kPActionKey) > 0) {
    prob = trainerOutput[kPActionKey].item<float>();
  }

  torch::NoGradGuard g_;
  return std::make_shared<BatchedPGReplayBufferFrame>(
      state, action, prob, reward);
}

} // namespace cpid
