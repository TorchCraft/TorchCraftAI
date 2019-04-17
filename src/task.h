/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <set>

#include "cherrypi.h"
#include "tracker.h"

namespace cherrypi {

class State;
struct Unit;

/**
 * Describes the current status of a task.
 */
enum class TaskStatus {
  Unknown = -1,
  /// Task is currently ongoing
  Ongoing,
  /// Task finished successfully
  Success,
  /// Task Canceled
  Cancelled,
  /// Task failed or was aborted
  Failure
};

/**
 * The primary way for modules to publish their activity.
 *
 * A task describes a particular activity of a module, likely spanning multiple
 * frames in the game. Tasks are meant for publishing module activity to the
 * blackboard. A typical use case would be that a given Module B acts on a
 * UPCTuple from Module A on the blackboard (i.e. it consumes it) and spawns a
 * corresponding task and possible further UPCTuples for lower-level Module
 * objects. Tasks are publicly available on the blackboard, which allows Module
 * A to track the execution of the UPCTuple that it spawned previously.
 *
 * Modules are encouraged to sub-class Task and add custom, task-specific data.
 * This way, module state is stored on the blackboard instead of in the modules
 * themselves.
 */
class Task {
 public:
  explicit Task(UpcId upcId, std::unordered_set<Unit*> units = {})
      : upcId_(upcId), units_(std::move(units)) {}
  virtual ~Task() {}

  virtual void update(State*) {}

  /*
   * The cancel method is an external signal that can be sent
   * by the module that owns the task, or by other tasks that are proxies
   * of the upc treated in the current task. It should be understood as a
   * means to mark the task as failed from the bot itself, rather than from
   * external conditions. Cancel signals should be propagated from ProxyTasks
   * to the tasks that execute the proxied UPC.
   *
   * The cancel method of a task should typically free the resources
   * reserved for it, and essentially set the overall state as if the
   * task naturally failed, including the removal of the task from the
   * blackboard in case it was not posted with auroremove=true.
   *
   * It *does not* mean that cancelling the task should cancel commands
   * sent to the game (this is usually impossible) or anything.
   */
  virtual void cancel(State*);

  TaskStatus status() const {
    return status_;
  }

  void setStatus(TaskStatus status) {
    status_ = status;
  }

  bool finished() const {
    return status_ == TaskStatus::Success || status_ == TaskStatus::Failure ||
        status_ == TaskStatus::Cancelled;
  }

  /// UPC id in Blackboard that caused this Task to be spawned
  UpcId upcId() const {
    return upcId_;
  }
  /// A set of units occupied performing this task
  std::unordered_set<Unit*> const& units() const {
    return units_;
  }
  void removeUnit(Unit* unit);
  /// A set of units occupied performing this task. If this is a ProxyTask, this
  /// will reflect the units of the targeted task.
  virtual std::unordered_set<Unit*> const& proxiedUnits() const {
    return units_;
  }

  /// A name for this task, for debugging purposes
  virtual const char* getName() const {
    return "Task";
  };

 protected:
  std::unordered_set<Unit*>& units() {
    return units_;
  }
  /// Remove units that have been assigned to another task and units that have
  /// died.
  virtual void removeDeadOrReassignedUnits(State* state);

 private:
  TaskStatus status_ = TaskStatus::Unknown;
  UpcId upcId_ = -1;
  std::unordered_set<Unit*> units_;
};

/**
 * A task that tracks execution of another task (for another UPCTuple).
 *
 * This is a simple wrapper task that can be used for modules that are merely
 * interested in the outcome of any action triggered by a posted UPCTuple.
 */
class ProxyTask : public Task {
 public:
  ProxyTask(UpcId targetUpcId, UpcId upcId);
  virtual ~ProxyTask() = default;

  virtual void update(State* state) override;
  virtual void cancel(State* state) override;
  virtual std::unordered_set<Unit*> const& proxiedUnits() const override;

  std::shared_ptr<Task> target() const {
    return target_;
  }

 protected:
  UpcId targetUpcId_;
  std::shared_ptr<Task> target_;
};

/// Policies for tracking multiple tasks.
enum class ProxyPolicy {
  /// One task has a given status
  ANY,
  /// Most tasks (i.e. more than half) have a given status
  MOST,
  /// All tasks have a given status
  ALL,
};

/**
 * A task that tracks execution of multiple other tasks.
 *
 * This is similar to ProxyTask but supports tracking of multiple tasks. Note
 * that this is slightly heavier in terms of processing needs than ProxyTask.
 * The status of the underlying tasks is mirrored according to the given policy.
 * These are the default settings:
 * - ProxyPolicy::ALL for TaskStatus::Success
 * - ProxyPolicy::ANY for TaskStatus::Failure
 * - ProxyPolicy::ANY for TaskStatus::Ongoing
 * - ProxyPolicy::ALL for TaskStatus::Unknown
 *
 * Status checks according to policies are evaluated in the following order:
 * TaskStatus::Success, TaskStatus::Failure, TaskStatus::Ongoing and
 * TaskStatus::Unknown.
 */
class MultiProxyTask : public Task {
 public:
  MultiProxyTask(std::vector<UpcId> targetUpcIds, UpcId upcId);
  virtual ~MultiProxyTask() = default;

  void setPolicyForStatus(TaskStatus status, ProxyPolicy policy);

  virtual void update(State* state) override;
  virtual void cancel(State* state) override;
  virtual std::unordered_set<Unit*> const& proxiedUnits() const override;

  std::vector<std::shared_ptr<Task>> targets() const {
    return targets_;
  }

 protected:
  bool matchStatus(TaskStatus status);

  std::vector<UpcId> targetUpcIds_;
  std::vector<std::shared_ptr<Task>> targets_;
  std::unordered_set<Unit*> proxiedUnits_;
  std::map<TaskStatus, ProxyPolicy> policy_;
  TaskStatus defaultTargetStatus_ = TaskStatus::Unknown;
};

} // namespace cherrypi
