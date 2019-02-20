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

#include "cpid/prioritymutex.h"
using namespace cpid;

const int kSleep = 50;

priority_mutex l(3);

enum Prio { Low, Mid, High };
std::vector<Prio> order;

int readyToStart = 0;
std::condition_variable readyToStartCV;
std::mutex readyToStartMutex;
void hpt(const char* s, int id) {
  using namespace std;
  std::unique_lock<std::mutex> ready(readyToStartMutex);
  readyToStartCV.wait(ready, [&]() { return readyToStart == id; });
  readyToStart++;
  ready.unlock();
  readyToStartCV.notify_all();
  l.lock(2);
  VLOG(0) << s;
  order.push_back(High);
  std::this_thread::sleep_for(std::chrono::milliseconds(kSleep));
  l.unlock();
}

void lpt(const char* s, int id) {
  using namespace std;
  std::unique_lock<std::mutex> ready(readyToStartMutex);
  readyToStartCV.wait(ready, [&]() { return readyToStart == id; });
  readyToStart++;
  ready.unlock();
  readyToStartCV.notify_all();
  l.lock(0);
  VLOG(0) << s;
  order.push_back(Low);
  std::this_thread::sleep_for(std::chrono::milliseconds(kSleep));
  l.unlock();
}

void mpt(const char* s, int id) {
  using namespace std;
  std::unique_lock<std::mutex> ready(readyToStartMutex);
  readyToStartCV.wait(ready, [&]() { return readyToStart == id; });
  readyToStart++;
  ready.unlock();
  readyToStartCV.notify_all();
  l.lock(1);
  VLOG(0) << s;
  order.push_back(Mid);
  std::this_thread::sleep_for(std::chrono::milliseconds(kSleep));
  l.unlock();
  // lowpriounlock();
}
CASE("priority_mutex") {
  order.clear();
  readyToStart = 0;

  // Try lock should work fine here
  EXPECT(l.try_lock(0));
  l.unlock();

  auto t0 = new std::thread(lpt, "low prio t0 working here", 0);
  auto t1 = new std::thread(lpt, "low prio t1 working here", 1);
  auto t1b = new std::thread(mpt, "mid prio t1b working here", 2);
  auto t3 = new std::thread(lpt, "low prio t3 working here", 3);
  auto t4 = new std::thread(lpt, "low prio t4 working here", 4);
  auto t2 = new std::thread(hpt, "high prio t2 working here", 5);
  auto t5 = new std::thread(lpt, "low prio t5 working here", 6);
  auto t6 = new std::thread(lpt, "low prio t6 working here", 7);
  auto t7 = new std::thread(lpt, "low prio t7 working here", 8);
  auto t8 = new std::thread(mpt, "mid prio t8 working here", 9);
  auto t9 = new std::thread(hpt, "high prio t9 working here", 10);

  // some threads are working, we can't try_lock
  EXPECT(!l.try_lock(0));
  // even on high prio
  EXPECT(!l.try_lock(2));

  t0->join();
  t1->join();
  t1b->join();
  t2->join();
  t3->join();
  t4->join();
  t5->join();
  t6->join();
  t7->join();
  t8->join();
  t9->join();

  std::vector<Prio> expected_order{
      Low, High, High, Mid, Mid, Low, Low, Low, Low, Low, Low};

  EXPECT(order.size() == expected_order.size());
  for (size_t i = 0; i < order.size(); ++i) {
    EXPECT(order[i] == expected_order[i]);
  }

  delete t0;
  delete t1;
  delete t1b;
  delete t2;
  delete t3;
  delete t4;
  delete t5;
  delete t6;
  delete t7;
  delete t8;
  delete t9;
}
