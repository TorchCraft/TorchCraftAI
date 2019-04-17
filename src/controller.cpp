/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "controller.h"

#include "utils.h"

#include <glog/logging.h>

namespace cherrypi {

ControllerBase::ControllerBase(Module* module) : module_(module) {}

void ControllerBase::addUnit(State* state, Unit* unit, UpcId id) {
  units_[unit] = id;
}

void ControllerBase::removeUnit(State* state, Unit* unit, UpcId id) {
  if (!isControllingUnitWith(unit, id)) {
    auto it = units_.find(unit);
    if (it == units_.end()) {
      VLOG(0) << "Attempting to remove unit " << utils::unitString(unit)
              << " via " << utils::upcString(id) << " but unit not controlled";
    } else if (it->second != id) {
      LOG(WARNING) << "Attempting to remove unit " << utils::unitString(unit)
                   << " via " << utils::upcString(id)
                   << " but internal UPC ID differs: "
                   << utils::upcString(it->second);
    }
    return;
  }

  // Remove unit from internal mapping as well as any scheduled UPCs for it.
  units_.erase(unit);
  upcs_.erase(unit);
}

bool ControllerBase::keepUnit(State* state, Unit* unit) const {
  if (unit->dead) {
    VLOG(3) << utils::unitString(unit) << " is dead, don't keep it";
    return false;
  }
  if (!unit->isMine) {
    VLOG(3) << utils::unitString(unit) << " is not mine, don't keep it";
    return false;
  }
  return true;
}

void ControllerBase::step(State* state) {}

void ControllerBase::postUpcs(State* state) {
  // Post UPCs produced in step above.
  auto board = state->board();
  for (auto& it : upcs_) {
    board->postUPC(std::move(it.second.second), it.second.first, module_);
  }
  upcs_.clear();
}

bool ControllerBase::isControllingUnitWith(Unit* unit, UpcId id) const {
  auto it = units_.find(unit);
  if (it == units_.end()) {
    return false;
  }
  return it->second == id;
}

Controller::Controller(Module* module) : ControllerBase(module) {}

bool Controller::didFail() const {
  if (units_.empty()) {
    VLOG(4) << "Lost all units, controller considered failed";
    return true;
  }
  return false;
}

void Controller::setUpcId(UpcId id) {
  upcId_ = id;
}

ControllerTask::ControllerTask(
    UpcId upcId,
    std::unordered_set<Unit*> units,
    State* state,
    std::shared_ptr<Controller> controller)
    : Task(upcId, std::move(units)), controller_(std::move(controller)) {
  for (Unit* unit : this->units()) {
    controller_->addUnit(state, unit, upcId);
  }
  controller_->setUpcId(upcId);
  setStatus(TaskStatus::Ongoing);
}

void ControllerTask::update(State* state) {
  if (status() != TaskStatus::Ongoing) {
    return;
  }

  // Remove units that we don't longer can or want to keep
  for (auto it = units().begin(); it != units().end();) {
    Unit* unit = *it;
    bool remove = false;
    if (state->board()->taskWithUnit(unit).get() != this) {
      remove = true;
    } else if (!controller_->keepUnit(state, unit)) {
      remove = true;
    }

    if (remove) {
      if (controller_->isControllingUnitWith(unit, upcId())) {
        controller_->removeUnit(state, unit, upcId());
      }
      units().erase(it++);
    } else {
      ++it;
    }
  }

  if (controller_->didSucceed()) {
    VLOG(4) << "Controller reported success, marking task "
            << utils::upcString(upcId()) << " as succeeded";
    setStatus(TaskStatus::Success);
  } else if (controller_->didFail()) {
    VLOG(4) << "Controller reported failure, marking task "
            << utils::upcString(upcId()) << " as failed";
    setStatus(TaskStatus::Failure);
  }
}

void ControllerTask::cancel(State* state) {
  if (status() != TaskStatus::Ongoing) {
    return;
  }

  // TODO: Maybe no need to shut down controller cleanly?
  for (Unit* unit : units()) {
    if (controller_->isControllingUnitWith(unit, upcId())) {
      controller_->removeUnit(state, unit, upcId());
    }
  }
  units().clear();
  Task::cancel(state);
}

std::shared_ptr<Controller> ControllerTask::controller() const {
  return controller_;
}

SharedControllerTask::SharedControllerTask(
    UpcId upcId,
    std::unordered_set<Unit*> units,
    State* state,
    std::shared_ptr<SharedController> controller)
    : Task(upcId, std::move(units)), controller_(std::move(controller)) {
  for (Unit* unit : this->units()) {
    controller_->addUnit(state, unit, upcId);
  }
  setStatus(TaskStatus::Ongoing);
}

void SharedControllerTask::update(State* state) {
  if (status() != TaskStatus::Ongoing) {
    return;
  }

  // Remove units that we don't longer can or want to keep
  for (auto it = units().begin(); it != units().end();) {
    Unit* unit = *it;
    bool remove = false;
    if (state->board()->taskWithUnit(unit).get() != this) {
      remove = true;
    } else if (!controller_->keepUnit(state, unit)) {
      remove = true;
    }

    if (remove) {
      if (controller_->isControllingUnitWith(unit, upcId())) {
        controller_->removeUnit(state, unit, upcId());
      }
      units().erase(it++);
    } else {
      ++it;
    }
  }

  if (units().empty()) {
    setStatus(TaskStatus::Failure);
  }
}

void SharedControllerTask::cancel(State* state) {
  if (status() != TaskStatus::Ongoing) {
    return;
  }

  for (Unit* unit : units()) {
    if (controller_->isControllingUnitWith(unit, upcId())) {
      controller_->removeUnit(state, unit, upcId());
    }
  }
  units().clear();
  Task::cancel(state);
}

std::shared_ptr<SharedController> SharedControllerTask::controller() const {
  return controller_;
}

} // namespace cherrypi
