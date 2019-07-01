/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "checkpointer.h"
#include "prioritymutex.h"
#include "threadpool.h"
#include "trainer.h"
#include <mutex>
#include <autogradpp/autograd.h>

DECLARE_int32(recurrent_burnin);

namespace cpid {

struct SyncFrame : ReplayBufferFrame {
  ag::Variant state;
  ag::Variant forwarded_state;

  /// Take a list of SingleFrames, and batch them as a single BatchedFrame
  virtual std::shared_ptr<SyncFrame> batch(
      const std::vector<std::shared_ptr<SyncFrame>>& list,
      std::unique_ptr<AsyncBatcher>& batcher) {
    throw std::runtime_error("Batching not implemented");
    return std::make_shared<SyncFrame>();
  }

  /// moves all the tensor to the given device
  virtual void toDevice(at::Device) {
    throw std::runtime_error("toDevice not implemented");
  }
};

/// This is the default batched frame to be used with sync trainers
struct BatchedFrame : SyncFrame {
  torch::Tensor reward; // size batchSize x 1
  torch::Tensor action; // size batchSize x num_actions

  /// Probability of action according to the policy that was used to obtain this
  /// frame (used only in PG algorithms)
  torch::Tensor pAction; // size batchSize x 1

  /// moves all the tensor to the given device
  void toDevice(at::Device) override;
};

struct SingleFrame : SyncFrame {
  float reward;
  torch::Tensor action; // size num_actions

  /// Probability of action according to the policy that was used to obtain this
  /// frame (used only in PG algorithms)
  torch::Tensor pAction; // size num_actions

  /// Take a list of SingleFrames, and batch them as a single BatchedFrame
  std::shared_ptr<SyncFrame> batch(
      const std::vector<std::shared_ptr<SyncFrame>>& list,
      std::unique_ptr<AsyncBatcher>& batcher) override;
};

/**
 * This class provides a fully synchronous training interface. It constructs
 * square batches of \param{returnsLength} consecutive frames for
 * \param{trainerBatchSize} different games, and once such a batch is ready, it
 * does a full update on it, blocking the playing threads in the meantime. Note
 * that in general, you can have more playing threads than the batch size of the
 * trainer. This can be useful to make sure the experience generation is not the
 * bottleneck. However, it is advised to avoid doing that if you want a fully
 * on-policy algorithm.

 * You can control how often the behaviour policy is updated. If
 \param{updateFreq} is 1, then it will be updated at each update, making the
 * algorithm fully on-policy. However, for stability, it might sometimes be
 * necessary, to keep a behaviour policy fixed for a while, and update it only
 * once in a while.

 * You can control how the samples should be reused. If
 * \param{overlappingUpdates} is false, then once an update has been done on T
 * consecutive frames, then they all are discarded, and the next update will be
 * done on the next T frames. Otherwise, only the oldest one is discarded, and
 * we do an update with the T-1 other frames, plus a newly generated one.
 *
 * If \param{forceOnPolicy}, then the trainer will do its best to try to be
 * onpolicy. In practice, this means that once an update is made, the frame
 * buffers are flushed, since any data they contain is bound to be off-policy,
 * since the policy just changed. Note that this makes sense only if updateFreq
 * is 1
 *
 * If \param{gpuMemoryEfficient} is true, then we store as little as possible on
 * the gpu. In practice, the data will be uploaded to the gpu at the last
 * possible moment, and freed immediately. Note that in this case, the forwards
 * of the frame will not be batched accross the temporal dimension. Overall,
 * setting this to true, has significant performance implications, do it only if
 * you are really memory bound.
 *
 * If \param{reduceGradients} is true, gradients will be averaged accross all
 * the nodes. Set it to false if you want the nodes to hold different versions
 * of the model
 *
 * \param{maxGradientNorm}: if this parameter is positive, then the gradients
 * will be scaled down so that the inf norm is no greater than the one provided
 */
class SyncTrainer : public Trainer {
  using hires_clock = std::chrono::steady_clock;

 public:
  SyncTrainer(
      ag::Container model,
      ag::Optimizer optim,
      std::unique_ptr<BaseSampler> sampler,
      std::unique_ptr<AsyncBatcher> batcher,
      int returnsLength,
      int updateFreq,
      int trainerBatchSize,
      bool overlappingUpdates,
      bool forceOnpolicy,
      bool gpuMemoryEfficient,
      bool reduceGradients = true,
      float maxGradientNorm = -1);

  /// Send a frame to the trainer. This will be blocking if an update is
  /// occuring
  void step(
      EpisodeHandle const& handle,
      std::shared_ptr<ReplayBufferFrame> value,
      bool isDone = false) override;

  bool update() override;

  void setTrain(bool train);

  std::shared_ptr<ReplayBufferFrame> makeFrame(
      ag::Variant trainerOutput,
      ag::Variant state,
      float reward) override;

  // Return value is undefined if the episode is no longer active at return
  // when running on-policy
  ag::Variant forward(ag::Variant inp, EpisodeHandle const&) override;

  void forceStopEpisode(EpisodeHandle const& handle) override;

  int getUpdateCount() const {
    return updateCount_;
  }

  /// Constructs an evaluator
  std::shared_ptr<Evaluator> makeEvaluator(
      size_t n,
      std::unique_ptr<BaseSampler> sampler) override;

  virtual void reset() override;
  template <class Archive>
  void save(Archive& ar) const;
  template <class Archive>
  void load(Archive& ar);


 protected:
  using Frame = std::pair<std::shared_ptr<SyncFrame>, bool>;
  struct Buffer {
    std::deque<Frame> frames;
    double cumReward = 0.0;
    bool isDone = false;
    int lastUpdateNum = 0;
    EpisodeKey currentOwner; // For internal sanity checks
  };

  /// Create an empty frame of the type expected by the trainer
  virtual std::shared_ptr<SyncFrame> makeEmptyFrame();

  /// This is the update function that needs to be overriden. It takes a list of
  /// batched frames of size returnsLength, and a byte matrix of size
  /// (returnsLength_ x trainerBatchSize_) indicating which frames are terminal
  virtual void doUpdate(
      const std::vector<std::shared_ptr<SyncFrame>>& seq,
      torch::Tensor terminal) = 0;

  /// Create a square batch using returnsLength frames from each of the
  /// selectedGames. The resulting frames are pushed in seq, and terminal is a
  /// byte tensor indicating which frames are terminal states.
  void createBatch(
      const std::vector<size_t>& selectedBuffers,
      std::vector<std::shared_ptr<SyncFrame>>& seq,
      torch::Tensor& terminal);

  /// Given a sequence of consecutive frames, compute the forward for all the
  /// frames. This will fill the state_forwarded field of the frames
  void computeAllForward(
      const std::vector<std::shared_ptr<SyncFrame>>& seq,
      int batchSize,
      const torch::Tensor& notTerminal = torch::Tensor());

  void computeAllForwardModel(
      const ag::Container model,
      const std::vector<std::shared_ptr<SyncFrame>>& seq,
      int batchSize,
      const torch::Tensor& notTerminal = torch::Tensor());

  /// Do an optim step, reducing gradients over network
  void doOptimStep();

  int returnsLength_, updateFreq_;
  int updateCount_ = 0;
  int trainerBatchSize_ = 64;
  bool overlappingUpdates_;
  bool forceOnPolicy_;
  bool gpuMemoryEfficient_;
  bool reduceGradients_;
  float maxGradientNorm_ = -1;

  // Mutexes should be locked in the order in which they appear here:
  mutable priority_mutex stepMutex_;

 private:
  /** Retrieve the 'buffers_' index for the corresponding handle
   * If no buffer is assigned yet, will reuse a finished buffer if possible
   * or create a new one.
   * This is to be able to create batches of size [batch_size, returnsLength]
   * even if some episodes are shorter.
   *
   * UNSAFE: Expects stepMutex_ lock to be acquired by caller
   **/
  size_t getBufferForHandleUNSAFE(EpisodeHandle const& handle);

  ThreadPool threads_;

  // we store the gameUID of the threads ready to update, along with the
  // timestamp at which they started waiting
  std::unordered_map<size_t, hires_clock::time_point> readyToUpdate_;
  std::condition_variable_any batchCV_;

  std::condition_variable_any forwardCV_;
  std::shared_mutex forwardMutex_;

  std::unordered_map<GameUID, size_t> gamesToBuffers_;
  std::vector<std::unique_ptr<Buffer>> buffers_;

  bool train_ = true;
};

template <class Archive>
void SyncTrainer::save(Archive& ar) const {
  ar(CEREAL_NVP(*model_));
  ar(CEREAL_NVP(optim_));
  ar(CEREAL_NVP(updateCount_));
}

template <class Archive>
void SyncTrainer::load(Archive& ar) {
  ar(CEREAL_NVP(*model_));
  auto o = std::static_pointer_cast<torch::optim::Adam>(optim_);
  VLOG(0) << "lr before load " << o->options.learning_rate();
  auto options = o->options;
  ar(CEREAL_NVP(optim_));
  o = std::static_pointer_cast<torch::optim::Adam>(optim_);
  o->options = options;
  VLOG(0) << "lr after load " << o->options.learning_rate();
  optim_->add_parameters(model_->parameters());
  ar(CEREAL_NVP(updateCount_));
}

} // namespace cpid
