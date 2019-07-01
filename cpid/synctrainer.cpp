/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "synctrainer.h"
#include "batcher.h"
#include "checkpointer.h"
#include "distributed.h"
#include "sampler.h"

#include "common/autograd.h"
#include "common/language.h"
#include <fmt/format.h>

namespace {
const std::string kValueKey = "V";
const std::string kQKey = "Q";
const std::string kPiKey = "Pi";
const std::string kSigmaKey = "std";
const std::string kActionQKey = "actionQ";
const std::string kActionKey = "action";
const std::string kPActionKey = "pAction";
const std::string kHiddenKey = "hidden";
const std::string kStateKey = "state";

} // namespace

DEFINE_int32(
    recurrent_burnin,
    0,
    "use this many steps to freshen recurrent state before doing loss");

DEFINE_string(
    clip_method,
    "max",
    "Which method of gradient clipping to use: 'max','l2'");
namespace cpid {

void BatchedFrame::toDevice(at::Device device) {
  auto cudaify = [device](torch::Tensor t) { return t.to(device); };
  state = common::applyTransform(state, cudaify);
  // forwarded_state = common::applyTransform(forwarded_state, cudaify);
  reward = reward.to(device);
  action = action.to(device);
  if (pAction.defined()) {
    pAction = pAction.to(device);
  }
}

std::shared_ptr<SyncFrame> SingleFrame::batch(
    const std::vector<std::shared_ptr<SyncFrame>>& list,
    std::unique_ptr<AsyncBatcher>& batcher) {
  torch::NoGradGuard g_;
  if (list.empty())
    return nullptr;

  size_t batchSize = list.size();
  auto batched = std::make_shared<BatchedFrame>();
  batched->reward = torch::zeros({int64_t(batchSize)});
  auto rewAcc = batched->reward.accessor<float, 1>();
  std::vector<ag::Variant> actions, pActions;
  std::vector<ag::Variant> states;
  actions.reserve(batchSize);
  states.reserve(batchSize);

  for (size_t i = 0; i < batchSize; ++i) {
    auto curFrame = std::dynamic_pointer_cast<SingleFrame>(list[i]);
    rewAcc[i] = curFrame->reward;
    if (curFrame->pAction.defined()) {
      pActions.push_back(curFrame->pAction);
    }
    actions.push_back(curFrame->action);
    states.push_back(curFrame->state);
  }

  batched->action = batcher->makeBatch(actions, -42).get();
  if (pActions.size() != 0) {
    batched->pAction = batcher->makeBatch(pActions, -42).get();
    batched->pAction.set_requires_grad(false);
  }
  batched->state = batcher->makeBatch(states);
  batched->action.set_requires_grad(false);
  batched->reward.set_requires_grad(false);
  return batched;
}

SyncTrainer::SyncTrainer(
    ag::Container model,
    ag::Optimizer optim,
    std::unique_ptr<BaseSampler> sampler,
    std::unique_ptr<AsyncBatcher> batcher,
    int returnsLength,
    int updateFreq,
    int trainerBatchSize,
    bool overlappingUpdates,
    bool forceOnPolicy,
    bool gpuMemoryEfficient,
    bool reduceGradients,
    float maxGradientNorm)
    : Trainer(model, optim, std::move(sampler), std::move(batcher)),
      returnsLength_(returnsLength),
      updateFreq_(updateFreq),
      trainerBatchSize_(trainerBatchSize),
      overlappingUpdates_(overlappingUpdates),
      forceOnPolicy_(forceOnPolicy),
      gpuMemoryEfficient_(gpuMemoryEfficient),
      reduceGradients_(reduceGradients),
      maxGradientNorm_(maxGradientNorm),
      threads_(10),
      updateCount_(0),
      stepMutex_(1) {
  if (updateFreq_ == 1) {
    batcher_->setModel(model_);
  } else {
    batcher_->setModel(ag::clone(model_));
  }
  if (returnsLength < 2) {
    throw std::runtime_error("SyncTrainer: the return size must be at least 2");
  }
}

void SyncTrainer::step(
    EpisodeHandle const& handle,
    std::shared_ptr<ReplayBufferFrame> value,
    bool isDone) {
  priority_lock lk(stepMutex_, 0); // low priority lock
  lk.lock();
  auto frame = std::static_pointer_cast<SingleFrame>(value);
  if (!train_ || !isActive(handle)) {
    return;
  }
  auto key = getBufferForHandleUNSAFE(handle);
  auto& buf = buffers_[key];
  buf->cumReward += frame->reward;
  buf->frames.emplace_back(std::move(frame), isDone);
  if (isDone) {
    metricsContext_->pushEvent("cumulated_reward", buf->cumReward);
    buf->cumReward = 0;
  }
  buf->isDone = isDone;
  if (isDone) {
    Trainer::forceStopEpisode(handle);
  }
  if ((int)buf->frames.size() >= returnsLength_) {
    std::unique_lock lk2(forwardMutex_);
    readyToUpdate_.insert({key, hires_clock::now()});
  }
  lk.unlock();
  batchCV_.notify_all();
}

std::shared_ptr<ReplayBufferFrame> SyncTrainer::makeFrame(
    ag::Variant trainerOutput,
    ag::Variant state,
    float reward) {
  if (!trainerOutput.isDict()) {
    throw std::runtime_error("SyncTrainer: \"trainerOutput\" should be a Dict");
  }
  torch::NoGradGuard g_;
  auto back = model_->options().device();
  if (gpuMemoryEfficient_) {
    back = torch::kCPU;
  }
  auto frame = std::make_shared<SingleFrame>();
  frame->action = trainerOutput.getDict().at(kActionKey).get().to(back);
  frame->state = std::move(state);

  auto toBack = [back](torch::Tensor t) { return t.to(back); };
  frame->state = common::applyTransform(frame->state, toBack);

  if (trainerOutput.getDict().count(kPActionKey) > 0) {
    auto pAction = trainerOutput.getDict().at(kPActionKey);
    if (pAction.isTensor()) {
      frame->pAction = pAction.get().to(back);
    }
  }
  frame->reward = reward;
  return frame;
}

std::shared_ptr<SyncFrame> SyncTrainer::makeEmptyFrame() {
  return std::make_shared<SingleFrame>();
}

void SyncTrainer::createBatch(
    const std::vector<size_t>& selectedBuffers,
    std::vector<std::shared_ptr<SyncFrame>>& seq,
    torch::Tensor& terminal) {
  MetricsContext::Timer batchTimer(metricsContext_, "trainer:batch_creation");

  seq.resize(returnsLength_);

  std::vector<std::future<std::shared_ptr<SyncFrame>>> futures;
  auto combinedFrame = makeEmptyFrame();
  auto batchOneFrame =
      [&](std::vector<std::shared_ptr<SyncFrame>> currentFrame) {
        return combinedFrame->batch(currentFrame, batcher_);
      };
  for (int i = 0; i < returnsLength_; ++i) {
    std::vector<std::shared_ptr<SyncFrame>> currentFrame;
    for (size_t j = 0; j < selectedBuffers.size(); ++j) {
      auto& f = buffers_[selectedBuffers[j]]->frames[i];
      currentFrame.push_back(f.first);
      terminal[i][j] = f.second;
    }
    futures.emplace_back(
        threads_.enqueue(batchOneFrame, std::move(currentFrame)));
  }
  for (int i = 0; i < returnsLength_; ++i) {
    futures[i].wait();
    seq[i] = futures[i].get();
    if (model_->options().device().is_cuda()) {
      seq[i]->toDevice(model_->options().device());
    }
  }
}

bool SyncTrainer::update() {
  priority_lock lk(stepMutex_, 1); // high priority lock
  lk.lock();

  auto shouldDoUpdate = [this]() {
    std::shared_lock lk2(forwardMutex_);
    if ((int)readyToUpdate_.size() >= trainerBatchSize_) {
      return true;
    }
    if ((int)readyToUpdate_.size() > 0) {
      hires_clock::time_point now = hires_clock::now();
      hires_clock::time_point oldest = std::accumulate(
          readyToUpdate_.begin(),
          readyToUpdate_.end(),
          now,
          [](hires_clock::time_point t, const auto& it) {
            return std::min(t, it.second);
          });
      std::chrono::duration<double, std::milli> dur = now - oldest;
      if (dur.count() / 1000. > 5)
        return true;
    }
    return false;
  };

  while (
      !batchCV_.wait_for(lk, std::chrono::milliseconds(2000), shouldDoUpdate)) {
  }

  std::vector<size_t> selectedBuffers;
  std::vector<std::shared_ptr<SyncFrame>> seq;
  {
    std::shared_lock lk2(forwardMutex_);
    if (readyToUpdate_.empty()) {
      return false;
    }
    for (const auto& g : readyToUpdate_) {
      selectedBuffers.push_back(g.first);
      LOG_IF(FATAL, buffers_[g.first]->frames.size() < unsigned(returnsLength_))
          << "Wrong buffer size: buffer #" << g.first << " (owner \""
          << buffers_[g.first]->currentOwner << "\")"
          << " has only " << buffers_[g.first]->frames.size()
          << " frames, but selected, and returnsLength=" << returnsLength_;
      if ((int)selectedBuffers.size() == trainerBatchSize_)
        break;
    }
  }

  updateCount_++;
  int actualBatchSize = selectedBuffers.size();
  metricsContext_->incCounter("sampleCount", actualBatchSize);

  auto terminal = torch::zeros({returnsLength_, actualBatchSize}, at::kByte);

  createBatch(selectedBuffers, seq, terminal);

  {
    std::unique_lock<std::shared_mutex> modelLock;
    if (updateFreq_ == 1) {
      modelLock = batcher_->lockModel();
    }

    {
      MetricsContext::Timer batchTimer(metricsContext_, "trainer:doUpdate");
      doUpdate(seq, terminal);
    }

    if (updateFreq_ != 1) {
      batcher_->setModel(ag::clone(model_));
    }
  }

  // now we clean up the frameBuffers_
  {
    std::unique_lock lk2(forwardMutex_);
    if (forceOnPolicy_) {
      for (auto& b : buffers_) {
        b->frames.clear();
      }
      readyToUpdate_.clear();
    } else {
      size_t to_delete = overlappingUpdates_ ? 1 : returnsLength_ - 1;
      for (const auto& g : selectedBuffers) {
        auto& buffer = buffers_[g]->frames;
        buffer.erase(buffer.begin(), buffer.begin() + to_delete);
        if ((int)buffer.size() < returnsLength_) {
          readyToUpdate_.erase(g);
        }
      }
    }
    for (const auto& g : selectedBuffers) {
      buffers_[g]->lastUpdateNum = updateCount_;
    }
  }

  lk.unlock();
  forwardCV_.notify_all();
  batchCV_.notify_all();
  return true;
}

void SyncTrainer::setTrain(bool train) {
  if (train) {
    model_->train();
  } else {
    model_->eval();
  }
  train_ = train;
}

void SyncTrainer::computeAllForward(
    const std::vector<std::shared_ptr<SyncFrame>>& seq,
    int batchSize,
    const torch::Tensor& notTerminal) {
  computeAllForwardModel(model_, seq, batchSize, notTerminal);
}

void SyncTrainer::computeAllForwardModel(
    const ag::Container model,
    const std::vector<std::shared_ptr<SyncFrame>>& seq,
    int batchSize,
    const torch::Tensor& notTerminal) {
  MetricsContext::Timer forwardTimer(
      metricsContext_, "trainer:computeAllForward");
  if (gpuMemoryEfficient_) {
    bool isCuda = model_->options().device().is_cuda();
    auto cudaify = [](torch::Tensor t) { return t.to(torch::kCUDA); };
    for (size_t i = 0; i < seq.size(); ++i) {
      ag::Variant input = seq[i]->state;
      if (i > 0 && input.getDict().count(kStateKey) > 0 &&
          input.getDict()[kStateKey].getDict().count(kHiddenKey) > 0) {
        input.getDict()[kStateKey][kHiddenKey] =
            seq[i - 1]->forwarded_state[kHiddenKey];
        // cut hidden states at terminal
        torch::Tensor mask =
            notTerminal[i]
                .view({batchSize, 1, 1, 1})
                .expand_as(input.getDict()[kStateKey][kHiddenKey]);
        input.getDict()[kStateKey][kHiddenKey] =
            input.getDict()[kStateKey][kHiddenKey] * mask;
        if (FLAGS_recurrent_burnin > 0 && i < FLAGS_recurrent_burnin) {
          input.getDict()[kStateKey][kHiddenKey] =
              input.getDict()[kStateKey][kHiddenKey].detach();
        }
      }
      VLOG(3) << "hidden (stale if offpolicy) "
              << common::tensorStats(
                     seq[i]->state.getDict()[kStateKey][kHiddenKey]);
      VLOG(3) << "hidden used "
              << common::tensorStats(input.getDict()[kStateKey][kHiddenKey]);
      if (isCuda) {
        input = common::applyTransform(input, cudaify);
      }
      seq[i]->forwarded_state = model->forward(std::move(input));
    }
  } else {
    // To save computation, we can do one forward for all element of the seq at
    // once
    std::vector<ag::Variant> allStates;
    for (const auto& frame : seq) {
      allStates.emplace_back(frame->state);
    }
    ag::Variant batch = common::makeBatchVariant(allStates);
    auto collapseFirstDim = [](torch::Tensor t) {
      std::vector<int64_t> sizes(t.sizes().vec()),
          newSizes(t.sizes().size() - 1);
      newSizes[0] = sizes[0] * sizes[1];
      std::copy(sizes.begin() + 2, sizes.end(), newSizes.begin() + 1);
      return t.view(newSizes);
    };
    batch = common::applyTransform(std::move(batch), collapseFirstDim);
    ag::Variant composedResult = model_->forward(std::move(batch));
    std::vector<ag::Variant> result =
        common::unBatchVariant(composedResult, batchSize, false);

    for (size_t i = 0; i < seq.size(); ++i) {
      seq[i]->forwarded_state = std::move(result[i]);
    }
  }
}

void SyncTrainer::doOptimStep() {
  namespace dist = distributed;
  if (reduceGradients_) {
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
  }

  // compute the inf norm
  int64_t numGradClips = 0;
  if (FLAGS_clip_method == "l2") {
    std::vector<torch::Tensor> gradList;
    for (auto& p : model_->parameters()) {
      gradList.emplace_back(p.grad().detach().reshape({-1}));
    }
    auto flatGrad = torch::cat(gradList, 0);
    auto gNorm2 = flatGrad.norm(2).item<float>();
    auto scaleFactor = gNorm2 > maxGradientNorm_ && maxGradientNorm_ > 0
        ? maxGradientNorm_ / gNorm2
        : 1.;
    if (scaleFactor != 1.) {
      for (auto& v : model_->parameters()) {
        v.grad().detach().mul_(scaleFactor);
      }
    }
  }

  float norm = 0;
  for (auto& var : model_->parameters()) {
    if (!var.grad().defined()) {
      continue;
    }
    norm = std::max(norm, torch::max(torch::abs(var.grad())).item<float>());
  }
  if (getUpdateCount() % 10 == 0) {
    metricsContext_->pushEvent("grad_inf_norm", norm);
  }
  if (FLAGS_clip_method == "max" && maxGradientNorm_ > 0) {
    float clip_coef = maxGradientNorm_ / (norm + 1.e-5);
    if (clip_coef < 1) {
      for (auto& var : model_->parameters()) {
        if (!var.grad().defined()) {
          continue;
        }
        var.grad().mul_(clip_coef);
      }
    }
  }
  optim_->step();
  optim_->zero_grad();
}

ag::Variant SyncTrainer::forward(ag::Variant inp, EpisodeHandle const& handle) {
  if (forceOnPolicy_) {
    priority_lock lk(stepMutex_, 0);
    lk.lock();
    if (!isActive(handle)) {
      return ag::VariantDict{};
    }
    auto bufferKey = getBufferForHandleUNSAFE(handle);
    lk.unlock();

    std::shared_lock forwardLock(forwardMutex_);
    // This can wait indefinitively when we finish training
    // Solution: Call trainer->reset()
    auto shouldDoForward = [this, &bufferKey, &handle]() {
      return !isActive(handle) || readyToUpdate_.count(bufferKey) == 0;
    };
    forwardCV_.wait(forwardLock, shouldDoForward);
    if (!isActive(handle)) {
      return ag::VariantDict{};
    }
  }
  return Trainer::forward(inp, handle);
}

void SyncTrainer::forceStopEpisode(EpisodeHandle const& handle) {
  priority_lock lk(stepMutex_, 0); // low priority lock
  lk.lock();
  if (!isActive(handle)) {
    return;
  }
  Trainer::forceStopEpisode(handle);
  auto key = getBufferForHandleUNSAFE(handle);
  auto& buf = buffers_[key];

  std::unique_lock forwardLock(forwardMutex_);
  buf->frames.clear();
  buf->cumReward = 0.0;
  buf->isDone = true;
  buf->lastUpdateNum = updateCount_;
  readyToUpdate_.erase(key);
}

std::shared_ptr<Evaluator> SyncTrainer::makeEvaluator(
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

size_t SyncTrainer::getBufferForHandleUNSAFE(EpisodeHandle const& handle) {
  auto key = handle.gameID();
  auto it = gamesToBuffers_.find(key);
  if (it != gamesToBuffers_.end()) {
    if (buffers_[it->second]->currentOwner != key) {
      throw std::runtime_error(fmt::format(
          "handle \"{}\" wants to access his buffer with index {}, but it no "
          "longer owns it (current owner is \"{}\")",
          key,
          it->second,
          buffers_[it->second]->currentOwner));
    }
    return it->second;
  }
  auto idx = -1;
  auto bestValue = 0;
  // Take the smallest 'lastUpdateNum' among free buffers
  // to prevent buffers from being too old
  for (auto i = 0U; i < buffers_.size(); ++i) {
    if (!buffers_[i]->isDone) {
      continue;
    }
    if (idx < 0 || buffers_[i]->lastUpdateNum < bestValue) {
      idx = i;
      bestValue = buffers_[i]->lastUpdateNum;
    }
  }
  if (idx < 0) {
    idx = buffers_.size();
    buffers_.push_back(std::make_unique<Buffer>());
  }
  buffers_[idx]->currentOwner = key;
  buffers_[idx]->isDone = false;
  gamesToBuffers_[key] = idx;
  return idx;
}

void SyncTrainer::reset() {
  {
    priority_lock lk(stepMutex_, 1);
    lk.lock();
    std::unique_lock lk2(forwardMutex_);
    for (auto& buf : buffers_) {
      buf->cumReward = 0.0f;
      buf->frames.clear();
      buf->isDone = true;
      buf->currentOwner.clear();
    }
    readyToUpdate_.clear();
    Trainer::reset();
  }
  forwardCV_.notify_all();
}

} // namespace cpid
