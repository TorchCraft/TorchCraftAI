/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "controller.h"

using namespace cherrypi;

namespace {

class TestModule : public Module {
 public:
  TestModule() : Module() {
    setName("TestModule");
  }
  virtual ~TestModule() = default;
};

class TestController : public Controller {
 public:
  using Controller::Controller;
  virtual ~TestController() = default;

  bool succeeded = false;
  bool failed = false;

  virtual bool didSucceed() const override {
    return succeeded;
  }
  virtual bool didFail() const override {
    return failed;
  }
  std::unordered_set<Unit*> units() const {
    std::unordered_set<Unit*> u;
    for (auto& it : units_) {
      u.insert(it.first);
    }
    return u;
  }
};

class TestSharedController : public SharedController {
 public:
  using SharedController::SharedController;
  virtual ~TestSharedController() = default;

  std::unordered_set<Unit*> units() const {
    std::unordered_set<Unit*> u;
    for (auto& it : units_) {
      u.insert(it.first);
    }
    return u;
  }
  std::unordered_set<Task*> tasks(State* state) const {
    std::unordered_set<Task*> t;
    for (auto& it : units_) {
      t.insert(state->board()->taskForId(it.second).get());
    }
    return t;
  }
};

} // namespace

CASE("controller/status") {
  State state(std::make_shared<tc::Client>());
  Unit unit;
  unit.isMine = true;
  TestModule module;

  auto controller = std::make_shared<TestController>(&module);
  auto task = std::make_shared<ControllerTask>(
      kRootUpcId, std::initializer_list<Unit*>{&unit}, &state, controller);
  state.board()->postTask(task, &module);

  EXPECT(task->status() == TaskStatus::Ongoing);

  controller->succeeded = false;
  controller->failed = true;
  state.update();
  EXPECT(task->status() == TaskStatus::Failure);

  task->setStatus(TaskStatus::Ongoing);
  controller->succeeded = true;
  controller->failed = false;
  state.update();
  EXPECT(task->status() == TaskStatus::Success);

  task->setStatus(TaskStatus::Ongoing);
  EXPECT(task->status() == TaskStatus::Ongoing);
  controller->succeeded = false;
  controller->failed = false;
  state.update();
  EXPECT(task->status() == TaskStatus::Ongoing);

  task->cancel(&state);
  EXPECT(task->status() == TaskStatus::Cancelled);
}

CASE("controller/shared_global_instance") {
  State state(std::make_shared<tc::Client>());
  TestModule module;

  auto ctrl1 =
      SharedController::globalInstance<SharedController>(&state, &module);
  auto ctrl2 =
      SharedController::globalInstance<SharedController>(&state, &module);
  EXPECT(ctrl1.get() == ctrl2.get());

  auto ctrl3 =
      SharedController::globalInstance<SharedController>(&state, &module, "c3");
  auto ctrl4 =
      SharedController::globalInstance<SharedController>(&state, &module, "c4");
  EXPECT(ctrl1.get() != ctrl3.get());
  EXPECT(ctrl1.get() != ctrl4.get());
  EXPECT(ctrl3.get() != ctrl4.get());
}

CASE("controller/unitallocation") {
  // Verifies consistent unit allocation across Controller and ControllerTask
  State state(std::make_shared<tc::Client>());
  TestModule module;
  std::vector<Unit> units{6};
  std::unordered_set<Unit*> uset;
  for (size_t i = 0; i < units.size(); i++) {
    units[i].id = int(i + 1);
    units[i].isMine = true;
    uset.insert(&units[i]);
  }
  units[4].isMine = false; // One non-allied unit from the start
  units[5].dead = true; // One dead unit from the start

  auto controller = std::make_shared<TestController>(&module);
  auto task =
      std::make_shared<ControllerTask>(kRootUpcId, uset, &state, controller);
  ControllerTask const* ctask = const_cast<ControllerTask const*>(task.get());
  state.board()->postTask(task, &module);

  state.update();
  controller->step(&state);

  // Four units in controller (2 were unavailable from the start)
  EXPECT(controller->units().size() == 4u);
  EXPECT(controller->units() == ctask->units());

  // Have one unit die
  units[0].dead = true;
  state.update();
  controller->step(&state);
  EXPECT(controller->units().size() == 3u);
  EXPECT(controller->units() == ctask->units());

  // Have one unit switch sides
  units[1].dead = true;
  state.update();
  controller->step(&state);
  EXPECT(controller->units().size() == 2u);
  EXPECT(controller->units() == ctask->units());

  // Have a unit be re-allocated
  state.board()->postTask(
      std::make_shared<Task>(
          kRootUpcId + 1, std::initializer_list<Unit*>{&units[2]}),
      &module);
  state.update();
  controller->step(&state);
  EXPECT(controller->units().size() == 1u);
  EXPECT(controller->units() == ctask->units());

  // Cancalling the task also removes the units from the controller
  task->cancel(&state);
  EXPECT(controller->units().size() == 0u);
  EXPECT(controller->units() == ctask->units());
}

CASE("controller/unitallocation_shared") {
  // Verifies consistent unit allocation across SharedController and
  // SharedControllerTask
  State state(std::make_shared<tc::Client>());
  TestModule module;
  std::vector<Unit> units{5};
  for (size_t i = 0; i < units.size(); i++) {
    units[i].id = int(i + 1);
    units[i].isMine = true;
  }

  auto controller =
      SharedController::globalInstance<TestSharedController>(&state, &module);

  // Add task with units 1-2
  auto task1 = std::make_shared<SharedControllerTask>(
      1,
      std::initializer_list<Unit*>{&units[0], &units[1]},
      &state,
      controller);
  state.board()->postTask(task1, &module);

  state.update();
  controller->step(&state);
  EXPECT(controller->units().size() == 2u);
  EXPECT(controller->tasks(&state).size() == 1u);

  // Kill first unit
  units[0].dead = true;
  state.update();
  controller->step(&state);
  EXPECT(controller->units().size() == 1u);
  EXPECT(controller->tasks(&state).size() == 1u);

  // Add task with unit 3
  auto task2 = std::make_shared<SharedControllerTask>(
      2, std::initializer_list<Unit*>{&units[2]}, &state, controller);
  state.board()->postTask(task2, &module);

  state.update();
  controller->step(&state);
  EXPECT(controller->units().size() == 2u);
  EXPECT(controller->tasks(&state).size() == 2u);

  // Post another task with units 2 and 3, effectively replacing the previous
  // two tasks
  auto task3 = std::make_shared<SharedControllerTask>(
      3,
      std::initializer_list<Unit*>{&units[1], &units[2]},
      &state,
      controller);
  state.board()->postTask(task3, &module);
  EXPECT(state.board()->taskWithUnit(&units[1]) == task3);
  EXPECT(state.board()->taskWithUnit(&units[2]) == task3);

  state.update();
  controller->step(&state);
  EXPECT(state.board()->taskWithUnit(&units[1]) == task3);
  EXPECT(state.board()->taskWithUnit(&units[2]) == task3);
  EXPECT(controller->units().size() == 2u);
  EXPECT(controller->tasks(&state).size() == 1u);
  EXPECT(*controller->tasks(&state).begin() == task3.get());

  // Cancel task 2, should not have any effect on controller
  task2->cancel(&state);
  EXPECT(controller->units().size() == 2u);
  EXPECT(controller->tasks(&state).size() == 1u);
  state.update();
  controller->step(&state);
  EXPECT(controller->units().size() == 2u);
  EXPECT(controller->tasks(&state).size() == 1u);
  EXPECT(*controller->tasks(&state).begin() == task3.get());

  // Post task with units 4-5
  auto task4 = std::make_shared<SharedControllerTask>(
      4,
      std::initializer_list<Unit*>{&units[3], &units[4]},
      &state,
      controller);
  state.board()->postTask(task4, &module);

  state.update();
  controller->step(&state);
  EXPECT(controller->units().size() == 4u);
  EXPECT(controller->tasks(&state).size() == 2u);

  // Cancel task3, effictively removing units 2 and 3
  task3->cancel(&state);
  EXPECT(controller->units().size() == 2u);
  EXPECT(controller->tasks(&state).size() == 1u);
  // Cancelling twice does not screw up things either
  task3->cancel(&state);
  EXPECT(controller->units().size() == 2u);
  EXPECT(controller->tasks(&state).size() == 1u);
  state.update();
  controller->step(&state);
  EXPECT(controller->units().size() == 2u);
  EXPECT(controller->tasks(&state).size() == 1u);
  EXPECT(*controller->tasks(&state).begin() == task4.get());
}
