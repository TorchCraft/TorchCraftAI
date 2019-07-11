/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "estrainer.h"
#include "batcher.h"
#include "common/rand.h"
#include "evaluator.h"
#include "policygradienttrainer.h"
#include "sampler.h"

#include "distributed.h"
#include <ATen/CPUGenerator.h>
#ifdef HAVE_CUDA
#include <ATen/CUDAGenerator.h>
#endif
#include <math.h>

namespace cpid {

ESTrainer::ESTrainer(
    ag::Container model,
    ag::Optimizer optim,
    std::unique_ptr<BaseSampler> sampler,
    float std,
    size_t batchSize,
    size_t historyLength,
    bool antithetic,
    RewardTransform transform,
    bool onPolicy)
    : Trainer(model, optim, std::move(sampler), nullptr),
      std_(std),
      batchSize_(batchSize),
      historyLength_(historyLength),
      antithetic_(antithetic),
      transform_(transform),
      onPolicy_(onPolicy),
      gatherSize_(batchSize_ * distributed::globalContext()->size),
      allRewards_(gatherSize_),
      allGenerations_(gatherSize_),
      allSeeds_(gatherSize_),
      rewards_(batchSize_),
      generations_(batchSize_),
      seeds_(batchSize_) {
  auto clone = ag::clone(model_);
  modelsHistory_.emplace_back(0, clone);
}

void ESTrainer::stepEpisode(
    GameUID const& gameUID,
    EpisodeKey const& key,
    ReplayBuffer::Episode& /*gen_episode*/) {
  {
    std::unique_lock<std::shared_timed_mutex> lock(insertionMutex_);
    // We put the most recent epsidoes to the back of the queue.
    // When learning, we retrieve starting from the back, too. This way
    // we are trying to use the freshest ones first.
    newGames_.emplace_back(std::make_pair(gameUID, key));
  }
  {
    // free the model: update() would regenerate it when needed
    auto perturbedModelKey = std::make_pair(gameUID, key);
    std::unique_lock<std::shared_timed_mutex> lock(modelStorageMutex_);
    modelCache_.erase(gameToGenerationSeed_[perturbedModelKey]);
  }
}

bool ESTrainer::update() {
  // ES should never need gradients really
  torch::NoGradGuard guard;

  namespace dist = distributed;
  std::lock_guard<std::mutex> updateLock(updateMutex_);
  // TODO: split in smaller methods
  {
    std::shared_lock<std::shared_timed_mutex> lock(insertionMutex_);
    if (newGames_.size() < batchSize_) {
      if (onPolicy_ && gamesStarted_ < batchSize_) {
        batchBarrier_.notify_all();
      }
      return false;
    }
    if (onPolicy_ && (gamesStarted_ > batchSize_)) {
      LOG(FATAL) << "onPolicy, but we have too many games playing/played"
                 << " gamesStarted_ = " << gamesStarted_;
    }
  }

  if (metricsContext_) {
    metricsContext_->pushEvent("trainer:batch");
  }
  MetricsContext::Timer modelUpdateTimer(
      metricsContext_, "trainer:model_update");

  auto currentParams = model_->named_parameters(); // TODO: inefficient
  optim_->zero_grad();

  for (size_t b = 0; b < batchSize_; ++b) {
    std::vector<RewardBufferFrame const*> episode;
    GameUID gameUID;
    EpisodeKey key;
    {
      std::unique_lock<std::shared_timed_mutex> lock(insertionMutex_);
      gameUID = newGames_.back().first;
      key = newGames_.back().second;
      episode = cast<RewardBufferFrame>(replayer_.get(gameUID, key));
      newGames_.pop_back();
    }
    float episodeReward = 0.0;
    for (size_t i = 0; i < episode.size(); ++i) {
      episodeReward += episode[i]->reward;
    }

    auto perturbedModelKey = std::make_pair(gameUID, key);
    int generation;
    int64_t seed;
    {
      std::shared_lock<std::shared_timed_mutex> lock(modelStorageMutex_);
      auto modelKey = gameToGenerationSeed_[perturbedModelKey];
      generation = modelKey.first;
      seed = modelKey.second;
    }
    rewards_[b] = episodeReward;
    seeds_[b] = seed;
    generations_[b] = generation;

    // clean up
    replayer_.erase(gameUID, key);

    std::unique_lock<std::shared_timed_mutex> lock(modelStorageMutex_);
    gameToGenerationSeed_.erase(perturbedModelKey);
  }

  {
    MetricsContext::Timer timeAllreduce(
        metricsContext_, "trainer:network_time");
    dist::allgather(allRewards_.data(), rewards_.data(), (int)batchSize_);
    dist::allgather(
        allGenerations_.data(), generations_.data(), (int)batchSize_);
    dist::allgather(allSeeds_.data(), seeds_.data(), (int)batchSize_);
  }

  float meanBatchReward =
      std::accumulate(allRewards_.begin(), allRewards_.end(), 0.0f) /
      gatherSize_;

  auto rewardsAsT =
      torch::from_blob(allRewards_.data(), {(int64_t)gatherSize_});
  auto rewardsTransformed = rewardTransform(rewardsAsT, transform_);
  float meanGenerationsDelay = 0.0;

  int oldestGeneration = modelsHistory_.front().first;
  int latestGeneration = modelsHistory_.back().first;

  size_t outdatedEpisodes = 0;
  for (auto g : allGenerations_) {
    outdatedEpisodes += oldestGeneration > g ? 1 : 0;
  }
  if (outdatedEpisodes >= gatherSize_ / 2) {
    VLOG(0) << "Too many outdated episodes, " << outdatedEpisodes << "/"
            << gatherSize_ << ", consider increasing history length";
  }

  for (size_t b = 0; b < gatherSize_; ++b) {
    int generation = allGenerations_[b];
    int64_t seed = allSeeds_[b];
    float reward = rewardsTransformed[b].item<float>();

    ag::Container originalModel;
    {
      if (oldestGeneration > generation) {
        // we have an episode generated by perturbing a model that is too
        // old to be stored in the history. We skip such an episode.
        continue;
      }
      originalModel = modelsHistory_[generation - oldestGeneration].second;
    }

    // TODO: lookup in the cache for the local models
    ag::Container perturbedModel = generateModel(generation, seed);
    auto perturbedParams = perturbedModel->named_parameters();
    double importanceWeight = 1.0;
    if (generation != latestGeneration) {
      if (onPolicy_) {
        LOG(FATAL) << "While onPolicy, got episode of generation " << generation
                   << "while the current one is " << latestGeneration;
      }
      meanGenerationsDelay += latestGeneration - generation;
      // the perturbedModel was sampled from
      // N(originalParams, std_), but we want to pretend it was sampled from
      // N(model_, std_) importance weight would be iw = P(perturbed | model_,
      // std_) / P(perturbed | originalModel, std_) => log(iw) = 0.5 *
      // (-(perturbed - model_)^2 + (perturbed - originalModel_)^2) / std_^2
      auto originalParams = originalModel->named_parameters();
      double logImportanceWeight = 0.0;
      for (auto& it : currentParams) {
        auto& name = it.key();
        auto& perturbedTensor = perturbedParams[name];
        auto& originalTensor = originalParams[name];
        auto& currentTensor = currentParams[name];

        logImportanceWeight +=
            -(perturbedTensor - currentTensor).pow_(2.0).sum().item<float>() +
            (perturbedTensor - originalTensor).pow_(2.0).sum().item<float>();
      }
      logImportanceWeight *= 0.5 / std_ / std_;
      importanceWeight = exp(logImportanceWeight);
      importanceWeight = importanceWeight > 1.0 ? 1.0 : importanceWeight;
    }

    for (auto& it : currentParams) {
      auto& name = it.key();
      auto& modelVar = it.value();
      auto& modelValue = it.value();
      auto& pertrubedValue = perturbedParams[name];
      auto gradEstimate = pertrubedValue - modelValue;
      // Need to flip the sign as we maximize; we also adjust the normalizer to
      // account for outdated episodes which we have thrown away. In case if all
      // episodes are outdated, this line is never executed.
      gradEstimate.mul_(
          -1.0 * reward / std_ * importanceWeight /
          (gatherSize_ - outdatedEpisodes));
      if (modelVar.grad().defined()) {
        modelVar.grad().add_(gradEstimate);
      } else {
        modelVar.grad() = torch::Tensor(gradEstimate).set_requires_grad(true);
      }
    }
    // end NoGradGuard
  }
  {
    std::unique_lock<std::shared_timed_mutex> lock(currentModelMutex_);
    optim_->step();
    auto clone = ag::clone(model_);
    assert(clone->options() == model_->options());
    int newGeneration = latestGeneration + 1;
    modelsHistory_.emplace_back(newGeneration, clone);
    if (modelsHistory_.size() > historyLength_) {
      modelsHistory_.pop_front();
    }
  }
  if (metricsContext_) {
    metricsContext_->pushEvent("trainer:batch_policy_loss", 0.0);
    metricsContext_->pushEvent("trainer:batch_value_loss", 0.0);
    metricsContext_->pushEvent("trainer:batch_loss", 0.0);
    metricsContext_->pushEvent("trainer:mean_batch_reward", meanBatchReward);
    metricsContext_->snapshotCounter("steps", "trainer:steps_per_batch", 0);
    metricsContext_->pushEvent(
        "trainer:mean_generations_delay", meanGenerationsDelay / gatherSize_);
    metricsContext_->incCounter("trainer:model_updates");
    metricsContext_->incCounter("trainer:outdated_episodes", outdatedEpisodes);
  }

  if (onPolicy_) {
    std::lock_guard<std::shared_timed_mutex> mapLock(activeMapMutex_);
    if (!actives_.empty()) {
      LOG(FATAL)
          << "onPolicy, but somehow we have games at the end of the update!";
    }
    std::shared_lock<std::shared_timed_mutex> insertLock(insertionMutex_);
    // It is possible that after a reset() some new games would be pushed,
    // hence in the first learning after a reset there could be more than
    // batchSize_ games in the buffer.
    newGames_.clear();
    replayer_.clear();
    gamesStarted_ = 0;
    if (!waitUpdate_) {
      batchBarrier_.notify_all();
    }
  }
  return true;
}

void ESTrainer::forceStopEpisode(EpisodeHandle const& handle) {
  {
    std::unique_lock<std::mutex> updateLock(updateMutex_);
    if (onPolicy_ && isActive(handle)) {
      LOG_IF(FATAL, gamesStarted_ == 0)
          << "Stopping episode but gamesStarted_=0 already";
      gamesStarted_--;
    }
  }
  Trainer::forceStopEpisode(handle);
}

Trainer::EpisodeHandle ESTrainer::startEpisode() {
  using namespace std::chrono_literals;

  auto handle = [&]() {
    std::unique_lock<std::mutex> updateLock(updateMutex_);
    if (onPolicy_) {
      while (true) {
        // we need to produce a game, so we proceed
        if (gamesStarted_ < batchSize_) {
          break;
        }
        auto wakeReason = batchBarrier_.wait_for(updateLock, 100ms);
        if (wakeReason == std::cv_status::timeout) {
          return EpisodeHandle();
        }
      }
      gamesStarted_++;
    }
    return Trainer::startEpisode();
  }();
  if (!handle) {
    return handle;
  }

  auto modelKey = std::make_pair(handle.gameID(), handle.episodeKey());
  int64_t seed;
  // To implement the variance reduction via antithetic variates,
  // we always generate seeds in pairs with swapped signs: seed and -seed.
  // Hence, we first check if there's a seed value stashed before, otherwise
  // generate a new one and stash its pair in the seedQueue.
  {
    std::lock_guard<std::mutex> lock(seedQueueMutex_);
    if (seedQueue_.empty()) {
      populateSeedQueue();
    }
    seed = seedQueue_.back();
    seedQueue_.pop_back();
  }
  int generation;
  {
    std::shared_lock<std::shared_timed_mutex> lock(currentModelMutex_);
    generation = modelsHistory_.back().first;
  }
  auto model = generateModel(generation, seed);
  {
    std::unique_lock<std::shared_timed_mutex> lock(modelStorageMutex_);
    auto generationSeed = std::make_pair(generation, seed);
    gameToGenerationSeed_[modelKey] = generationSeed;
    modelCache_[generationSeed] = model;
  }
  return handle;
}

ag::Container ESTrainer::getGameModel(
    GameUID const& gameUID,
    EpisodeKey const& key) {
  if (!this->train_) {
    return model_;
  }
  auto modelKey = std::make_pair(gameUID, key);
  { // model was already generated
    std::shared_lock<std::shared_timed_mutex> lock(modelStorageMutex_);
    auto lookup = gameToGenerationSeed_.find(modelKey);
    if (lookup != gameToGenerationSeed_.end()) {
      auto generationSeedPair = lookup->second;
      return modelCache_[generationSeedPair];
    } else {
      return model_;
    }
  }
}

/// Re-creates model based on its seed and the generation it was produced from.
/// The absolute value of the seed is used for seeding, and its sign indicates
/// if we add or subtract the noise, which is used to implement antithetic
/// variates.
ag::Container ESTrainer::generateModel(int generation, int64_t seed) {
  torch::NoGradGuard guard;
  ag::Container originalModel;
  {
    std::shared_lock<std::shared_timed_mutex> lock(currentModelMutex_);
    auto oldestGeneration = modelsHistory_.front().first;
    if (oldestGeneration > generation) {
      // shouldn't happen that we call generateModel with a too old generation,
      // the only unlikely case is that the whole historyLength_ was maxed
      // during a single startEpisode call
      throw std::runtime_error(
          "Cannot generate a model from a too old generation, increase history "
          "length!");
    }
    originalModel = modelsHistory_[generation - oldestGeneration].second;
  }
  auto perturbed = ag::clone(originalModel);

  std::shared_ptr<at::Generator> generator;
  if (perturbed->options().device().is_cuda()) {
#ifdef HAVE_CUDA
    generator = std::make_shared<at::CUDAGenerator>(&at::globalContext());
    assert(perturbed->options() == originalModel->options());
#endif
  } else {
    generator = std::make_shared<at::CPUGenerator>(&at::globalContext());
    assert(perturbed->options() == originalModel->options());
  }
  // The actually applied perturbation would be sign(seed) * noise(abs(seed))
  generator->manualSeed(seed < 0 ? -seed : seed);
  auto clonedParams = perturbed->parameters();
  for (auto& tensor : clonedParams) {
    auto delta = at::zeros_like(tensor).normal_(0, std_, generator.get());
    tensor.add_(delta, seed < 0 ? -1.0 : 1.0);
  }
  return perturbed;
  // END NoGradGuard
}

ag::Variant ESTrainer::forward(ag::Variant inp, EpisodeHandle const& handle) {
  MetricsContext::Timer forwardTimer(
      metricsContext_, "trainer:forward", kFwdMetricsSubsampling);
  ag::Container model = getGameModel(handle.gameID(), handle.episodeKey());
  torch::NoGradGuard g;
  return forwardUnbatched(inp, model);
}

std::shared_ptr<Evaluator> ESTrainer::makeEvaluator(
    size_t n,
    std::unique_ptr<BaseSampler> sampler) {
  return evaluatorFactory(
      model_,
      std::move(sampler),
      n,
      [this](ag::Variant inp, EpisodeHandle const& handle) {
        torch::NoGradGuard g;
        return this->forwardUnbatched(inp);
      });
}

torch::Tensor ESTrainer::rewardTransform(
    torch::Tensor const& rewards,
    ESTrainer::RewardTransform transform) {
  auto transformed = rewards.clone();
  int64_t size = rewards.size(0);
  switch (transform) {
    case kNone:
      break;
    case kRankTransform: {
      auto indices = std::get<1>(at::sort(transformed, 0));
      auto tmp = torch::range(0, size - 1, 1, at::CPU(at::kFloat))
                     .div_(size - 1.0)
                     .add_(-0.5f);
      transformed.index_copy_(0, indices, tmp);
    } break;
    case kStdNormalize:
      if (size > 0) {
        transformed.div_(transformed.std() + 1e-8);
      }
      break;
    default:
      throw std::runtime_error("Unknown reward transform!");
      break;
  }
  return transformed;
}

void ESTrainer::populateSeedQueue() {
  auto seed = (int64_t)common::Rand::rand();
  seedQueue_.push_back(seed);
  if (antithetic_) {
    seedQueue_.push_back(-seed);
  }
}

void ESTrainer::reset() {
  if (onPolicy_) {
    std::unique_lock<std::mutex> updateLock(updateMutex_);
    Trainer::reset();
    gamesStarted_ = 0;
    newGames_.clear();
    batchBarrier_.notify_all();
    return;
  }
  Trainer::reset();
}

std::shared_ptr<ReplayBufferFrame> ESTrainer::makeFrame(
    ag::Variant /*trainerOutput*/,
    ag::Variant /*state*/,
    float reward) {
  return std::make_shared<RewardBufferFrame>(reward);
}

} // namespace cpid
