/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifdef HAVE_CPID
#include "test.h"

#include "fsutils.h"
#include "utils.h"

#include <c10d/FileStore.hpp>

#include <autogradpp/autograd.h>
#include <common/autograd/utils.h>
#include <cpid/distributed.h>

using namespace cherrypi;
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

CASE("distributed/context[.distributed]") {
  auto file = fsutils::mktemp();
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(file); });
  constexpr auto nThreads = 3;

  std::vector<torch::Tensor> tensors;
  for (auto i = 0; i < nThreads; i++) {
    // Don't try to allreduce a cuda tensor!
    // NCCL will hang if you try to do operations from the same process, and I
    // don't want to start extra processes for a unit test...
    tensors.push_back(torch::ones({5, 5}));
  }

  auto test = [&](int rank) {
    auto store = std::make_shared<dist::FileStore>(file, 3);
    auto ctx = std::make_shared<dist::Context>(store, rank, 3);
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

#endif // HAVE_CPID
