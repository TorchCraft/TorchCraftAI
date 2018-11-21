/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "common/rand.h"
#include "module.h"
#include "upcstorage.h"

#include <glog/logging.h>

using namespace cherrypi;

namespace {

struct MyUpcPostData : public UpcPostData {
  bool foo = true;
  int bar = 42;
};

class MyModule : public Module {};

size_t elapsedMs(hires_clock::duration const& duration) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(duration)
      .count();
}

}; // namespace

CASE("upcstorage/add_access") {
  UpcStorage storage;
  UpcId id;
  auto upc1 = std::make_shared<UPCTuple>();
  auto upc2 = std::make_shared<UPCTuple>();
  auto upc3 = std::make_shared<UPCTuple>();
  auto pdata3 = std::make_shared<MyUpcPostData>();
  auto module1 = Module::make<MyModule>();
  auto module23 = Module::make<MyModule>();

  id = storage.addUpc(1, kRootUpcId, module1.get(), upc1);
  EXPECT(id == 1);
  id = storage.addUpc(2, 1, module23.get(), upc2);
  EXPECT(id == 2);
  id = storage.addUpc(3, 1, module23.get(), upc3, pdata3);
  EXPECT(id == 3);

  EXPECT(storage.sourceId(-1000) == kInvalidUpcId);
  EXPECT(storage.sourceId(kFilteredUpcId) == kInvalidUpcId);
  EXPECT(storage.sourceId(kInvalidUpcId) == kInvalidUpcId);
  EXPECT(storage.sourceId(0) == kInvalidUpcId);
  EXPECT(storage.sourceId(1) == kRootUpcId);
  EXPECT(storage.sourceId(2) == 1);
  EXPECT(storage.sourceId(3) == 1);
  EXPECT(storage.sourceId(4) == kInvalidUpcId);
  EXPECT(storage.sourceId(1000) == kInvalidUpcId);

  EXPECT(storage.sourceIds(-1000) == std::vector<UpcId>{});
  EXPECT(storage.sourceIds(kFilteredUpcId) == std::vector<UpcId>{});
  EXPECT(storage.sourceIds(kInvalidUpcId) == std::vector<UpcId>{});
  EXPECT(storage.sourceIds(0) == std::vector<UpcId>{});
  EXPECT(storage.sourceIds(1) == std::vector<UpcId>{kRootUpcId});
  EXPECT(
      storage.sourceIds(1, module1.get()) ==
      std::vector<UpcId>{kRootUpcId}); // Not in list of sources
  EXPECT(
      storage.sourceIds(1, module23.get()) ==
      std::vector<UpcId>{kRootUpcId}); // Not in list of sources
  EXPECT(storage.sourceIds(2) == std::vector<UpcId>({1, kRootUpcId}));
  EXPECT(storage.sourceIds(2, module1.get()) == std::vector<UpcId>({1}));
  EXPECT(storage.sourceIds(3) == std::vector<UpcId>({1, kRootUpcId}));
  EXPECT(storage.sourceIds(3, module1.get()) == std::vector<UpcId>({1}));
  EXPECT(
      storage.sourceIds(3, module23.get()) ==
      std::vector<UpcId>({1, kRootUpcId})); // Not in list of sources
  EXPECT(storage.sourceIds(4) == std::vector<UpcId>{});
  EXPECT(storage.sourceIds(1000) == std::vector<UpcId>{});

  EXPECT(storage.upc(-1000) == nullptr);
  EXPECT(storage.upc(kFilteredUpcId) == nullptr);
  EXPECT(storage.upc(kInvalidUpcId) == nullptr);
  EXPECT(storage.upc(0) == nullptr);
  EXPECT(storage.upc(1) == upc1);
  EXPECT(storage.upc(2) == upc2);
  EXPECT(storage.upc(3) == upc3);
  EXPECT(storage.upc(4) == nullptr);
  EXPECT(storage.upc(1000) == nullptr);

  EXPECT(storage.post(-1000) == nullptr);
  EXPECT(storage.post(kFilteredUpcId) == nullptr);
  EXPECT(storage.post(kInvalidUpcId) == nullptr);
  EXPECT(storage.post(0) == nullptr);
  EXPECT(storage.post(1)->frame == 1);
  EXPECT(storage.post(1)->data == nullptr);
  EXPECT(storage.post(2)->frame == 2);
  EXPECT(storage.post(2)->data == nullptr);
  EXPECT(storage.post(3)->frame == 3);
  EXPECT(storage.post(3)->data == pdata3);
  EXPECT(
      std::static_pointer_cast<MyUpcPostData>(storage.post(3)->data)->bar ==
      42);
  EXPECT(storage.post(4) == nullptr);
  EXPECT(storage.post(1000) == nullptr);

  EXPECT(storage.upcPostsFrom(module1.get()).size() == 1u);
  EXPECT(storage.upcPostsFrom(module1.get(), 2).size() == 0u);
  EXPECT(storage.upcPostsFrom(module23.get()).size() == 2u);
  EXPECT(storage.upcPostsFrom(module23.get(), 1).size() == 0u);
  EXPECT(storage.upcPostsFrom(module23.get(), 2).size() == 1u);
  EXPECT(storage.upcPostsFrom(module23.get(), 3).size() == 1u);
}

CASE("upcstorage/non_persistent") {
  UpcStorage storage;
  storage.setPersistent(false);
  UpcId id;
  auto upc1 = std::make_shared<UPCTuple>();
  auto pdata1 = std::make_shared<MyUpcPostData>();

  id = storage.addUpc(0, kRootUpcId, nullptr, upc1, pdata1);
  EXPECT(id == 1);
  EXPECT(storage.sourceId(1) == kRootUpcId);
  EXPECT(storage.upc(1) == nullptr); // not stored
  EXPECT(storage.post(1)->upc == nullptr); // not stored
  EXPECT(storage.post(1)->data == nullptr); // not stored
}

CASE("upcstorage/benchmark[hide]") {
  // Prepare data
  size_t constexpr N = 1000000;
  auto upc = std::make_shared<UPCTuple>();
  auto postData = std::make_shared<MyUpcPostData>();
  auto module = Module::make<MyModule>();
  auto sources = std::vector<UpcId>(N, 0);
  for (size_t i = 1; i < N; i++) {
    sources[i] = common::Rand::rand() % i;
  }
  UpcStorage storage;

  // Insertion
  auto start = hires_clock::now();
  for (size_t i = 0; i < N; i++) {
    storage.addUpc(i / 4u, sources[i], module.get(), upc, postData);
  }
  auto end = hires_clock::now();
  VLOG(0) << "Inserted " << N << " els in " << elapsedMs(end - start) << "ms";

  // Query sources
  start = hires_clock::now();
  for (size_t i = 0; i < N; i++) {
    storage.sourceId(static_cast<UpcId>(i));
  }
  end = hires_clock::now();
  VLOG(0) << "Queried  " << N << " src in " << elapsedMs(end - start) << "ms";

  // Query all sources
  size_t sum = 0;
  start = hires_clock::now();
  for (size_t i = 0; i < N; i++) {
    sum += storage.sourceIds(static_cast<UpcId>(i)).size();
  }
  end = hires_clock::now();
  VLOG(0) << "Queried  " << N << " srT in " << elapsedMs(end - start)
          << "ms ; avg depth " << sum / N;
}
