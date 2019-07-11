/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "zeroordertrainer.h"
#include "batcher.h"
#include "distributed.h"
#include "sampler.h"

#include <mutex>

namespace {} // namespace
DEFINE_double(zo_reg_lambda, 0.1, "Reward discount");

namespace cpid {

OnlineZORBTrainer::OnlineZORBTrainer(ag::Container model, ag::Optimizer optim)
    : Trainer(model, optim, std::make_unique<BaseSampler>(), nullptr),
      nEpisodes_(0) {}

Trainer::EpisodeHandle OnlineZORBTrainer::startEpisode() {
  // Don't start any episodes while we're updating
  std::lock_guard<std::mutex> lock(updateLock_);
  std::lock_guard<std::mutex> lock2(noiseLock_);
  auto handle = Trainer::startEpisode();
  auto uid = handle.gameID();
  auto k = handle.episodeKey();
  if (antithetic_ && lastNoise_.size() > 0) {
    for (size_t i = 0; i < lastNoise_.size(); i++) {
      lastNoise_[i] = -lastNoise_[i].clone();
    }
    noiseStash_[uid][k] = std::move(lastNoise_);
    lastNoise_.clear();
  } else {
    noiseStash_[uid][k] =
        std::dynamic_pointer_cast<ZOBackpropModel>(model_)->generateNoise();
    lastNoise_ = noiseStash_[uid][k];
  }
  return handle;
}

ag::Variant OnlineZORBTrainer::forward(
    ag::Variant inp,
    EpisodeHandle const& handle) {
  auto forward = model_->forward(inp).getTensorList();
  if (forward.size() % 4 != 0) {
    throw std::runtime_error(
        "Output of a model for OnlineZORBTrainer must have a multiple of 4 "
        "elements!");
  }

  auto gameUID = handle.gameID();
  auto key = handle.episodeKey();

  torch::NoGradGuard guard;
  ag::tensor_list ret;
  torch::Tensor noise;
  ret.reserve(forward.size() / 4);
  for (size_t i = 0; i < forward.size(); i += 4) {
    auto& psi = forward[i];
    auto& w = forward[i + 1];
    auto noiseIndex = forward[i + 2].item<int64_t>();

    std::lock_guard<std::mutex> lock(noiseLock_);
    auto perturbed = isActive(handle)
        ? w + delta_ * noiseStash_[gameUID][key][noiseIndex]
        : w;
    auto scores = psi.mv(perturbed);
    ret.push_back(torch::Tensor(std::get<1>(scores.max(0))));
    ret.push_back(torch::Tensor(scores));
  }
  return ret;
}

void OnlineZORBTrainer::stepEpisode(
    GameUID const& id,
    EpisodeKey const& k,
    ReplayBuffer::Episode& ep) {
  nEpisodes_++;
}

bool OnlineZORBTrainer::update() {
  namespace dist = distributed;
  episodes_ = nEpisodes_.load();
  dist::allreduce(&episodes_, 1);
  if (episodes_ < batchSize_) {
    return false;
  }
  MetricsContext::Timer modelUpdateTimer(
      metricsContext_, "trainer:model_update");
  if (metricsContext_) {
    metricsContext_->pushEvent("trainer:batch");
  }
  // On update, clear all active games since we're always on policy
  // We also block on new games
  std::lock_guard<std::mutex> updateLock(updateLock_);
  {
    std::unique_lock<std::shared_timed_mutex> lock(activeMapMutex_);
    actives_.clear();
  }

  // Now, there are no active games, everything active will unblock itself
  // and wait on the next call of startEpisode
  auto device = model_->options().device();
  optim_->zero_grad();
  auto meanBatchReward = 0.;
  auto batchLoss = 0.;

  for (auto& ep : replayBuffer().getAllEpisodes()) {
    auto episode = cast<OnlineZORBReplayBufferFrame>(ep.second);
    if (episode.size() == 0) {
      throw std::runtime_error("Why is episode size 0?");
    }
    auto& uid = ep.first.gameID;
    auto& k = ep.first.episodeKey;

    auto R = 0.;
    for (std::size_t j = episode.size() - 1; j >= 1; j--) { // Loop over time
      // Optimise w (state-independent)
      meanBatchReward += episode[j]->reward;
      R += episode[j]->reward;
      auto actions = episode[j - 1]->actions;
      auto rtrn = R / (episode.size() - j);

      // Optimise Ψ (state-dependent)
      auto& state = episode[j - 1]->state;
      ag::tensor_list stateVars;
      for (auto tensor : state) { // Add all state inputs for model
        stateVars.push_back(tensor.to(device).set_requires_grad(true));
      }
      auto out = model_->forward(ag::Variant(stateVars)).getTensorList();
      auto loss = torch::zeros({1}, device);

      // Each 4 is a (psi, w, ind to noise vector, value estimate) tuple
      for (size_t i = 0; i < out.size(); i += 4) {
        auto& psi = out[i]; // N x E
        auto& w = out[i + 1]; // E
        auto& uIndex = out[i + 2]; // long
        auto value = valueLambda_ == 0 // float
            ? torch::tensor(0, psi.options())
            : out[i + 3];
        auto uVar =
            torch::Tensor(noiseStash_[uid][k][uIndex.item<int64_t>()].toBackend(
                              w.type().backend()))
                .set_requires_grad(true); // E
        auto action = actions[i / 4];
        torch::Tensor psiActed = psi[action]; // E

        // Negative for gradient descent
        torch::Tensor wGrad = -(rtrn - value.item<float>()) * uVar;
        // This should be div, but we do mul for numerical stability
        torch::Tensor psiGrad = wGrad.mul(w.mul(psiActed).sign());

        loss += (w * wGrad).sum() + (psiActed * psiGrad).sum();
        if (valueLambda_ != 0) {
          loss += valueLambda_ *
              at::mse_loss(value, torch::tensor(rtrn, psi.options()));
        }
      }
      (loss / batchSize_).backward();
      batchLoss += loss.item<float>() / batchSize_;
    }
  }

  if (metricsContext_) {
    metricsContext_->incCounter("trainer:model_updates");
    metricsContext_->pushEvent("trainer:batch_loss", batchLoss);
    metricsContext_->pushEvent(
        "trainer:mean_batch_reward", meanBatchReward / batchSize_);
    metricsContext_->snapshotCounter("steps", "trainer:steps_per_batch", 0);
  }

  dist::allreduceGradients(model_);
  optim_->step();

  replayBuffer().clear();
  std::lock_guard<std::mutex> lock(noiseLock_);
  noiseStash_.clear();
  nEpisodes_ = 0;

  return true;
}

std::shared_ptr<ReplayBufferFrame> OnlineZORBTrainer::makeFrame(
    ag::Variant trainerOutput,
    ag::Variant state,
    float reward) {
  auto action = trainerOutput["action"];
  return std::make_shared<OnlineZORBReplayBufferFrame>(
      std::vector<torch::Tensor>({state[0]}),
      std::vector<long>({action.item<int32_t>()}),
      reward);
}

} // namespace cpid
