/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#include "prioritymutex.h"
#include <future>
#include <glog/logging.h>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>

#include <autogradpp/autograd.h>
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
  std::unique_lock<std::shared_mutex> lockModel();

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
  virtual ag::Variant makeBatch(
      const std::vector<ag::Variant>& queries,
      double padValue);

  /**
   * This function should return true when the batch is ready to be consumed
   */
  virtual bool shouldConsume();

 protected:
  void startBatching(int batchSize);
  void stopBatching();

  void consumeThread();

  ag::Container model_;
  bool consumeThreadStarted_ = false;

  int batchSize_;
  int padValue_;
  bool stripOutput_;
  double stripValue_;

  std::condition_variable_any batchReadyCV_;
  std::mutex batchReadyMutex_;

  // Mutexes have to be acquires in this order:
  priority_mutex accessMutex_;
  std::shared_mutex modelMutex_;

  std::vector<ag::Variant> queries_;
  std::vector<std::shared_ptr<std::promise<ag::Variant>>> replies_;

  std::thread consumeThread_;
  std::atomic<bool> shouldStop_{false};
};

/** A batcher that can operate on (already) batched data
 * Should be used when features have a variable batch dimension, for
 * instance the number of units controlled.
 * More specifically, tensors with sizes [b1, ft], [b2, ft] ..., are
 * batched into a Tensor of size [b1 + b2 + ..., ft].
 *
 * On the contrary to AsyncBatcher, SubBatchAsyncBatcher expects
 * input tensors shape to differ on the first dimension only, and will
 * not pad input tensors, unless explicitely autorized with 'allowPadding'.
 */
class SubBatchAsyncBatcher : public AsyncBatcher {
 public:
  static constexpr const char* kBatchInfoKey = "batch_info";

  SubBatchAsyncBatcher(int batchSize, ag::Container model = nullptr);
  ~SubBatchAsyncBatcher();

  std::vector<ag::Variant>
  unBatch(const ag::Variant& out, bool stripOutput, double stripValue) override;

  ag::Variant makeBatch(const std::vector<ag::Variant>& queries, double)
      override;

  void allowPadding(bool allowPadding) {
    allowPadding_ = allowPadding;
  }

  torch::Tensor makeBatchTensors(
      std::vector<torch::Tensor> const& tensors,
      double padValue);

 protected:
  bool allowPadding_ = false;

 public:
  static std::vector<torch::Tensor> unBatchTensor(
      const torch::Tensor& out,
      std::vector<int64_t> const& batchSizes);

  static std::vector<long> findBatchInfo(
      ag::Variant const& batchInfoVar,
      std::string const& variableName);

  static std::vector<torch::Tensor> forEachSubbatch(
      ag::Variant const& input,
      std::string const& inputName,
      torch::Tensor batchedInput,
      std::function<torch::Tensor(torch::Tensor)> do_fn = [](torch::Tensor t) {
        return t;
      });
};

} // namespace cpid
