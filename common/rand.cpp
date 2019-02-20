/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "rand.h"

#include <ATen/CPUGenerator.h>
#include <autogradpp/autograd.h>

#include <glog/logging.h>

namespace common {

using hires_clock = std::chrono::steady_clock;
std::mt19937 Rand::randEngine_;
std::mutex Rand::randEngineMutex_;

thread_local bool Rand::hasLocalSeed_ = false;
thread_local std::mt19937 Rand::localRandEngine_;

std::unique_ptr<at::Generator> Rand::torchEngine_;
thread_local std::unique_ptr<at::Generator> Rand::localTorchEngine_;

int64_t Rand::defaultRandomSeed() {
  return hires_clock::now().time_since_epoch().count();
}

void Rand::setSeed(int64_t seed) {
  std::lock_guard<std::mutex> guard(randEngineMutex_);
  std::seed_seq ss{uint32_t(seed & 0xFFFFFFFF), uint32_t(seed >> 32)};
  randEngine_.seed(ss);
  // Also set a global seed for rand() so that third-party code behaves
  // deterministically (if it happens to use rand()).
  std::srand(seed);

  try {
    torch::manual_seed(static_cast<uint32_t>(seed));
    torchEngine_ = std::make_unique<at::CPUGenerator>(&at::globalContext());
    torchEngine_->manualSeed(seed);
  } catch (std::exception const& ex) {
    LOG(WARNING) << "Failed to set torch random seed: " << ex.what();
  }
}

void Rand::setLocalSeed(int64_t seed) {
  std::seed_seq ss{uint32_t(seed & 0xFFFFFFFF), uint32_t(seed >> 32)};
  localRandEngine_.seed(ss);
  hasLocalSeed_ = true;
  try {
    localTorchEngine_ =
        std::make_unique<at::CPUGenerator>(&at::globalContext());
    localTorchEngine_->manualSeed(seed);
  } catch (std::exception const& ex) {
    LOG(WARNING) << "Failed to set torch random seed: " << ex.what();
  }
}

uint64_t Rand::rand() {
  if (hasLocalSeed_) {
    return localRandEngine_();
  }
  std::lock_guard<std::mutex> guard(randEngineMutex_);
  return randEngine_();
}

at::Generator* Rand::gen() {
  if (hasLocalSeed_) {
    return localTorchEngine_.get();
  }
  return torchEngine_.get();
}

std::string randId(size_t len) {
  static std::mt19937 rng = std::mt19937(std::random_device()());
  static const char alphanum[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  std::uniform_int_distribution<int> dis(0, sizeof(alphanum) - 2);
  std::string s(len, 0);
  for (size_t i = 0; i < len; i++) {
    s[i] = alphanum[dis(rng)];
  }
  return s;
}

} // namespace common
