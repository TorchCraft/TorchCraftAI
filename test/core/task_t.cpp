/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "state.h"
#include "task.h"
#include "tracker.h"

using namespace cherrypi;

namespace {

class MockTracker : public Tracker {
 public:
  MockTracker(TrackerStatus target) : Tracker(100), target_(target) {}

 protected:
  // Simply advance to next state, and finally to target state
  bool updateNotTracking(State*) override {
    status_ = TrackerStatus::Pending;
    return true;
  };
  bool updatePending(State*) override {
    status_ = TrackerStatus::Ongoing;
    return true;
  };
  bool updateOngoing(State*) override {
    status_ = target_;
    return true;
  };

  TrackerStatus target_;
};

class MockTask : public Task {
 public:
  MockTask(int upcId) : Task(upcId) {}
};

} // namespace

CASE("task/proxy/status_unknown") {
  // ProxyTask status is unknown with unavailable underlying task
  State state(std::make_shared<tc::Client>());
  auto ptask = std::make_shared<ProxyTask>(0, 1);
  state.board()->postTask(ptask, nullptr);
  EXPECT(ptask->status() == TaskStatus::Unknown);
  state.update();
  EXPECT(ptask->status() == TaskStatus::Unknown);
}

CASE("task/proxy/status") {
  State state(std::make_shared<tc::Client>());
  auto ptask = std::make_shared<ProxyTask>(0, 1);
  state.board()->postTask(ptask, nullptr);

  auto mtask = std::make_shared<MockTask>(0);
  state.board()->postTask(mtask, nullptr);

  state.update();
  EXPECT(mtask->status() == TaskStatus::Unknown);
  EXPECT(ptask->status() == TaskStatus::Unknown);

  mtask->setStatus(TaskStatus::Ongoing);
  state.update();
  EXPECT(mtask->status() == TaskStatus::Ongoing);
  EXPECT(ptask->status() == TaskStatus::Ongoing);

  mtask->setStatus(TaskStatus::Success);
  state.update();
  EXPECT(mtask->status() == TaskStatus::Success);
  EXPECT(ptask->status() == TaskStatus::Success);

  mtask->setStatus(TaskStatus::Failure);
  state.update();
  EXPECT(mtask->status() == TaskStatus::Failure);
  EXPECT(ptask->status() == TaskStatus::Failure);
}

CASE("task/multiproxy/default_policy") {
  State state(std::make_shared<tc::Client>());
  auto proxied = {1, 2, 3, 4, 5};
  auto ptask = std::make_shared<MultiProxyTask>(proxied, 0);
  state.board()->postTask(ptask, nullptr);

  // No task has not been posted yet, status should be unknown
  state.update();
  EXPECT(ptask->status() == TaskStatus::Unknown);

  std::vector<std::shared_ptr<MockTask>> mtasks = {
      std::make_shared<MockTask>(1),
      std::make_shared<MockTask>(2),
      std::make_shared<MockTask>(3),
      std::make_shared<MockTask>(4),
      std::make_shared<MockTask>(5),
  };
  for (auto& task : mtasks) {
    state.board()->postTask(task, nullptr);
  }

  // Tasks have been posted, but still with old status
  state.update();
  EXPECT(ptask->status() == TaskStatus::Unknown);

  // Single one switches to Ongoing
  mtasks[0]->setStatus(TaskStatus::Ongoing);
  state.update();
  EXPECT(ptask->status() == TaskStatus::Ongoing);

  // All to Ongoing
  for (auto& task : mtasks) {
    task->setStatus(TaskStatus::Ongoing);
  }
  state.update();
  EXPECT(ptask->status() == TaskStatus::Ongoing);

  // One to Success: need all to succeed
  mtasks[2]->setStatus(TaskStatus::Success);
  state.update();
  EXPECT(ptask->status() == TaskStatus::Ongoing);

  // One to Failure: status is reflected
  mtasks[1]->setStatus(TaskStatus::Failure);
  state.update();
  EXPECT(ptask->status() == TaskStatus::Failure);

  // All to Success
  for (auto& task : mtasks) {
    task->setStatus(TaskStatus::Success);
  }
  state.update();
  EXPECT(ptask->status() == TaskStatus::Success);

  // All to Failure
  for (auto& task : mtasks) {
    task->setStatus(TaskStatus::Failure);
  }
  state.update();
  EXPECT(ptask->status() == TaskStatus::Failure);
}

CASE("task/multiproxy/match_most") {
  State state(std::make_shared<tc::Client>());
  auto proxied = {1, 2, 3, 4, 5};
  auto ptask = std::make_shared<MultiProxyTask>(proxied, 0);
  ptask->setPolicyForStatus(TaskStatus::Ongoing, ProxyPolicy::MOST);
  ptask->setPolicyForStatus(TaskStatus::Success, ProxyPolicy::MOST);
  state.board()->postTask(ptask, nullptr);

  // No task has not been posted yet, status should be unknown
  state.update();
  EXPECT(ptask->status() == TaskStatus::Unknown);

  std::vector<std::shared_ptr<MockTask>> mtasks = {
      std::make_shared<MockTask>(1),
      std::make_shared<MockTask>(2),
      std::make_shared<MockTask>(3),
      std::make_shared<MockTask>(4),
      std::make_shared<MockTask>(5),
  };
  for (auto& task : mtasks) {
    state.board()->postTask(task, nullptr);
  }

  // All in Unknown
  state.update();
  EXPECT(ptask->status() == TaskStatus::Unknown);

  // 1 in Ongoing: not yet
  mtasks[0]->setStatus(TaskStatus::Ongoing);
  state.update();
  EXPECT(ptask->status() == TaskStatus::Unknown);

  // 2 in Ongoing: not yet
  mtasks[1]->setStatus(TaskStatus::Ongoing);
  state.update();
  EXPECT(ptask->status() == TaskStatus::Unknown);

  // 3 in Ongoing: switch
  mtasks[4]->setStatus(TaskStatus::Ongoing);
  state.update();
  EXPECT(ptask->status() == TaskStatus::Ongoing);

  // One to Success: need all to succeed
  mtasks[2]->setStatus(TaskStatus::Success);
  state.update();
  EXPECT(ptask->status() == TaskStatus::Ongoing);

  // Two to Success: need all to succeed, ongoing does not cover most any more
  mtasks[1]->setStatus(TaskStatus::Success);
  state.update();
  EXPECT(ptask->status() == TaskStatus::Unknown);

  // 3 in Success: good!
  mtasks[0]->setStatus(TaskStatus::Success);
  state.update();
  EXPECT(ptask->status() == TaskStatus::Success);
}
