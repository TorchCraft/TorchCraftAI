/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "task.h"

#include "state.h"
#include "tracker.h"
#include "utils.h"

#include <glog/logging.h>

namespace cherrypi {

void Task::cancel(State* state) {
  VLOG(1) << "Task " << utils::upcString(upcId_) << " cancelled";
  setStatus(TaskStatus::Cancelled);
}

void Task::removeUnit(Unit* unit) {
  auto it = units_.find(unit);
  if (it == units_.end()) {
    LOG(ERROR) << "Unit " << utils::unitString(unit) << " not found in task "
               << utils::upcString(upcId_);
    return;
  }

  units_.erase(it);
  VLOG(2) << "Remove unit " << utils::unitString(unit) << " from task "
          << utils::upcString(upcId_);
}

void Task::removeDeadOrReassignedUnits(State* state) {
  // We can't split this up really since the blackboard will automatically
  // unassign units that have died.
  auto board = state->board();
  for (auto it = units_.begin(); it != units_.end();) {
    if (board->taskWithUnit(*it).get() != this) {
      units_.erase(it++);
    } else {
      ++it;
    }
  }
}

ProxyTask::ProxyTask(UpcId targetUpcId, UpcId upcId)
    : Task(upcId), targetUpcId_(targetUpcId) {}

void ProxyTask::update(State* state) {
  if (target_ == nullptr) {
    target_ = state->board()->taskForId(targetUpcId_);
    if (target_ == nullptr) {
      return;
    } else {
      VLOG(1) << "Proxy: Found target task for UPC " << targetUpcId_;
    }
  }

  auto oldStatus = status();
  setStatus(target_->status());
  if (status() != oldStatus) {
    VLOG(2) << "Task for UPC " << upcId()
            << ": status changed: " << utils::enumAsInt(oldStatus) << " -> "
            << utils::enumAsInt(status());
  }
}

void ProxyTask::cancel(State* state) {
  if (target_) {
    VLOG(2) << "ProxyTask cancelled -> cancelling proxied task for UPC "
            << target_->upcId();
    return target_->cancel(state);
  }
  VLOG(2) << "ProxyTask cancelled without proxy -> removing UPC "
          << targetUpcId_;
  state->board()->removeUPCs({targetUpcId_});
  Task::cancel(state);
}

std::unordered_set<Unit*> const& ProxyTask::proxiedUnits() const {
  if (target_ != nullptr) {
    return target_->proxiedUnits();
  }
  if (!units().empty()) {
    std::runtime_error("ProxyTask should not contain any units");
  }
  return units();
}

MultiProxyTask::MultiProxyTask(std::vector<UpcId> targetUpcIds, UpcId upcId)
    : Task(upcId), targetUpcIds_(std::move(targetUpcIds)) {
  targets_.insert(targets_.begin(), targetUpcIds_.size(), nullptr);
  policy_[TaskStatus::Unknown] = ProxyPolicy::ALL;
  policy_[TaskStatus::Ongoing] = ProxyPolicy::ANY;
  policy_[TaskStatus::Failure] = ProxyPolicy::ANY;
  policy_[TaskStatus::Cancelled] = ProxyPolicy::ALL;
  policy_[TaskStatus::Success] = ProxyPolicy::ALL;
}

void MultiProxyTask::setPolicyForStatus(TaskStatus status, ProxyPolicy policy) {
  policy_[status] = policy;
}

void MultiProxyTask::update(State* state) {
  auto board = state->board();
  for (size_t i = 0; i < targetUpcIds_.size(); i++) {
    auto& target = targets_[i];
    if (target == nullptr) {
      target = board->taskForId(targetUpcIds_[i]);
      if (target != nullptr) {
        VLOG(1) << "Multiproxy: found target task for UPC " << targetUpcIds_[i];
      }
    }
  }

  // Update status according to policy
  auto oldStatus = status();
  setStatus(TaskStatus::Unknown); // Fallback
  for (auto status : {TaskStatus::Success,
                      TaskStatus::Cancelled,
                      TaskStatus::Failure,
                      TaskStatus::Ongoing,
                      TaskStatus::Unknown}) {
    if (matchStatus(status)) {
      if (status != oldStatus) {
        VLOG(2) << "MultiProxy: change status to " << UpcId(status);
      }
      setStatus(status);
      break;
    }
  }

  // Update list of proxied units
  proxiedUnits_.clear();
  for (auto& target : targets_) {
    if (target != nullptr) {
      for (auto unit : target->proxiedUnits()) {
        proxiedUnits_.insert(unit);
      }
    }
  }
}

void MultiProxyTask::cancel(State* state) {
  auto board = state->board();
  VLOG(2) << "MultiProxy: canceling task with " << targetUpcIds_.size()
          << " UPCs";
  for (size_t i = 0; i < targetUpcIds_.size(); i++) {
    auto& target = targets_[i];
    if (target) {
      target->cancel(state);
      VLOG(2) << "MultiProxyTask canceled -> canceling proxy task for upc "
              << target->upcId();
    } else {
      board->removeUPCs({targetUpcIds_[i]});
      VLOG(2) << "MultiProxyTask canceled -> removing UPC without proxy task "
              << targetUpcIds_[i];
    }
  }
  // mark unproxied tasks as canceled by default
  defaultTargetStatus_ = TaskStatus::Cancelled;

  Task::cancel(state);
}

std::unordered_set<Unit*> const& MultiProxyTask::proxiedUnits() const {
  return proxiedUnits_;
}

bool MultiProxyTask::matchStatus(TaskStatus status) {
  auto policy = policy_[status];
  auto getStatus = [&](auto& task) {
    if (task == nullptr) {
      return defaultTargetStatus_;
    }
    return task->status();
  };

  if (policy == ProxyPolicy::ANY) {
    for (auto& target : targets_) {
      if (getStatus(target) == status) {
        return true;
      }
    }
    return false;
  } else if (policy == ProxyPolicy::MOST) {
    size_t n = 0;
    for (auto& target : targets_) {
      if (getStatus(target) == status) {
        n++;
      }
    }
    return n > targets_.size() / 2;
  } else if (policy == ProxyPolicy::ALL) {
    for (auto& target : targets_) {
      if (getStatus(target) != status) {
        return false;
      }
    }
    return true;
  }

  return false;
}

} // namespace cherrypi
