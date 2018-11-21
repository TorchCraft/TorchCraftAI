/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#include "prioritymutex.h"
#include "trainer.h"
#include <future>
#include <glog/logging.h>
#include <memory>
#include <mutex>
#include <thread>

#include <torch/torch.h>

namespace cpid {

class AsyncBatcher {
 public:
  /** Construct a batcher
   * @param model is the model used for forwarding
   * @param batchSize is the maximal size of a batch. A forward will occur when
   * that many inputs have been collected. If the consumer waits for more than
   * 200ms for a batch, it will try to forward an incomplete batch
   * @param padValue This is the value used to pad the inputs to the same size
   * @param stipOutput: when true, any negative value in the output tensors will
   * be masked out
   */
  AsyncBatcher(
      ag::Container model,
      int batchsize,
      int padValue = -1,
      bool stripOutput = true,
      double stripValue = -1.);

  /** This function queues up the state for a forward. This function
   * is blocking until the batch is full, then it executes a forward and then
   * returns corresponding to this state.
   * After a given timeout, the forward will be done anyways, even if the batch
   * is not full.
   * WARNING: this function only executes forward WITHOUT gradient
   * (and ignores any torch::grad_guard)
   */
  virtual ag::Variant batchedForward(ag::Variant state);

  virtual ~AsyncBatcher();

  /** Changes the model to be used for forwarding. This operation has
   * high priority, but if a forward is about to be executed with the old model,
   * it may be executed before the effective model switch */
  void setModel(ag::Container newModel);

  /** Get a lock on the model. That allows updating the model ensuring that no
   * forward is being executed */
  void lockModel();
  void unlockModel();

  /** Given an output of the model, retrieve the replies for all the
   * element of the batch.
   * It will mask out any negative element of the reply tensor (that allows to
   * batch replies even though they don't have the same size)
   */
  virtual std::vector<ag::Variant> unBatch(const ag::Variant& out);
  virtual std::vector<ag::Variant>
  unBatch(const ag::Variant& out, bool stripOutput, double stripValue);

  /** Given a vector of queries, create the batch that is going to be
   * passed to the model.
   * This default implementation finds the max size of each tensor accross the
   * batch and resizes all the queries to that size, padding the extra space
   * with -1
   */
  virtual ag::Variant makeBatch(const std::vector<ag::Variant>& queries);

  /**
   * This function should return true when the batch is ready to be consumed
   */
  virtual bool shouldConsume();

 protected:
  void startBatching(int batchSize);

  void consumeThread();

  ag::Container model_;
  bool consumeThreadStarted_ = false;

  int batchSize_;
  int padValue_;
  bool stripOutput_;
  double stripValue_;

  std::condition_variable_any batchReadyCV_;
  std::mutex batchReadyMutex_;

  priority_mutex accessMutex_;

  std::vector<ag::Variant> queries_;
  std::vector<std::shared_ptr<std::promise<ag::Variant>>> replies_;

  std::thread consumeThread_;
  bool shouldStop_;
};

} // namespace cpid
