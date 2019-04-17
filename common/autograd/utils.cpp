/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "utils.h"

#include <autogradpp/autograd.h>

namespace common {

bool gpuAvailable() {
  // torch::cuda::is_available() will throw an exception if we compiled the
  // binary with CUDA but run it on a CPU-only machine since the CUDA calls it
  // performs will fail. We assume that if it throws, we don't have a GPU.
  try {
    return torch::cuda::is_available();
  } catch (...) {
  }
  return false;
}

} // namespace common
