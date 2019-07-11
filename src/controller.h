/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "cherrypi.h"
#include "module.h"
#include "state.h"
#include "task.h"
#include "utils.h"

namespace cherrypi {

/**
 * Base class for controllers.
 *
 * A Controller is a mix between a Task and a Module: it has a sense of unit
 * ownership and contains state similar to Task, and has a step() function which
 * is used to post UPCs to Blackboard similar to Module. It is tailored to
 * control of individual units: units can be added or removed, and per-unit UPC
 * posting is made easy.
 *
 * For using a controller, Module and Task objects still required. Module create
 * controller instances and call step(). Tasks take care of player-wide unit
 * allocation via Blackboard and provide source UPCs for each unit being
 * controller.
 *
 * See Controller and SharedController for usage exampels of two common
 * controller patterns.
 */
class ControllerBase {
 public:
  ControllerBase(Module* module);
  virtual ~ControllerBase() = default;

  /// Add a unit to this controller.
  /// This is usually called whenever a new Task for a controller is being
  /// cretaed.
  /// Re-implement this function if you need to update internal data structures
  /// when gaining control of units but make sure to also call the base class
  /// method.
  virtual void addUnit(State* state, Unit* unit, UpcId id);

  /// Remove a unit from this controller.
  /// This is usually called from Task::update() to remove units that were
  /// assigned to other Tasks, or for which keepUnit() returns false.
  /// Re-implement this function if you need to update internal data structures
  /// when gaining control of units but make sure to also call the base class
  /// method.
  virtual void removeUnit(State* state, Unit* unit, UpcId id);

  /// Decide whether to keep a unit.
  /// By default, this returns false for dead and non-allied units.
  virtual bool keepUnit(State* state, Unit* unit) const;

  /// Advance controller state and produce UPCs.
  /// This is intended to be called from Module::step() of the instantiating
  /// module.
  /// The default implementation does nothing.
  virtual void step(State* state);

  /// Checks if the controller is controlling the given unit via the given UPC
  /// ID.
  /// Tasks are required to call this function before calling removeUnit() when
  /// removing units from controllers.
  bool isControllingUnitWith(Unit* unit, UpcId id) const;

  /// A name for this Controller, for debugging purposes
  virtual const char* getName() const {
    return "Controller";
  };

 protected:
  /// Posts scheduled UPCs to the Blackboard.
  /// UPCs can be scheduled by addUpc().
  void postUpcs(State* state);

  /// Schedules an action (as a UPC) for the given unit which will be posted
  /// after doStep().
  template <typename... Args>
  void addUpc(Unit* unit, Args&&... args) {
    if (upcs_.find(unit) != upcs_.end()) {
      LOG(WARNING) << "Duplicate UPC for unit " << utils::unitString(unit);
    }
    if (units_.find(unit) == units_.end()) {
      LOG(WARNING) << "Not controlling unit " << utils::unitString(unit);
      return;
    }

    // TODO
    auto sourceId = units_[unit];
    if (sourceId == kInvalidUpcId) {
      return;
    }

    auto upc = utils::makeSharpUPC(unit, std::forward<Args>(args)...);
    upcs_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(unit),
        std::forward_as_tuple(sourceId, std::move(upc)));
  }

 protected:
  Module* module_;
  std::unordered_map<Unit*, UpcId> units_;
  std::unordered_map<Unit*, std::pair<UpcId, std::shared_ptr<UPCTuple>>> upcs_;
};

/**
 * Base class for single-task controllers.
 *
 * This class models a 1:1 relationship with an accompanying Task. Units are
 * added to the controller when the respective task object (ControllerTask) is
 * created.
 *
 * Controller provides two additional virtual function that can be
 * re-implemented by sub-classes: didSucceed() and didFail(). These will be used
 * by the accompanying Task object to update its status. If your controller
 * returns true for one of these functions, the accompanying Task will end and
 * the controller is free to be disposed of and should not be stepped through
 * any more.
 *
 * A typical Module::step() function with Controller objects might look
 * similar to this:
 *
```
void MyModule::step(State* state) {
  // For the current relevant UPCs on the Blackboard
  for (auto& it : relevantUpcs()) {
    auto upcId = it.first;
    auto& upc = it.second;
    board->consumeUPC(upcId, this);

    // Select units from upc.unit
    auto units = sampleUnits(upc);

    // Create a new task with a new controller instance
    auto controller = std::make_shared<MyController>(this);
    auto task = std::make_shared<ControllerTask>(upcId, units, controller);
    board->postTask(task, this, true);
  }

  // Update active controllers
  for (auto& task : state->board()->tasksOfModule(this)) {
    auto ctask = std::static_pointer_cast<ControllerTask>(task);
    auto controller = ctask->controller();
    controller->step(state);
  }
}
```
 */
class Controller : public ControllerBase {
 public:
  Controller(Module* module);
  virtual ~Controller() = default;

  /// Implement this to return whether your custom Controller did succeed in its
  /// mission (if applicable) and can be disposed.
  /// By default, this returns false.
  virtual bool didSucceed() const {
    return false;
  }

  /// Implement this to return whether your custom Controller did fail in its
  /// mission (if applicable) and can be disposed.
  /// By default, a Controller fails if it does not control any units.
  virtual bool didFail() const;

  /// Set the UPC ID of the corresponding task.
  void setUpcId(UpcId id);

 protected:
  UpcId upcId_;
};

/**
 * Base class for Controllers shared between multiple tasks.
 *
 * A common pattern is the control of multiple units in a centralized fashion.
 * Since unit allocation is globally managed via Task objects which have a 1:1
 * relation to their respective UPCs, this requires handling multiple Task
 * objects.
 *
 * With SharedController and SharedControllerTask, this pattern can be
 * implemented quite easily by inheriting from SharedController. Typically, the
 * resulting code in Module::step() will look similar to this:
 *
```
void MyModule::step(State* state) {
  auto controller = SharedController::globalInstance<MyController>(state, this);

  // For the current relevant UPCs on the Blackboard
  for (auto& it : relevantUpcs()) {
    auto upcId = it.first;
    auto& upc = it.second;
    board->consumeUPC(upcId, this);

    // Select units from upc.unit
    auto units = sampleUnits(upc);

    // Create a new task and register it in the controller instance
    auto task = std::make_shared<SharedControllerTask>(upcId, units,
        controller);
    board->postTask(task, this, true);
  }

  controller->step(state);
}
```
 *
 */
class SharedController : public ControllerBase {
 public:
  using ControllerBase::ControllerBase;
  virtual ~SharedController() = default;

  /// Retrieves the global instance of a shared controller.
  /// Shared controllers can be stored in the Blackboard. This function will
  /// create the requested controller object if necessary (the Blackboard key
  /// is "controller_<module name>/<controller name>"
  template <typename T>
  static std::shared_ptr<T> globalInstance(
      State* state,
      Module* module,
      std::string name = std::string()) {
    auto board = state->board();
    auto key = std::string("controller_") + module->name() + "/" + name;
    auto controller =
        board->get<std::shared_ptr<SharedController>>(key, nullptr);
    if (controller == nullptr) {
      controller = std::make_shared<T>(module);
      board->post(key, controller);
    }
    return std::dynamic_pointer_cast<T>(controller);
  }
};

/**
 * Generic Task for Controller.
 *
 * Please see Controller for further details and a usage example.
 */
class ControllerTask : public Task {
 public:
  ControllerTask(
      UpcId upcId,
      std::unordered_set<Unit*> units,
      State* state,
      std::shared_ptr<Controller> controller);
  virtual ~ControllerTask() = default;

  virtual void update(State* state) override;
  virtual void cancel(State* state) override;

  std::shared_ptr<Controller> controller() const;
  virtual const char* getName() const override {
    return controller()->getName();
  };

 protected:
  std::shared_ptr<Controller> controller_;
};

/**
 * Generic Task for SharedController.
 *
 * Please see Controller for further details and a usage example.
 *
 * This task will enter failure state if there are no more units allocated to
 * it. In contrast to Controller, SharedController does not report any success
 * or failure status and the sole responsibility of this task is to keep track
 * of unit allocations. If there are no more units, this task's job is done.
 */
class SharedControllerTask : public Task {
 public:
  SharedControllerTask(
      UpcId upcId,
      std::unordered_set<Unit*> units,
      State* state,
      std::shared_ptr<SharedController> controller);
  virtual ~SharedControllerTask() = default;

  virtual void update(State* state) override;
  virtual void cancel(State* state) override;

  std::shared_ptr<SharedController> controller() const;

 protected:
  std::shared_ptr<SharedController> controller_;
};

} // namespace cherrypi
