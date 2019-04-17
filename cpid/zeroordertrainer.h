/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "metrics.h"
#include "trainer.h"
#include <autogradpp/autograd.h>
#include <stack>
#include <torch/torch.h>

namespace cpid {

/**
 * See trainer for output format of these models
 */
class ZOBackpropModel {
 public:
  // To stay true to the paper, the noise should be on the unit  sphere, i.e.
  // randn(size) / norm
  virtual std::vector<torch::Tensor> generateNoise() = 0;
};

/**
 * State, Action taken, reward
 * Taking in an additional action taken allows you to not just take the max
 * action, but use your own inference strategy, for example, if some actions
 * are invalid
 **/
struct OnlineZORBReplayBufferFrame : ReplayBufferFrame {
  OnlineZORBReplayBufferFrame(
      std::vector<torch::Tensor> state,
      std::vector<long> actions,
      double reward)
      : state(state), actions(actions), reward(reward) {}
  std::vector<torch::Tensor> state;
  std::vector<long> actions;
  double reward;
};

/**
 * This is a OnlineZORBTrainer that works with multiple actions per frame.
 * Contract:
 * - Expect to make N distinct actions, with M_{i=1,..,N} possible actions for
 * each
 * - The model inherits from OnlineZORBModel and returns a vector of noises
 * - Input: the replaybuffer frame state
 * - Output: The model should take in a state and generate an array of
 *   [\phi(s, A_1,i), w_1, ind_1, v_1, ..., \phi(s, A_N,i), w_N, ind_N, v_N]
 *   - \phi is a matrix of size [M_i, embed_size] for each action i
 *   - w has size [embed_size]
 *   - ind is an index to the noise vector generated.
 *   - v is a critic for variance reduction. Optional, (use torch::Tensor())
 * Because of the particular way this trainer works, the sampler (inference
 * procedureg) is part of the forward function.
 * - The inference procedure the trainer provides through trainer->forward is
 *      argmax_i \phi * (w + \delta * noise[ind])
 * - The critic is used in training, by doing (return - critic), where the
 *   critic is trained on the return, like in actor critic. This works because
 *   G = E_u [ f(x + d u) u ]
 *   G = E_u [ f(x + d u) u ] - E_u [ v u ] (u is gaussian this so this 0)
 *   G = E_u [ [ f(x + d u) - v ] u ]
 *
 * The trainer->forward function will return you
 *   [action_i, action_scores_i, ...]
 *
 * TODO ON MULTIPLE NODES, THIS IS UNTESTED AND DOESN'T WORK
 * even though the logic is mostly there
 */
class OnlineZORBTrainer : public Trainer {
  int64_t episodes_ = 0;
  std::mutex updateLock_;
  std::mutex noiseLock_;
  std::unordered_map<
      GameUID,
      std::unordered_map<EpisodeKey, std::vector<torch::Tensor>>>
      noiseStash_;
  std::vector<torch::Tensor> lastNoise_;

  std::atomic<int> nEpisodes_;

 public:
  void stepEpisode(GameUID const&, EpisodeKey const&, ReplayBuffer::Episode&)
      override;
  bool update() override;
  EpisodeHandle startEpisode() override;
  ag::Variant forward(ag::Variant inp, EpisodeHandle const&) override;

  OnlineZORBTrainer(ag::Container model, ag::Optimizer optim);
  // Contract: TrainerOutput is a map with a key "action" containing the taken
  // action
  virtual std::shared_ptr<ReplayBufferFrame> makeFrame(
      ag::Variant trainerOutput,
      ag::Variant state,
      float reward) override;

  // Set this to non-0 to not use a critic
  TORCH_ARG(float, valueLambda) = 0;
  TORCH_ARG(float, delta) = 1e-3;
  TORCH_ARG(int, batchSize) = 10;
  // Use antithetic sampling for the noise
  TORCH_ARG(bool, antithetic) = false;
};

} // namespace cpid
