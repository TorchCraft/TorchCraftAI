/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "rand.h"

#ifdef HAVE_TORCH
#include <ATen/CPUGenerator.h>
#include <autogradpp/autograd.h>
#endif // HAVE_TORCH

#include <glog/logging.h>

namespace common {

using hires_clock = std::chrono::steady_clock;
std::mt19937 Rand::randEngine_;
std::mutex Rand::randEngineMutex_;

thread_local bool Rand::hasLocalSeed_ = false;
thread_local std::mt19937 Rand::localRandEngine_;

#ifdef HAVE_TORCH
std::unique_ptr<at::Generator> Rand::torchEngine_;
thread_local std::unique_ptr<at::Generator> Rand::localTorchEngine_;
#endif // HAVE_TORCH

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

#ifdef HAVE_TORCH
  try {
    torch::manual_seed(static_cast<uint32_t>(seed));
    torchEngine_ = std::make_unique<at::CPUGenerator>(&at::globalContext());
    torchEngine_->manualSeed(seed);
  } catch (std::exception const& ex) {
    LOG(WARNING) << "Failed to set torch random seed: " << ex.what();
  }
#endif // HAVE_TORCH
}

void Rand::setLocalSeed(int64_t seed) {
  std::seed_seq ss{uint32_t(seed & 0xFFFFFFFF), uint32_t(seed >> 32)};
  localRandEngine_.seed(ss);
  hasLocalSeed_ = true;
#ifdef HAVE_TORCH
  try {
    localTorchEngine_ =
        std::make_unique<at::CPUGenerator>(&at::globalContext());
    localTorchEngine_->manualSeed(seed);
  } catch (std::exception const& ex) {
    LOG(WARNING) << "Failed to set torch random seed: " << ex.what();
  }
#endif // HAVE_TORCH
}

uint64_t Rand::rand() {
  if (hasLocalSeed_) {
    return localRandEngine_();
  }
  std::lock_guard<std::mutex> guard(randEngineMutex_);
  return randEngine_();
}

#ifdef HAVE_TORCH
at::Generator* Rand::gen() {
  if (hasLocalSeed_) {
    return localTorchEngine_.get();
  }
  return torchEngine_.get();
}
#endif // HAVE_TORCH

} // namespace common
