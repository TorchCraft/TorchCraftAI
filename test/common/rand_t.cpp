/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#include "test.h"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <torch/torch.h>

#include "common/rand.h"

using namespace common;

CASE("common/rand/local_seed") {
  std::vector<int> ref;
  auto compare = [&ref](const std::vector<int>& other) {
    if (other.size() != ref.size())
      return false;
    for (size_t i = 0; i < other.size(); ++i) {
      if (ref[i] != other[i])
        return false;
    }
    return true;
  };

  Rand::setSeed(42);

  // We sample 10 ints from this seed;
  for (int i = 0; i < 10; i++) {
    ref.push_back(Rand::rand());
  }
  EXPECT(compare(ref));

  // if we sample another, it should be different
  std::vector<int> test;
  for (int i = 0; i < 10; i++) {
    test.push_back(Rand::rand());
  }
  EXPECT(!compare(test));

  const int threadCount = 4;

  auto thread = [&](int ind) {
    if (ind < 2) {
      Rand::setLocalSeed(42);
    } else {
      Rand::setSeed(42);
    }

    std::vector<int> local_sample;
    for (int i = 0; i < 10; ++i) {
      // we add some delays to make sure the threads sample in an interleaved
      // fasion
      if (i % 2 == ind % 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      local_sample.push_back(Rand::rand());
    }
    if (ind < 2) {
      // the local threads must have sampled the reference vector
      EXPECT(compare(local_sample));
    } else {
      // the other used the global seed, hence they will have different results
      EXPECT(!compare(local_sample));
    }
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < threadCount; ++i) {
    threads.emplace_back(thread, i);
  }

  for (int i = 0; i < threadCount; ++i) {
    threads[i].join();
  }
}

CASE("common/rand/torch") {
  torch::Tensor mean = torch::zeros({5});

  Rand::setSeed(42);
  torch::Tensor ref = torch::zeros({10, 5});
  for (int i = 0; i < 10; ++i) {
    ref.select(0, i) = at::normal(mean, 1, Rand::gen());
  }

  EXPECT(at::allclose(ref, ref));
  // a different sampling will do something different
  torch::Tensor test = torch::zeros({10, 5});
  for (int i = 0; i < 10; ++i) {
    test.select(0, i) = at::normal(mean, 1, Rand::gen());
  }
  EXPECT(!at::allclose(ref, test));

  const int threadCount = 4;

  auto thread = [&](int ind) {
    if (ind < 2) {
      Rand::setLocalSeed(42);
    } else {
      Rand::setSeed(42);
    }
    torch::Tensor local_sample = torch::zeros({10, 5});
    for (int i = 0; i < 10; ++i) {
      // we add some delays to make sure the threads sample in an interleaved
      // fasion
      if (i % 2 == ind % 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      local_sample.select(0, i) = at::normal(mean, 1, Rand::gen());
    }
    if (ind < 2) {
      // the local threads must have sampled the reference vector
      EXPECT(at::allclose(ref, local_sample));
    } else {
      // the other used the global seed, hence they will have different results
      EXPECT(!at::allclose(ref, local_sample));
    }
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < threadCount; ++i) {
    threads.emplace_back(thread, i);
  }

  for (int i = 0; i < threadCount; ++i) {
    threads[i].join();
  }
}
