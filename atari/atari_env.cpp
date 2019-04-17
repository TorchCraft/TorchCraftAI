/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "atari_env.hpp"
#include "common/autograd.h"

AtariWrapper&& AtariWrapper::make() {
  this->reset();
  return std::move(*this);
}

void AtariWrapper::reset() {
  ale_ = std::make_unique<ALEInterface>();
  ale_->setInt("random_seed", seed_);
  ale_->setBool("showinfo", false);
  ale_->setFloat("repeat_action_probability", 0.1);
  ale_->setBool("color_averaging", true);
  {
    static std::mutex ale_create_mutex;
    auto guard = std::lock_guard(ale_create_mutex);
    ale_->loadROM(ale_rom_);
  }

  auto& s = ale_->getScreen();
  width_ = s.width();
  height_ = s.height();

  action_set_ = ale_->getMinimalActionSet();
}

torch::Tensor AtariWrapper::getScreen() {
  std::vector<unsigned char> output;
  torch::Tensor screen;
  if (grayscale_) {
    ale_->getScreenGrayscale(output);
    screen = torch::from_blob(output.data(), {1, height_, width_}, torch::kByte)
                 .to(torch::kFloat) /
        256.;
  } else {
    ale_->getScreenRGB(output);

    screen = torch::from_blob(output.data(), {height_, width_, 3}, torch::kByte)
                 .permute({2, 0, 1})
                 .to(torch::kFloat) /
        256.;
  }

  if (rescale_) {
    screen = common::upsample(
                 screen.unsqueeze(0), common::UpsampleMode::Bilinear, {84, 84})
                 .squeeze(0);
  }
  return screen;
}

torch::Tensor AtariWrapper::getState() {
  frame_buffer_.push_front(getScreen());
  while (frame_buffer_.size() <= stacked_observations_) {
    ale_->act(action_set_.at(0));
    frame_buffer_.push_front(getScreen());
  }
  while (frame_buffer_.size() > stacked_observations_) {
    frame_buffer_.pop_back();
  }

  return torch::cat(
      std::vector<torch::Tensor>(frame_buffer_.begin(), frame_buffer_.end()),
      0);
}

void AtariWrapper::reset_game() {
  ale_->reset_game();
  frame_buffer_.clear();
}

double AtariWrapper::act(int action) {
  double reward = 0;
  for (int j = 0; j < frame_skip_; ++j) {
    reward += ale_->act(action_set_.at(action));
  }
  if (clip_reward_) {
    reward = std::min(1., reward);
    reward = std::max(-1., reward);
  }
  return reward;
}
