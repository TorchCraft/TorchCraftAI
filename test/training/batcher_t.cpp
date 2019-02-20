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

  void reset() override {}
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

SCENARIO("batcher.SubBatchAsyncBatcher") {
  std::shared_ptr<AsyncBatcher> batcher =
      std::make_unique<SubBatchAsyncBatcher>(4);
  auto batchersb = std::static_pointer_cast<SubBatchAsyncBatcher>(batcher);

  GIVEN("2 variants of different subbatch size") {
    std::vector<ag::Variant> batch = {
        ag::VariantDict{{"action", torch::zeros({10})},
                        {"pi", torch::zeros({10, 2})}},
        ag::VariantDict{{"action", torch::zeros({5})},
                        {"pi", torch::zeros({5, 2})}}};
    ag::Variant batched = batcher->makeBatch(batch);
    EXPECT(batched.isDict());
    EXPECT(batched.getDict()["action"].get().sizes() == at::IntList({15}));

    std::vector<ag::Variant> unbatched = batcher->unBatch(batched);
    EXPECT(unbatched.size() == 2);
    EXPECT(unbatched[0].getDict()["action"].get().sizes() == at::IntList({10}));
    EXPECT(unbatched[1].getDict()["action"].get().sizes() == at::IntList({5}));
    EXPECT(unbatched[0].getDict()["pi"].get().sizes() == at::IntList({10, 2}));
    EXPECT(unbatched[1].getDict()["pi"].get().sizes() == at::IntList({5, 2}));
  }

  GIVEN("2 variants with keys of different subbatch size") {
    std::vector<ag::Variant> batch = {
        ag::Variant({{"our_units_ft", torch::zeros({5, 2})},
                     {"nmy_units_ft", torch::zeros({10, 2})}}),
        ag::Variant({{"our_units_ft", torch::zeros({6, 2})},
                     {"nmy_units_ft", torch::zeros({9, 2})}})};
    ag::Variant batched = batcher->makeBatch(batch);
    std::vector<ag::Variant> unbatched = batcher->unBatch(batched);

    EXPECT(unbatched.size() == 2);
    EXPECT(
        unbatched[0].getDict()["our_units_ft"].get().sizes() ==
        at::IntList({5, 2}));
    EXPECT(
        unbatched[1].getDict()["our_units_ft"].get().sizes() ==
        at::IntList({6, 2}));
    EXPECT(
        unbatched[0].getDict()["nmy_units_ft"].get().sizes() ==
        at::IntList({10, 2}));
    EXPECT(
        unbatched[1].getDict()["nmy_units_ft"].get().sizes() ==
        at::IntList({9, 2}));
  }

  GIVEN("variants with incompatible keys") {
    std::vector<ag::Variant> batch = {
        ag::VariantDict{{"pi", torch::zeros({10, 2})}},
        ag::VariantDict{
            {"action", torch::zeros({5})},
        }};
    EXPECT_THROWS(batcher->makeBatch(batch));
  }

  GIVEN("unbatch without batch_size key") {
    EXPECT_THROWS(batcher->unBatch(ag::VariantDict{
        {"a", torch::zeros({10, 2})},
        {"b", torch::zeros({20, 2})},
    }));
  }

  GIVEN("unbatch with custom subbatch size") {
    ag::VariantDict toUnbatch{
        {"pi", torch::ones({3, 2})},
        {SubBatchAsyncBatcher::kBatchInfoKey,
         ag::VariantDict{{"pi", torch::tensor({2, 1}, at::kLong)}}}};
    std::vector<ag::Variant> unbatched = batcher->unBatch(toUnbatch);
    EXPECT(unbatched.size() == 2);
    EXPECT(unbatched[0].getDict()["pi"].get().sizes() == at::IntList({2, 2}));
    EXPECT(unbatched[1].getDict()["pi"].get().sizes() == at::IntList({1, 2}));
  }

  GIVEN("operation on unbatched tensor") {
    std::vector<ag::Variant> batch = {
        ag::VariantDict{{"input", torch::ones({10}, at::kLong)}},
        ag::VariantDict{{"input", torch::ones({5}, at::kLong)}}};
    ag::Variant batched = batcher->makeBatch(batch);
    // Do an operation on the tensor unbatched:
    batched = ag::VariantDict{
        {"result",
         SubBatchAsyncBatcher::forEachSubbatch(
             batched,
             "input", // Same batch as "action"
             batched["input"],
             [](at::Tensor t) -> at::Tensor { return t.sum(); })}};

    std::vector<ag::Variant> unbatched = batcher->unBatch(batched);

    EXPECT(unbatched.size() == 2);
    std::vector res = {unbatched[0].getDict()["result"].get(),
                       unbatched[1].getDict()["result"].get()};
    EXPECT(res[0].sizes() == at::IntList({}));
    EXPECT(res[1].sizes() == at::IntList({}));
    EXPECT(res[0].item<long>() == 10);
    EXPECT(res[1].item<long>() == 5);
  }

  GIVEN("padding in tensor_list") {
    std::vector<torch::Tensor> tensors = {torch::ones({2, 10}, at::kLong),
                                          torch::ones({3, 9}, at::kLong) * 2};
    EXPECT_THROWS(batchersb->makeBatchTensors(tensors, 0));
    batchersb->allowPadding(true);
    auto out = batchersb->makeBatchTensors(tensors, 0);
    batchersb->allowPadding(false);
    EXPECT(out.sizes() == at::IntList({5, 10}));
    EXPECT(out[0][9].item<long>() == 1);
    EXPECT(out[1][9].item<long>() == 1);
    EXPECT(out[2][8].item<long>() == 2);
    EXPECT(out[3][8].item<long>() == 2);
    EXPECT(out[4][8].item<long>() == 2);
    EXPECT(out[4][9].item<long>() == 0);
  }

  GIVEN("padding in Dict") {
    std::vector<ag::Variant> vars = {
        ag::VariantDict{{"k", torch::ones({2, 10}, at::kLong)}},
        ag::VariantDict{{"k", torch::ones({3, 9}, at::kLong) * 2}},
    };
    EXPECT_THROWS(batchersb->makeBatch(vars, 0));
    batchersb->allowPadding(true);
    auto out = batchersb->makeBatch(vars, 0)["k"];
    batchersb->allowPadding(false);
    EXPECT(out.sizes() == at::IntList({5, 10}));
    EXPECT(out[0][9].item<long>() == 1);
    EXPECT(out[1][9].item<long>() == 1);
    EXPECT(out[2][8].item<long>() == 2);
    EXPECT(out[3][8].item<long>() == 2);
    EXPECT(out[4][8].item<long>() == 2);
    EXPECT(out[4][9].item<long>() == 0);
  }
}
