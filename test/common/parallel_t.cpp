/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "common/parallel.h"
#include "common/rand.h"

using namespace common;

using C1Type = std::unique_ptr<BufferedConsumer<std::string>>;
template <size_t N>
using C2Type = std::unique_ptr<BufferedConsumer<int>>;
CASE("common/parallel/bufferedconsumer/1c") {
  auto run = [&](uint8_t t1, uint8_t t2) {
    int result = 0;
    std::mutex mx;
    auto c2 = std::make_unique<BufferedConsumer<int>>(t2, 10, [&](int i) {
      if (t2 <= 1) {
        result += i * 2;
      } else {
        // We have more than one thread for c2 so we need to protect the result
        std::lock_guard<std::mutex> guard(mx);
        result += i * 2;
      }
    });

    auto c1 = std::make_unique<BufferedConsumer<std::string>>(
        t1, 1000, [&](std::string s) { c2->enqueue(std::atoi(s.c_str())); });

    for (auto const& s : {"1", "2", "3", "4", "5"}) {
      for (int i = 0; i < 100; i++) {
        c1->enqueue(s);
      }
    }

    EXPECT((c1->wait(), true));
    EXPECT((c1 = nullptr, true));
    EXPECT((c2->wait(), true));
    EXPECT((c2 = nullptr, true));
    EXPECT(result == 3000);
  };

  // Test for 0, 1, and 5 threads for c2
  run(10, 0);
  run(10, 1);
  run(10, 5);
}

CASE("common/parallel/bufferedconsumer/enqueue_or_replace_oldest") {
  std::mutex m;
  std::unique_lock l(m);
  int total = 0;
  std::atomic<bool> insideCallback = false;

  auto producer = std::make_unique<BufferedConsumer<int>>(1, 1, [&](int i) {
    insideCallback = true;
    std::unique_lock l2(m);
    total += i;
    insideCallback = false;
  });
  EXPECT_NO_THROW(producer->enqueue(1));
  while (!insideCallback) {
  }
  // Now the producer queue is empty
  producer->enqueueOrReplaceOldest(10); // Should get added to the list
  producer->enqueueOrReplaceOldest(100); // Should replace the previous one
  l.unlock();
  producer->wait();
  EXPECT(total == 101);
}

CASE("common/parallel/bufferedproducer/starved") {
  int i = 0;
  std::mutex mutex;
  auto prodFunc = [&]() {
    auto ret = [&]() {
      std::unique_lock<std::mutex> lk(mutex);
      i++;
      return i * i;
    }();
    std::this_thread::sleep_for(
        std::chrono::milliseconds(common::Rand::rand() % 100));
    return ret;
  };

  auto test = [&](auto nThreads, int nQueueSz) {
    auto producer =
        std::make_unique<BufferedProducer<int>>(nThreads, nQueueSz, prodFunc);
    for (int i = 0; i < 100; i++) {
      auto val = producer->get().value();
      EXPECT(std::sqrt(val) * std::sqrt(val) == val);
    }
    EXPECT((producer = nullptr, true));
    i = 0;
  };

  test(1, 10);
  test(5, 10);
  test(10, 5);
}

CASE("common/parallel/bufferedproducer/queue_full") {
  int i = 0;
  std::mutex mutex;
  auto prodFunc = [&]() {
    return [&]() {
      std::unique_lock<std::mutex> lk(mutex);
      i++;
      return i * i;
    }();
  };

  auto test = [&](auto nThreads, int nQueueSz) {
    auto producer =
        std::make_unique<BufferedProducer<int>>(nThreads, nQueueSz, prodFunc);
    for (int i = 0; i < 10; i++) {
      auto val = producer->get().value();
      std::this_thread::sleep_for(
          std::chrono::milliseconds(common::Rand::rand() % 100));
      EXPECT(std::sqrt(val) * std::sqrt(val) == val);
    }
    EXPECT((producer = nullptr, true));
    i = 0;
  };

  test(1, 10);
  test(5, 10);
  test(10, 5);
}

CASE("common/parallel/bufferedproducer/stop") {
  std::atomic_int i{0};
  auto prodFunc = [&]() {
    return [&]() -> std::optional<int> {
      int next = i++;
      if (next >= 1000) {
        return {};
      }
      return next;
    }();
  };

  auto test = [&](auto nThreads, int nQueueSz) {
    auto producer =
        std::make_unique<BufferedProducer<int>>(nThreads, nQueueSz, prodFunc);
    for (int j = 0; j < 1000; j++) {
      auto x = producer->get();
      EXPECT(x.has_value());
    }
    EXPECT(!producer->get().has_value());
    EXPECT(!producer->get().has_value());
    EXPECT(!producer->get().has_value());
    EXPECT(!producer->get().has_value());
    EXPECT((producer = nullptr, true));
    i = 0;
  };

  test(1, 10);
  test(5, 10);
  test(10, 5);
}
