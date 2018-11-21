/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "common/rand.h"
#include "utils.h"

using namespace cherrypi;
using namespace cherrypi::utils;

CASE("utils/geometry/bb_distance/top_left_bottom_right") {
  EXPECT(
      pxDistanceBB(20, 20, 30, 30, 5, 5, 15, 15) ==
      int(pxdistance(20, 20, 15, 15)));
  EXPECT(
      pxDistanceBB(5, 5, 15, 15, 20, 20, 30, 30) ==
      int(pxdistance(15, 15, 20, 20)));
  EXPECT(pxDistanceBB(20, 20, 30, 30, 10, 10, 20, 20) == 0);
  EXPECT(pxDistanceBB(10, 10, 20, 20, 20, 20, 30, 30) == 0);
}

CASE("utils/geometry/bb_distance/top_right_bottom_left") {
  EXPECT(
      pxDistanceBB(20, 20, 30, 30, 35, 5, 45, 15) ==
      int(pxdistance(30, 20, 35, 15)));
  EXPECT(
      pxDistanceBB(35, 5, 45, 15, 20, 20, 30, 30) ==
      int(pxdistance(35, 15, 30, 20)));
  EXPECT(pxDistanceBB(20, 20, 30, 30, 30, 20, 40, 30) == 0);
  EXPECT(pxDistanceBB(30, 20, 40, 30, 20, 20, 30, 30) == 0);
}

CASE("utils/geometry/bb_distance/top_bottom_adjacent") {
  EXPECT(pxDistanceBB(20, 20, 30, 30, 20, 5, 30, 15) == 20 - 15);
  EXPECT(pxDistanceBB(20, 20, 30, 30, 25, 5, 35, 15) == 20 - 15);
  EXPECT(pxDistanceBB(20, 20, 30, 30, 15, 5, 25, 15) == 20 - 15);
  EXPECT(pxDistanceBB(20, 5, 30, 15, 20, 20, 30, 30) == 20 - 15);
  EXPECT(pxDistanceBB(25, 5, 35, 15, 20, 20, 30, 30) == 20 - 15);
  EXPECT(pxDistanceBB(15, 5, 25, 15, 20, 20, 30, 30) == 20 - 15);
  EXPECT(pxDistanceBB(20, 20, 30, 30, 20, 10, 30, 20) == 0);
  EXPECT(pxDistanceBB(20, 10, 30, 20, 20, 20, 30, 30) == 0);
}

CASE("utils/geometry/bb_distance/left_right_adjacent") {
  EXPECT(pxDistanceBB(20, 20, 30, 30, 35, 20, 45, 30) == 35 - 30);
  EXPECT(pxDistanceBB(20, 20, 30, 30, 35, 25, 45, 35) == 35 - 30);
  EXPECT(pxDistanceBB(20, 20, 30, 30, 35, 15, 45, 25) == 35 - 30);
  EXPECT(pxDistanceBB(35, 20, 45, 30, 20, 20, 30, 30) == 35 - 30);
  EXPECT(pxDistanceBB(35, 25, 45, 35, 20, 20, 30, 30) == 35 - 30);
  EXPECT(pxDistanceBB(35, 15, 45, 25, 20, 20, 30, 30) == 35 - 30);
  EXPECT(pxDistanceBB(20, 20, 30, 30, 30, 20, 40, 30) == 0);
  EXPECT(pxDistanceBB(30, 20, 40, 30, 20, 20, 30, 30) == 0);
}

CASE("utils/geometry/bb_distance/intersecting") {
  EXPECT(pxDistanceBB(20, 20, 30, 30, 20, 20, 30, 30) == 0);
  EXPECT(pxDistanceBB(20, 20, 30, 30, 25, 20, 35, 30) == 0);
  EXPECT(pxDistanceBB(20, 20, 30, 30, 15, 20, 25, 30) == 0);
  EXPECT(pxDistanceBB(20, 20, 30, 30, 20, 25, 30, 35) == 0);
  EXPECT(pxDistanceBB(20, 20, 30, 30, 20, 15, 30, 25) == 0);
  EXPECT(pxDistanceBB(20, 20, 30, 30, 15, 15, 25, 25) == 0);
  EXPECT(pxDistanceBB(20, 20, 30, 30, 25, 25, 35, 35) == 0);
}

CASE("utils/gemoetry/getmovepos") {
  // Vector rotation test
  for (int angle = 0; angle < 360; angle += 10) {
    auto dest =
        utils::getMovePosHelper(100, 100, 110, 100, 256, 256, angle, true);
    auto dirX = dest.x - 100;
    auto dirY = dest.y - 100;
    // 20 because getMovePosHelper rounds...
    EXPECT(std::abs(dirX * dirX + dirY * dirY - 100) <= 20);
  }
  for (int angle = 0; angle < 360; angle += 10) {
    auto dest =
        utils::getMovePosHelper(100, 100, 101, 100, 256, 256, angle, false);
    auto dirX = dest.x - 100;
    auto dirY = dest.y - 100;
    // At least 10 walktiles ahead, even when we sepcified magnitude of 1
    EXPECT(std::abs(dirX * dirX + dirY * dirY) >= 80);
  }
}

using C1Type = std::unique_ptr<BufferedConsumer<std::string, 10>>;
template <size_t N>
using C2Type = std::unique_ptr<BufferedConsumer<int, N>>;
CASE("utils/parallel/bufferedconsumer/1c") {
  auto run = [&](auto c1, auto c2) {
    c1 = std::make_unique<typename decltype(c1)::element_type>(
        1000, [&](std::string s) { c2->enqueue(std::atoi(s.c_str())); });

    int result = 0;
    std::mutex mx;
    c2 = std::make_unique<typename decltype(c2)::element_type>(10, [&](int i) {
      if (decltype(c2)::element_type::nthreads <= 1) {
        result += i * 2;
      } else {
        // We have more than one thread for c2 so we need to protect the result
        std::lock_guard<std::mutex> guard(mx);
        result += i * 2;
      }
    });

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
  run(C1Type(), C2Type<0>());
  run(C1Type(), C2Type<1>());
  run(C1Type(), C2Type<5>());
}

CASE("utils/parallel/bufferedproducer/starved") {
  int i = 0;
  std::mutex mutex;
  auto prodFunc = [&]() {
    auto ret = [&]() {
      std::unique_lock<std::mutex> lk(mutex);
      i++;
      return std::make_unique<int>(i * i);
    }();
    std::this_thread::sleep_for(
        std::chrono::milliseconds(common::Rand::rand() % 100));
    return ret;
  };

  auto test = [&](auto nThreads, int nQueueSz) {
    auto producer =
        std::make_unique<BufferedProducer<int>>(nThreads, nQueueSz, prodFunc);
    for (int i = 0; i < 100; i++) {
      auto val = *(producer->get());
      EXPECT(std::sqrt(val) * std::sqrt(val) == val);
    }
    EXPECT((producer = nullptr, true));
    i = 0;
  };

  test(1, 10);
  test(5, 10);
  test(10, 5);
}

CASE("utils/parallel/bufferedproducer/queue_full") {
  int i = 0;
  std::mutex mutex;
  auto prodFunc = [&]() {
    return [&]() {
      std::unique_lock<std::mutex> lk(mutex);
      i++;
      return std::make_unique<int>(i * i);
    }();
  };

  auto test = [&](auto nThreads, int nQueueSz) {
    auto producer =
        std::make_unique<BufferedProducer<int>>(nThreads, nQueueSz, prodFunc);
    for (int i = 0; i < 10; i++) {
      auto val = *(producer->get());
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

CASE("utils/parallel/bufferedproducer/stop") {
  std::atomic_int i{0};
  auto prodFunc = [&]() {
    return [&]() -> std::unique_ptr<int> {
      int next = i++;
      if (next >= 1000)
        return nullptr;
      return std::make_unique<int>(next);
    }();
  };

  auto test = [&](auto nThreads, int nQueueSz) {
    auto producer =
        std::make_unique<BufferedProducer<int>>(nThreads, nQueueSz, prodFunc);
    for (int j = 0; j < 1000; j++) {
      auto x = producer->get();
      EXPECT(x != nullptr);
    }
    EXPECT(producer->get() == nullptr);
    EXPECT(producer->get() == nullptr);
    EXPECT(producer->get() == nullptr);
    EXPECT(producer->get() == nullptr);
    EXPECT((producer = nullptr, true));
    i = 0;
  };

  test(1, 10);
  test(5, 10);
  test(10, 5);
}

CASE("utils/algorithms/cmerge/vector") {
  using iv = std::vector<int>;
  {
    // 1 argument
    auto a = iv{1, 2};
    auto b = utils::cmerge(a);
    auto t = iv{1, 2};
    EXPECT(b == t);
  }
  {
    // 2 arguments
    auto a = iv{1, 2};
    auto b = iv{3, 4};
    auto c = utils::cmerge(a, b);
    auto t = iv{1, 2, 3, 4};
    EXPECT(c == t);
  }
  {
    // 3 arguments
    auto a = iv{1, 2};
    auto b = iv{3, 4};
    auto c = iv{3, 4};
    auto d = utils::cmerge(a, b, c);
    auto t = iv{1, 2, 3, 4, 3, 4};
    EXPECT(d == t);
  }
  {
    // 4 arguments
    auto a = iv{1, 2};
    auto b = iv{3, 4};
    auto c = iv{3, 4, 0, 0};
    auto d = iv{5, 6};
    auto e = utils::cmerge(a, b, c, d);
    auto t = iv{1, 2, 3, 4, 3, 4, 0, 0, 5, 6};
    EXPECT(e == t);
  }
}

CASE("utils/algorithms/cmerge/map") {
  using smap = std::map<std::string, int>;
  {
    // 1 argument
    auto a = smap{{"a", 1}, {"b", 2}};
    auto b = utils::cmerge(a);
    auto t = smap{{"a", 1}, {"b", 2}};
    EXPECT(b == t);
  }
  {
    // 2 arguments
    auto a = smap{{"a", 1}, {"b", 2}};
    auto b = smap{{"c", 3}, {"d", 4}};
    auto c = utils::cmerge(a, b);
    auto t = smap{{"a", 1}, {"b", 2}, {"c", 3}, {"d", 4}};
    EXPECT(c == t);
  }
  {
    // 3 arguments
    auto a = smap{{"a", 1}, {"b", 2}};
    auto b = smap{{"dup", 3}, {"d", 4}};
    auto c =
        smap{{"dup", 0}, {"e", 6}}; // Result will use value from b for "dup"
    auto d = utils::cmerge(a, b, c);
    auto t = smap{{"a", 1}, {"b", 2}, {"dup", 3}, {"d", 4}, {"e", 6}};
    EXPECT(d == t);
  }
}
