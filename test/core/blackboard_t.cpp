/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "blackboard.h"
#include "module.h"
#include "state.h"

using namespace cherrypi;

namespace {

class MockModule : public Module {
 public:
  MockModule() : Module() {}
  void step(State* s) override {}
};

// Immediately changes to specified status in update()
class MockStatusTask : public Task {
 public:
  MockStatusTask(
      TaskStatus status,
      int upcId,
      std::unordered_set<Unit*> units = {})
      : Task(upcId, std::move(units)) {
    tstatus_ = status;
  }

  void update(State*) override {
    setStatus(tstatus_);
  }

  TaskStatus tstatus_;
};

} // namespace

CASE("blackboard/kv_storage") {
  State state(std::make_shared<tc::Client>());
  Blackboard* board = state.board();
  EXPECT(board->hasKey("test") == false);
  board->post("test", 123);
  EXPECT(board->hasKey("test") == true);
  EXPECT(board->get<int>("test") == 123);
  board->post("test", 456);
  EXPECT(board->get<int>("test") == 456);

  board->post("string", std::string("foo"));
  EXPECT(board->get<std::string>("string") == "foo");
}

CASE("blackboard/upc_storage") {
  State state(std::make_shared<tc::Client>());
  Blackboard* board = state.board();
  MockModule module1, module2;

  // Monotonically increasing IDs
  auto id1 = board->postUPC(std::make_shared<UPCTuple>(), kRootUpcId, &module2);
  auto id2 = board->postUPC(std::make_shared<UPCTuple>(), kRootUpcId, &module2);
  EXPECT(board->upcs().size() == size_t(2));
  EXPECT(id2 > id1);
  auto id3 = board->postUPC(std::make_shared<UPCTuple>(), kRootUpcId, &module1);
  EXPECT(board->upcs().size() == size_t(3));
  EXPECT(id3 > id2);

  // upcsFrom()
  EXPECT(board->upcsFrom(&module1).size() == size_t(1));
  EXPECT(board->upcsFrom(&module2).size() == size_t(2));

  // upcsWith{Sharp,}Command()
  auto sharpC = std::make_shared<UPCTuple>();
  sharpC->command[Command::Move] = 1.0f;
  auto notSoSharpC = std::make_shared<UPCTuple>();
  notSoSharpC->command[Command::Move] = 0.8f;
  auto idSharp = board->postUPC(std::move(sharpC), kRootUpcId, &module1);
  auto idNSSharp = board->postUPC(std::move(notSoSharpC), kRootUpcId, &module1);
  EXPECT(board->upcsWithSharpCommand(Command::Move).size() == size_t(1));
  EXPECT(board->upcsWithSharpCommand(Command::Move).begin()->first == idSharp);
  EXPECT(board->upcsWithCommand(Command::Move, 0.5f).size() == size_t(2));
  EXPECT(board->upcsWithCommand(Command::Move, 0.9f).size() == size_t(1));
  auto all = board->upcs();
  EXPECT(all.find(idNSSharp) != all.end());

  // Consumption
  board->consumeUPCs({id1, id2}, &module1);
  EXPECT(board->upcs().size() == all.size() - 2);
  EXPECT(board->upcsFrom(&module2).size() == size_t(0));
}

CASE("blackboard/command_storage") {
  State state(std::make_shared<tc::Client>());
  Blackboard* board = state.board();
  std::vector<tc::Client::Command> commands;
  for (int i = 0; i < 20; i++) {
    state.update();

    commands.emplace_back(
        tc::BW::Command::CommandUnit,
        i,
        tc::BW::UnitCommandType::Train,
        0,
        0,
        0,
        tc::BW::UnitType::Terran_Marine);
    for (auto& comm : commands) {
      board->postCommand(comm, kRootUpcId);
    }

    EXPECT(board->commands().size() == commands.size());
    for (int j = 0; j < std::min(i, 3); j++) {
      EXPECT(board->commands(j).size() == commands.size() - j);
    }
  }
}

CASE("blackboard/task_storage") {
  State state(std::make_shared<tc::Client>());
  Blackboard* board = state.board();
  MockModule module1, module2;
  Unit unit1, unit2;
  auto task1 = std::shared_ptr<Task>(new Task(1, {&unit1, &unit2}));
  auto task2 = std::shared_ptr<Task>(new Task(2));
  board->postTask(task1, &module1);
  board->postTask(task2, &module2);

  EXPECT(board->taskForId(1) == task1);
  EXPECT(board->taskForId(2) == task2);
  EXPECT(board->tasksOfModule(&module1).size() == size_t(1));
  EXPECT(board->tasksOfModule(&module1)[0] == task1);
  EXPECT(board->tasksOfModule(&module2).size() == size_t(1));
  EXPECT(board->tasksOfModule(&module2)[0] == task2);
  EXPECT(board->taskWithUnit(&unit1) == task1);
  EXPECT(board->taskWithUnit(&unit2) == task1);

  // Multiple tasks for module2, unit2 re-assigned
  auto task3 = std::shared_ptr<Task>(new Task(3, {&unit2}));
  board->postTask(task3, &module2);
  EXPECT(board->tasksOfModule(&module2).size() == size_t(2));
  EXPECT(board->taskWithUnit(&unit1) == task1);
  EXPECT(board->taskWithUnit(&unit2) == task3);

  // Multiple tasks with same ID will throw
  auto task4 = std::shared_ptr<Task>(new Task(3));
  EXPECT_THROWS(board->postTask(task4, &module2));
  EXPECT(board->taskForId(3) == task3);

  // Mark task for removal removes at next update
  board->markTaskForRemoval(1);
  state.update();
  EXPECT(board->taskForId(1) == nullptr);
  EXPECT(board->taskForId(2) == task2);
  EXPECT(board->taskWithUnit(&unit1) == nullptr);
  EXPECT(board->taskWithUnit(&unit2) == task3);

  board->markTaskForRemoval(task2);
  state.update();
  EXPECT(board->taskForId(2) == nullptr);
}

CASE("blackboard/task_autoremoval") {
  State state(std::make_shared<tc::Client>());
  Blackboard* board = state.board();
  MockModule module;
  Unit unit1, unit2, unit3;
  auto task1 = std::shared_ptr<Task>(
      new MockStatusTask(TaskStatus::Ongoing, 1, {&unit1}));
  auto task2 = std::shared_ptr<Task>(
      new MockStatusTask(TaskStatus::Success, 2, {&unit2}));
  auto task3 = std::shared_ptr<Task>(
      new MockStatusTask(TaskStatus::Failure, 3, {&unit3}));
  board->postTask(task1, &module, true);
  board->postTask(task2, &module, true);
  board->postTask(task3, &module, true);
  EXPECT(board->tasksOfModule(&module).size() == size_t(3));

  // First update: update task status
  state.update();
  EXPECT(board->tasksOfModule(&module).size() == size_t(3));

  // Second update: tasks are actually removed
  state.update();
  EXPECT(board->tasksOfModule(&module).size() == size_t(1));
  EXPECT(board->tasksOfModule(&module)[0] == task1);
  EXPECT(board->taskWithUnit(&unit1) == task1);
  EXPECT(board->taskWithUnit(&unit2) == nullptr);
  EXPECT(board->taskWithUnit(&unit3) == nullptr);
}

CASE("blackboard/filter_invalid_upc") {
  State state(std::make_shared<tc::Client>());
  Blackboard* board = state.board();
  MockModule module1, module2;

  // invalid unit
  auto upc = std::make_shared<UPCTuple>();
  upc->unit[nullptr] = 1;
  EXPECT(upc->unit.size() == 1u);
  auto id = board->postUPC(std::move(upc), kRootUpcId, &module2);
  EXPECT(board->upcs().count(id) == 1u);
  EXPECT(board->upcs().at(id)->unit.size() == 0u);

  // invalid target unit
  upc = std::make_shared<UPCTuple>();
  upc->position = UPCTuple::UnitMap{{nullptr, 1}};
  EXPECT(upc->position.get<UPCTuple::UnitMap>().size() == 1u);
  id = board->postUPC(std::move(upc), kRootUpcId, &module2);
  EXPECT(board->upcs().count(id) == 1u);
  EXPECT(board->upcs().at(id)->position.is<UPCTuple::Empty>() == true);

  // invalid createtype
  upc = std::make_shared<UPCTuple>();
  upc->state = UPCTuple::BuildTypeMap{{nullptr, 1}};
  EXPECT(upc->state.get<UPCTuple::BuildTypeMap>().size() == 1u);
  id = board->postUPC(std::move(upc), kRootUpcId, &module2);
  EXPECT(board->upcs().count(id) == 1u);
  EXPECT(board->upcs().at(id)->state.is<UPCTuple::Empty>() == true);
}
