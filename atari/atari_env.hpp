/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#pragma once
#include <autogradpp/autograd.h>

#include <ale_interface.hpp>
#include <deque>
#include <memory>
#include <mutex>

class AtariWrapper {
 public:
  AtariWrapper() = default;
  AtariWrapper(AtariWrapper const&) = delete;
  AtariWrapper(AtariWrapper&&) = default;
  AtariWrapper& operator=(AtariWrapper const&) = delete;
  AtariWrapper& operator=(AtariWrapper&&) = default;

  AtariWrapper&& make();
  void reset();

  torch::Tensor getState();

  double act(int action);

  size_t getNumActions() {
    return ale_->getMinimalActionSet().size();
  }

  void reset_game();

  bool game_over() {
    return ale_->game_over();
  }

  int height() const {
    return height_;
  }
  int width() const {
    return width_;
  }

  TORCH_ARG(int, seed) = 42;
  TORCH_ARG(int, frame_skip) = 4; // Action frequency
  TORCH_ARG(int, stacked_observations) =
      4; // Number of past frames used as input
  TORCH_ARG(std::string, ale_rom) = "pong.bin"; // Path to the atari rom to use
  TORCH_ARG(bool, grayscale) =
      false; // Whether to convert the frames to grayscale
  TORCH_ARG(bool, rescale) = false; // Whether to rescale the frames to 84,84
  TORCH_ARG(bool, clip_reward) = false; // If true, reward are clipped to [-1,
                                        // 1]

 protected:
  torch::Tensor getScreen();

  ActionVect action_set_;

  std::unique_ptr<ALEInterface> ale_;
  int width_, height_;

  std::deque<torch::Tensor> frame_buffer_;
};
