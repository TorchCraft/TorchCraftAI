/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "cherrypi.h"
#include "common/circularbuffer.h"
#include "module.h"
#include "task.h"
#include "unitsinfo.h"
#include "upc.h"
#include "upcfilter.h"

#include <chrono>
#include <list>
#include <map>
#include <memory>

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <mapbox/variant.hpp>
#include <torchcraft/client.h>

namespace cherrypi {

class UpcStorage;
struct UpcPostData;
class SharedController;
class CherryVisDumperModule;

/// UPCTuple and associated origin
struct UPCData {
 public:
  std::shared_ptr<UPCTuple> upc = nullptr;
  UpcId source = kInvalidUpcId;
  Module* origin = nullptr;

  UPCData() {}
  UPCData(std::shared_ptr<UPCTuple> upc, UpcId source, Module* origin)
      : upc(upc), source(source), origin(origin) {}
};

/// Task and associated owner
struct TaskData {
 public:
  std::shared_ptr<Task> task = nullptr;
  Module* owner = nullptr;
  bool autoRemove = true;
  FrameNum creationFrame = -1;

  TaskData() {}
  TaskData(
      std::shared_ptr<Task> task,
      Module* owner,
      bool autoRemove,
      FrameNum creationFrame)
      : task(std::move(task)),
        owner(owner),
        autoRemove(autoRemove),
        creationFrame(creationFrame) {}
};

/**
 * Game command are posted with an associated UPC ID.
 */
struct CommandPost {
  tc::Client::Command command;
  UpcId sourceId;
};

/**
 * An access-aware blackboard.
 *
 * The blackboard provides a means for modules to exchange UPCTuples while
 * keeping track of producers and consumers.
 *
 * Furthermore, there is functionality for holding global state via a simple
 * key-value store (post(), hasKey(), get() and remove()).
 *
 * The blackboard itself will only store active UPCTuple objects, i.e. UPCs
 * that have not been consumed and UPCs (as well as their sources) for which
 * there are active tasks.
 */
class Blackboard {
 public:
  /// A variant of types that are allowed in the Blackboard's key-value storage.
  using Data = mapbox::util::variant<
      bool,
      int,
      float,
      double,
      std::string,
      Position,
      std::shared_ptr<SharedController>,
      std::unordered_map<int, int>>;
  using UPCMap = std::map<UpcId, std::shared_ptr<UPCTuple>>;

  using TaskTimeStats =
      std::tuple<UpcId, std::string, std::chrono::milliseconds>;

  // A few commonly used keys for post() and get()
  static char const* kEnemyRaceKey;
  static char const* kEnemyNameKey;
  static char const* kBuildOrderKey;
  static char const* kBuildOrderSwitchEnabledKey;
  static char const* kOpeningBuildOrderKey;
  static char const* kMinScoutFrameKey;
  static char const* kMaxScoutWorkersKey;
  static char const* kMaxScoutExplorersKey;
  static char const* kGameUidKey;
  static char const* kMineralsPerFramePerGatherer;
  static char const* kGasPerFramePerGatherer;
  static char const* kGathererMinGasWorkers;
  static char const* kGathererMaxGasWorkers;
  static char const* kBanditRootKey;

  Blackboard(State* state);
  virtual ~Blackboard();

  void init();

  /// Clears the queue of commands
  void clearCommands();

  /// Updates internal mappings after the torchcraft state has been updated.
  void update();

  void post(std::string const& key, Data const& data) {
    map_[key] = data;
  }
  bool hasKey(std::string const& key) {
    return map_.find(key) != map_.end();
  }
  Data const& get(std::string const& key) const {
    return map_.at(key);
  }
  template <typename T>
  T const& get(std::string const& key) const {
    return map_.at(key).get<T>();
  }
  template <typename T>
  T const& get(std::string const& key, T const& defaultValue) const {
    auto it = map_.find(key);
    if (it == map_.end()) {
      return defaultValue;
    }
    return it->second.get<T>();
  }
  void remove(std::string const& key) {
    map_.erase(key);
  }
  template <typename T>
  void iterValues(T f_do) const {
    for (auto it = map_.begin(); it != map_.end(); ++it) {
      f_do(it->first, it->second);
    }
  }

  bool isTracked(UnitId uid) const;
  void track(UnitId uid);
  void untrack(UnitId uid);

  // UPC post/query/consume

  /**
   * Post a UPC tuple.
   *
   * The UPC tuple will be subject to filtering via UPCFilter objects. The
   * filtered UPCTuple object will end up in the Blackboard, and the function
   * will return a resulting UPC ID that is unique for this Blackboard instance.
   * If the UPCTuple was rejected by a filter, kFilteredUpcId will be returned.
   *
   * The signature of this function enforces posting UPC tuples via move() as
   * UPC filters modify UPC tuples by creating a copy of them. Hence, the
   * UPCTuple instance ending up in the Blackboard might be different from the
   * one provided to this function.
   */
  UpcId postUPC(
      std::shared_ptr<UPCTuple>&& upc,
      UpcId sourceId,
      Module* origin,
      std::shared_ptr<UpcPostData> data = nullptr);
  void consumeUPCs(std::vector<UpcId> const& ids, Module* consumer);
  void consumeUPC(UpcId id, Module* consumer) {
    consumeUPCs({id}, consumer);
  }
  void removeUPCs(std::vector<UpcId> const& ids);
  /// Returns all non-consumed UPCs
  UPCMap upcs() const;
  /// Returns all non-consumed UPCs from a given module
  UPCMap upcsFrom(Module* origin) const;
  /// Returns all non-consumed UPCs from a given module
  UPCMap upcsFrom(std::shared_ptr<Module> origin) const {
    return upcsFrom(origin.get());
  }
  /// Returns all non-consumed UPCs with a Dirac command distribution on cmd
  UPCMap upcsWithSharpCommand(Command cmd) const;
  /// Returns all non-consumed UPCs with a command distribution where cmd has at
  /// least a probability of minProb
  UPCMap upcsWithCommand(Command cmd, float minProb) const;
  /// Returns the non-consumed UPC with the given ID
  std::shared_ptr<UPCTuple> upcWithId(UpcId id) const;
  UpcStorage* upcStorage() const;

  // UPC filters
  void addUPCFilter(std::shared_ptr<UPCFilter> filter);
  void removeUPCFilter(std::shared_ptr<UPCFilter> filter);

  // Task post/query
  void
  postTask(std::shared_ptr<Task> task, Module* owner, bool autoRemove = false);
  std::shared_ptr<Task> taskForId(UpcId id) const;
  std::vector<std::shared_ptr<Task>> tasksOfModule(Module* module) const;
  std::shared_ptr<Task> taskWithUnit(Unit* unit) const;
  TaskData taskDataWithUnit(Unit* unit) const;
  std::shared_ptr<Task> taskWithUnitOfModule(Unit* unit, Module* module) const;
  void markTaskForRemoval(UpcId upcId);
  void markTaskForRemoval(std::shared_ptr<Task> task) {
    markTaskForRemoval(task->upcId());
  }

  /// This will return `Unknown` for tasks that were never registered
  TaskStatus lastStatusOfTask(UpcId id) const;

  void updateUnitAccessCounts(tc::Client::Command const& command);

  // Game commands
  void postCommand(tc::Client::Command const& command, UpcId sourceId);
  std::vector<tc::Client::Command> commands(int stepsBack = 0) const;
  size_t pastCommandsAvailable() const {
    return commands_.size();
  }

  /// Updates the taskByUnit mapping, should be called after setUnits on a task
  void updateTasksByUnit(Task* task);

  /// UPC consistency checks
  /// Calling this only makes sense in the player's postStep function, once
  /// all the UPCs have been converted into commands to be posted to the game.
  void checkPostStep();

  std::vector<TaskTimeStats> getTaskTimeStats() const {
    return taskTimeStats_;
  }

  void setCollectTimers(bool collect);

  void setTraceDumper(std::shared_ptr<CherryVisDumperModule> tracer) {
    traceDumper_ = tracer;
  }

  std::shared_ptr<CherryVisDumperModule> getTraceDumper() {
    return traceDumper_;
  }

 private:
  State* state_;
  std::unordered_map<std::string, Data> map_;
  common::CircularBuffer<std::vector<CommandPost>> commands_;
  std::map<UpcId, UPCData> upcs_;
  std::unique_ptr<UpcStorage> upcStorage_;

  /// UPCs that are on the Blackboard or have active tasks
  std::unordered_map<UpcId, std::shared_ptr<UPCTuple>> activeUpcs_;
  std::list<std::shared_ptr<UPCFilter>> upcFilters_;
  std::set<UnitId> tracked_;

  std::list<TaskData> tasks_;
  std::map<UpcId, std::list<TaskData>::iterator> tasksById_;
  std::unordered_multimap<Module*, std::list<TaskData>::iterator>
      tasksByModule_;
  std::unordered_map<Unit*, std::list<TaskData>::iterator> tasksByUnit_;
  std::vector<UpcId> tasksToBeRemoved_;
  std::map<UnitId, size_t> unitAccessCounts_;
  std::vector<TaskTimeStats> taskTimeStats_;
  std::unordered_map<UpcId, TaskStatus> lastTaskStatus_;
  std::shared_ptr<CherryVisDumperModule> traceDumper_;

  bool collectTimers_ = false;
};

} // namespace cherrypi
