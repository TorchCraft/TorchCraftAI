/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * NOTE: each of these tests requires an empy redis instance available at
 * -redis_host and -redis_port.
 */

#include "test.h"

#include "cpid/distributed.h"
#include "cpid/redisstore.h"

#include <fmt/format.h>
#include <gflags/gflags.h>

// From redisclient_t.cpp
DECLARE_string(redis_host);
DECLARE_int32(redis_port);

using namespace cpid;
namespace dist = cpid::distributed;

CASE("redisstore/context[.redis]") {
  // This is a clone of distributed/context[.distributed]
  auto constexpr nThreads = 3;

  std::vector<torch::Tensor> tensors;
  for (auto i = 0; i < nThreads; i++) {
    // Don't try to allreduce a cuda tensor!
    // NCCL will hang if you try to do operations from the same process, and I
    // don't want to start extra processes for a unit test...
    tensors.push_back(torch::ones({5, 5}));
  }

  auto test = [&](int rank) {
    auto store = std::make_shared<RedisStore>(
        "rdvu", FLAGS_redis_host, FLAGS_redis_port);
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

CASE("redisstore/delete[.redis]") {
  auto prefix = "delete";
  auto store =
      std::make_shared<RedisStore>(prefix, FLAGS_redis_host, FLAGS_redis_port);
  uint32_t value = 0xDEADBEEF;
  auto* iptr = reinterpret_cast<uint8_t*>(&value);
  std::vector<uint8_t> v(iptr, iptr + sizeof(int32_t));
  EXPECT_NO_THROW(store->set("foo", v));

  std::unique_ptr<RedisClient> cl;
  EXPECT_NO_THROW(
      cl = std::make_unique<RedisClient>(FLAGS_redis_host, FLAGS_redis_port));
  RedisReply reply;
  EXPECT_NO_THROW(reply = cl->command({"GET", fmt::format("{}:foo", prefix)}));
  auto* cptr = reinterpret_cast<char*>(&value);
  std::string_view vs(cptr, sizeof(int32_t));
  EXPECT(reply.stringv() == vs);

  store.reset(); // deletion of store causes keys to be deleted
  EXPECT_NO_THROW(
      reply = cl->command({"EXISTS", fmt::format("{}:foo", prefix)}));
  EXPECT(reply.integer() == 0);
}
