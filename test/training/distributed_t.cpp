/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * Tests tagged '.distributed' should be run with ./distrun.
 */

#if defined(HAVE_CPID) && defined(HAVE_C10D)
#include "test.h"

#include "utils.h"

#include <c10d/FileStore.hpp>

#include <autogradpp/autograd.h>
#include <common/autograd/utils.h>
#include <common/fsutils.h>
#include <cpid/distributed.h>

using namespace cherrypi;
using namespace common;
namespace dist = cpid::distributed;

CASE("distributed/allreduce[.distributed]") {
  dist::init();
  dist::init();
  dist::init();
  int N = 1000;
  for (auto type :
       {at::kFloat, at::kByte, at::kChar, at::kDouble, at::kInt, at::kLong}) {
    auto var = torch::empty({N}, type);
    for (auto j = 0; j < 10; j++) {
      var.fill_(1);
      dist::allreduce(var);
      for (int i = 0; i < N; i++) {
        EXPECT(
            var[i].item<double>() == lest::approx(dist::globalContext()->size));
      }
    }
  }

  if (common::gpuAvailable()) {
    for (auto type : {at::kFloat, at::kDouble, at::kInt, at::kLong}) {
      auto var = torch::empty({N}, torch::TensorOptions(at::kCUDA).dtype(type));
      for (auto j = 0; j < 10; j++) {
        var.fill_(1);
        dist::allreduce(var);
        for (int i = 0; i < N; i++) {
          EXPECT(
              var[i].item<double>() ==
              lest::approx(dist::globalContext()->size));
        }
      }
    }
  }
}

CASE("distributed/templates[.distributed]") {
  dist::init();
#define FOR_ALL_TYPES(FUNC)     \
  FUNC(uint8_t, torch::kByte);  \
  FUNC(char, torch::kChar);     \
  FUNC(int8_t, torch::kChar);   \
  FUNC(int16_t, torch::kShort); \
  FUNC(int32_t, torch::kInt);   \
  FUNC(int64_t, torch::kLong);  \
  FUNC(float, torch::kFloat);   \
  FUNC(double, torch::kDouble);
#define TEST(FType, DType)                 \
  {                                        \
    std::vector<FType> vec = {5};          \
    dist::globalContext()->allreduce(vec); \
    dist::globalContext()->broadcast(vec); \
  }
  FOR_ALL_TYPES(TEST)
#undef FOR_ALL_TYPES
#undef TEST
}

CASE("distributed/broadcast[.distributed]") {
  dist::init();
  int N = 1000;
  int k = 0;
  auto size = dist::globalContext()->size;
  auto rank = dist::globalContext()->rank;
  for (auto type :
       {at::kFloat, at::kByte, at::kChar, at::kDouble, at::kInt, at::kLong}) {
    auto var = torch::empty({N}, type);
    for (auto j = 0; j < 10; j++) {
      var.fill_(rank);
      dist::broadcast(var, k % size);
      for (int i = 0; i < N; i++) {
        EXPECT(var[i].item<double>() == lest::approx(k % size));
      }
      k++;
    }
  }

  if (common::gpuAvailable()) {
    for (auto type : {at::kFloat, at::kDouble, at::kInt, at::kLong}) {
      auto var = torch::empty({N}, torch::TensorOptions(at::kCUDA).dtype(type));
      for (auto j = 0; j < 10; j++) {
        var.fill_(rank);
        dist::broadcast(var, k % size);
        for (int i = 0; i < N; i++) {
          EXPECT(var[i].item<double>() == lest::approx(k % size));
        }
        k++;
      }
    }
  }
}

CASE("distributed/allgather[.distributed]") {
  dist::init();
  int N = 1000;
  auto size = dist::globalContext()->size;
  auto rank = dist::globalContext()->rank;
  for (auto type :
       {at::kFloat, at::kByte, at::kChar, at::kDouble, at::kInt, at::kLong}) {
    auto var = torch::empty({N}, type);
    auto out = torch::empty({size, N}, type);
    for (auto j = 0; j < 10; j++) {
      var.fill_(rank);
      dist::allgather(out, var);
      auto outsum = out.toType(at::kDouble).sum(1);
      for (int i = 0; i < size; i++) {
        EXPECT(outsum[i].item<double>() == lest::approx(i * N));
      }
    }
  }
  for (auto type :
       {at::kFloat, at::kByte, at::kChar, at::kDouble, at::kInt, at::kLong}) {
    auto var = torch::empty({N}, torch::TensorOptions(at::kCUDA).dtype(type));
    auto out =
        torch::empty({size, N}, torch::TensorOptions(at::kCUDA).dtype(type));
    for (auto j = 0; j < 10; j++) {
      var.fill_(rank);
      dist::allgather(out, var);
      auto outsum = out.toType(at::kDouble).sum(1);
      for (int i = 0; i < size; i++) {
        EXPECT(outsum[i].item<double>() == lest::approx(i * N));
      }
    }
  }
}

CASE("distributed/context_TSANUnsafe") {
  auto file = fsutils::mktemp();
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(file); });
  auto constexpr nThreads = 3;

  std::vector<torch::Tensor> tensors;
  for (auto i = 0; i < nThreads; i++) {
    // Don't try to allreduce a cuda tensor!
    // NCCL will hang if you try to do operations from the same process, and I
    // don't want to start extra processes for a unit test...
    tensors.push_back(torch::ones({5, 5}));
  }

  auto test = [&](int rank) {
    auto store = std::make_shared<dist::FileStore>(file, nThreads);
    auto ctx = std::make_shared<dist::Context>(store, rank, nThreads);
    ctx->allreduce(tensors[rank]);
  };

  std::vector<std::thread> threads;
  for (auto i = 0; i < nThreads; i++) {
    threads.emplace_back(test, i);
  }
  for (auto& thread : threads) {
    thread.join();
  }

  for (auto& tensor : tensors) {
    EXPECT(tensor.sum().item<float>() == 25 * nThreads);
  }
}

CASE("distributed/barrier_TSANUnsafe") {
  auto file = fsutils::mktemp();
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(file); });
  auto constexpr nThreads = 3;

  std::atomic<int> atBarrier = 0;
  std::atomic<int> finished = 0;
  auto test = [&](int rank) {
    auto store = std::make_shared<dist::FileStore>(file, nThreads + 1);
    auto ctx = std::make_shared<dist::Context>(store, rank, nThreads + 1);
    ++atBarrier;
    ctx->barrier();
    ++finished;
  };

  std::vector<std::thread> threads;
  for (auto i = 0; i < nThreads; i++) {
    threads.emplace_back(test, i);
  }

  // Extra context in main to control execution
  auto store = std::make_shared<dist::FileStore>(file, nThreads + 1);
  auto ctx = std::make_shared<dist::Context>(store, nThreads, nThreads + 1);
  while (atBarrier.load() < nThreads) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  EXPECT(finished.load() == 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT(finished.load() == 0);
  ctx->barrier();

  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT(finished.load() == 3);
}

CASE("distributed/barrier_timeout_TSANUnsafe") {
  auto file = fsutils::mktemp();
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(file); });
  auto constexpr nThreads = 3;

  std::atomic<int> failed = 0;
  auto test = [&](int rank) {
    auto store = std::make_shared<dist::FileStore>(file, nThreads);
    auto timeout = std::chrono::seconds(1);
    store->setTimeout(timeout);
    auto ctx = std::make_shared<dist::Context>(store, rank, nThreads, timeout);

    if (rank == 0) { // delay for rank 0 to let everyone fail
      std::this_thread::sleep_for(timeout * 2);
    }

    try {
      ctx->barrier();
    } catch (...) {
      ++failed;
    }
  };

  std::vector<std::thread> threads;
  for (auto i = 0; i < nThreads; i++) {
    threads.emplace_back(test, i);
  }
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT(failed == nThreads);
}

#endif // HAVE_CPID
