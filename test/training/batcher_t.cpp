/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <chrono>
#include <future>
#include <glog/logging.h>
#include <iostream>
#include <thread>
#include <vector>

#include "test.h"

#include "cpid/batcher.h"
using namespace cpid;

static const int kBatchSize = 30;
static const int kNumWorkers = 60;
class BatchMock : public ag::Container_CRTP<BatchMock> {
 public:
  BatchMock() {}

  void reset() {}
  ag::Variant forward(ag::Variant input) override {
    torch::Tensor in = input[0];
    torch::Tensor out1 = in.clone();
    out1 = out1.where(out1.lt(0), out1 + 1);

    torch::Tensor out2 = in.clone();
    out2 = out2.where(out2.lt(0), out2 * 10 + 1);

    VLOG(0) << "Forward : in size" << in.size(0) << " out size "
            << out1.size(0);
    return ag::VariantDict{{"result", out1}, {"result2", out2}};
  }
};

CASE("batcher[.flaky]") {
  std::shared_ptr<BatchMock> runner = std::make_shared<BatchMock>();
  AsyncBatcher batcher(runner, kBatchSize);

  auto worker = [&](int seed) {
    std::mt19937 gen(seed);
    std::uniform_int_distribution<> dist(0, 42000);
    VLOG(0) << "Starting thread " << seed;

    for (int i = 0; i < 10; ++i) {
      int target = dist(gen);
      auto state = torch::zeros(seed + 1, at::kInt).fill_(target);
      for (int j = 0; j < seed + 1; ++j) {
        state[j] = target + j;
      }
      EXPECT(state[0].item<int32_t>() == target);

      VLOG(0) << "Thread " << seed << " about to send ";
      auto result = batcher.batchedForward(ag::Variant(state))["result"];
      auto result2 = batcher.batchedForward(ag::Variant(state))["result2"];

      EXPECT((result == state + 1).all().item<uint8_t>());
      EXPECT((result2 == state * 10 + 1).all().item<uint8_t>());
    }
    VLOG(0) << "Thread " << seed << " done";
  };

  std::vector<std::thread> workers;

  for (int i = 0; i < kNumWorkers; i++) {
    workers.emplace_back(worker, i);
  }
  for (int i = 0; i < kNumWorkers; i++) {
    workers[i].join();
  }
}
