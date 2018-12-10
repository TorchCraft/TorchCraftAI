/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "batcher.h"
#include "common/autograd.h"

namespace cpid {

AsyncBatcher::AsyncBatcher(
    ag::Container model,
    int batchSize,
    int padValue,
    bool stripOutput,
    double stripValue)
    : model_(model),
      consumeThreadStarted_(false),
      padValue_(padValue),
      stripOutput_(stripOutput),
      stripValue_(stripValue),
      accessMutex_(3) {
  shouldStop_ = false;
  startBatching(batchSize);
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

AsyncBatcher::~AsyncBatcher() {
  if (consumeThreadStarted_) {
    {
      std::lock_guard<std::mutex> lk(batchReadyMutex_);
      shouldStop_ = true;
    }
    batchReadyCV_.notify_all();
    consumeThread_.join();
  }
}

void AsyncBatcher::startBatching(int batchSize) {
  if (consumeThreadStarted_) {
    throw std::runtime_error("Consumer thread already started");
  }
  if (batchSize < 1) {
    throw std::runtime_error("Batchsize must be at least 1");
  }
  batchSize_ = batchSize;
  consumeThread_ = std::thread(&AsyncBatcher::consumeThread, this);
}

ag::Variant AsyncBatcher::batchedForward(ag::Variant state) {
  if (!consumeThreadStarted_) {
    throw std::runtime_error(
        "Can't batch forwards if the consume thread is not started. Call "
        "startBatching() first");
  }

  auto myPromise = std::make_shared<std::promise<ag::Variant>>();
  {
    // will lock with lowest priority
    std::unique_lock<priority_mutex> accessLock(accessMutex_);
    while (true) {
      if (!shouldConsume()) // if the batch is not full, we can proceed with
        // insertion
        break;
      // if the batch is full, we shouldn't queue more items
      // give the consumer thread a chance to get the lock
      accessLock.unlock();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      accessLock.lock();
    }

    replies_.push_back(myPromise);
    queries_.emplace_back(std::move(state));

    if (replies_.size() != queries_.size()) {
      LOG(FATAL) << "Size mismatch between replies (" << replies_.size()
                 << ") and queries(" << queries_.size() << ")";
    }

    if (shouldConsume()) {
      // when we hit the last element of the batch, we can send a notification
      batchReadyCV_.notify_all();
    }
  }

  ag::Variant reply = myPromise->get_future().get();
  return reply;
}

bool AsyncBatcher::shouldConsume() {
  return int(queries_.size()) >= batchSize_;
}

void AsyncBatcher::consumeThread() {
  consumeThreadStarted_ = true;
  while (true) {
    // create the lock, but doesn't actually lock
    priority_lock accessLock(accessMutex_, 1);
    accessLock.lock();

    // wait for the batch to be ready
    if (!batchReadyCV_.wait_for(
            accessLock, std::chrono::milliseconds(200), [this]() {
              return shouldStop_ || shouldConsume();
            })) {
      if (queries_.size() > 0 && accessLock.try_lock(1)) {
        // if we manage to get a insertion lock, we carry on with this
        // incomplete forward
        VLOG(3) << "Doing incomplete forward";
      } else {
        // otherwise, it's better to wait for a full batch
        continue;
      }
    } else if (shouldStop_) {
      return;
    }

    ag::Variant input = this->makeBatch(queries_);
    ag::Variant out;
    {
      torch::NoGradGuard g_;
      out = model_->forward(input);
    }

    auto replies_values = this->unBatch(out);

    if (replies_.size() != replies_values.size()) {
      LOG(FATAL) << "The batch size of the reply (" << replies_values.size()
                 << ") doesn't match the expected batch size ("
                 << replies_.size() << ")";
    }

    for (size_t i = 0; i < replies_.size(); ++i) {
      replies_[i]->set_value(std::move(replies_values[i]));
    }

    // batch consumed, we can mark this one as done
    replies_.clear();
    queries_.clear();

    accessLock.unlock();
  }
}

std::vector<ag::Variant> AsyncBatcher::unBatch(const ag::Variant& out) {
  return unBatch(out, stripOutput_, stripValue_);
}

std::vector<ag::Variant> AsyncBatcher::unBatch(
    const ag::Variant& o,
    bool stripOutput,
    double stripValue) {
  return common::unBatchVariant(o, 1, stripOutput, stripValue);
}

ag::Variant AsyncBatcher::makeBatch(const std::vector<ag::Variant>& queries) {
  return common::makeBatchVariant(queries, padValue_);
}

void AsyncBatcher::setModel(ag::Container newModel) {
  priority_lock lock(accessMutex_);
  lock.lock(2);
  model_ = newModel;
}

void AsyncBatcher::lockModel() {
  accessMutex_.lock(2);
}

void AsyncBatcher::unlockModel() {
  accessMutex_.unlock();
}

} // namespace cpid
