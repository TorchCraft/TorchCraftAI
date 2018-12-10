/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <memory>
#include <mutex>
#include <random>

#include <ATen/Generator.h>

namespace common {

// This provides some thead-safe random primitives

class Rand {
 public:
  /// Set the seed for random generators: this one, rand(3) and ATen
  static void setSeed(int64_t seed);

  /// Set a static seed for the local thread.
  static void setLocalSeed(int64_t seed);

  /// Sample random value
  static uint64_t rand();

  /// Default random seed used by init()
  static int64_t defaultRandomSeed();

  /// Random number engine based on previously set seed
  template <typename T>
  static T makeRandEngine() {
    std::seed_seq seed{Rand::rand(), Rand::rand()};
    return T(seed);
  }

  /// Sample from a given distribution
  template <typename T>
  static auto sample(T&& distrib) -> decltype(distrib.min()) {
    if (hasLocalSeed_) {
      return distrib(localRandEngine_);
    }
    std::lock_guard<std::mutex> guard(randEngineMutex_);
    return distrib(randEngine_);
  }

  /**
   * This allows to use a custom seed in torch.
   * For example: at::normal(mean, dev, Rand::gen());
   * Similarly to rand(), this will use a thread_local generator if a local seed
   * is set
   */
  static at::Generator* gen();

 protected:
  static std::mt19937 randEngine_;
  static std::mutex randEngineMutex_;

  static thread_local bool hasLocalSeed_;
  static thread_local std::mt19937 localRandEngine_;

  static std::unique_ptr<at::Generator> torchEngine_;
  static thread_local std::unique_ptr<at::Generator> localTorchEngine_;
};

// This method was originally written by Christopher Smith at
// https://stackoverflow.com/questions/6942273/get-random-element-from-container
// and is used under CC BY-SA: https://creativecommons.org/licenses/by-sa/2.0/
template <typename Iter, typename RandomGenerator>
Iter select_randomly(Iter start, Iter end, RandomGenerator& g) {
  std::uniform_int_distribution<> dis(0, std::distance(start, end) - 1);
  std::advance(start, dis(g));
  return start;
}
} // namespace common
