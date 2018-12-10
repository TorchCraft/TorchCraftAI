/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "blackboard.h"

#include "state.h"
#include "upcstorage.h"
#include "utils.h"

#include <glog/logging.h>

#include <chrono>
#include <iomanip>
#include <sstream>

DEFINE_bool(blackboard_drawunits, false, "Draw unit command information");
DEFINE_bool(blackboard_logunits, false, "Log unit command information");

namespace cherrypi {

namespace {

template <typename UnaryPredicate>
Blackboard::UPCMap selectUpcs(
    std::map<int, UPCData> const& upcs,
    UnaryPredicate pred) {
  Blackboard::UPCMap result;
  auto it = upcs.begin();
  while (true) {
    it = std::find_if(it, upcs.end(), pred);
    if (it == upcs.end()) {
      break;
    }
    result.emplace(it->first, it->second.upc);
    ++it;
  }
  return result;
}

} // namespace

char const* Blackboard::kEnemyRaceKey = "enemy_race";
char const* Blackboard::kEnemyNameKey = "enemy_name";
char const* Blackboard::kBuildOrderKey = "buildorder";
char const* Blackboard::kBuildOrderSwitchEnabledKey =
    "build_order_switch_enabled";
char const* Blackboard::kOpeningBuildOrderKey = "opening_buildorder";
char const* Blackboard::kMinScoutFrameKey = "scout_min_frame";
char const* Blackboard::kMaxScoutWorkersKey = "scout_max_workers";
char const* Blackboard::kMaxScoutExplorersKey = "scout_max_explorers";
char const* Blackboard::kGameUidKey = "game_uid";
char const* Blackboard::kMineralsPerFramePerGatherer =
    "minerals_per_frame_per_gatherer";
char const* Blackboard::kGasPerFramePerGatherer = "gas_per_frame_per_gatherer";
char const* Blackboard::kGathererMinGasWorkers = "gatherer_min_gas_workers";
char const* Blackboard::kGathererMaxGasWorkers = "gatherer_max_gas_workers";
char const* Blackboard::kBanditRootKey = "bandit_root";

Blackboard::Blackboard(State* state)
    : state_(state),
      commands_(16),
      upcStorage_(std::make_unique<UpcStorage>()) {
  // Note: state may not be fully constructed yet -- don't do anything fancy
  // with it.
}
Blackboard::~Blackboard() {}

void Blackboard::init() {}

bool Blackboard::isTracked(UnitId uid) const {
  return tracked_.find(uid) != tracked_.end();
}
void Blackboard::track(UnitId uid) {
  tracked_.insert(uid);
}
void Blackboard::untrack(UnitId uid) {
  tracked_.erase(uid);
}

void Blackboard::setCollectTimers(bool collect) {
  collectTimers_ = collect;
}

UpcId Blackboard::postUPC(
    std::shared_ptr<UPCTuple>&& upc,
    UpcId sourceId,
    Module* origin,
    std::shared_ptr<UpcPostData> data) {
  for (auto filter : upcFilters_) {
    bool valid = filter->filter(state_, upc, origin);
    if (!valid) {
      // Note that we return the ID to the caller, i.e. it will assume that
      // the UPC has been posted. This is done in order to not impose a need
      // to handle posting failures. Module code should be robust enough to
      // handle situations where UPCTuples are not consumed anyway.
      LOG(WARNING) << "UPCTuple " << utils::upcString(upc, kFilteredUpcId)
                   << " from " << origin->name()
                   << " has been filtered out. Not posting.";
      return kFilteredUpcId;
    }
  }

  auto id = upcStorage_->addUpc(
      state_->currentFrame(), sourceId, origin, upc, std::move(data));
  activeUpcs_[id] = upc;
  upcs_[id] = UPCData(upc, sourceId, origin);
  VLOG(1) << "<- " << utils::upcString(upc, id) << " from " << origin->name()
          << " with source " << utils::upcString(sourceId);
  return id;
}

void Blackboard::consumeUPCs(std::vector<UpcId> const& ids, Module* consumer) {
  for (auto id : ids) {
    VLOG(1) << "-> " << utils::upcString(upcs_[id].upc, id) << " to "
            << consumer->name();
    upcs_.erase(id);
  }
}

void Blackboard::removeUPCs(std::vector<int> const& ids) {
  for (auto id : ids) {
    if (upcs_.erase(id) > 0) {
      VLOG(1) << "-> upc " << id << " removed ";
    }
  }
}

Blackboard::UPCMap Blackboard::upcs() const {
  return selectUpcs(upcs_, [](std::pair<int, UPCData> const&) { return true; });
}

Blackboard::UPCMap Blackboard::upcsFrom(Module* origin) const {
  return selectUpcs(upcs_, [origin](std::pair<int, UPCData> const& d) {
    return d.second.origin == origin;
  });
}

Blackboard::UPCMap Blackboard::upcsWithSharpCommand(Command cmd) const {
  return selectUpcs(upcs_, [cmd](std::pair<int, UPCData> const& d) {
    return d.second.upc->commandProb(cmd) == 1.0f;
  });
}

Blackboard::UPCMap Blackboard::upcsWithCommand(Command cmd, float minProb)
    const {
  return selectUpcs(upcs_, [cmd, minProb](std::pair<int, UPCData> const& d) {
    return d.second.upc->commandProb(cmd) >= minProb;
  });
}

std::shared_ptr<UPCTuple> Blackboard::upcWithId(UpcId id) const {
  auto it = upcs_.find(id);
  if (it == upcs_.end()) {
    return nullptr;
  }
  return it->second.upc;
}

UpcStorage* Blackboard::upcStorage() const {
  return upcStorage_.get();
}

void Blackboard::addUPCFilter(std::shared_ptr<UPCFilter> filter) {
  upcFilters_.push_back(filter);
}

void Blackboard::removeUPCFilter(std::shared_ptr<UPCFilter> filter) {
  upcFilters_.remove(filter);
}

void Blackboard::postTask(
    std::shared_ptr<Task> task,
    Module* owner,
    bool autoRemove) {
  if (tasksById_.find(task->upcId()) != tasksById_.end()) {
    throw std::runtime_error(
        "Existing task found for " + utils::upcString(task->upcId()));
  }
  auto it = tasks_.emplace(tasks_.end(), task, owner, autoRemove);
  tasksById_.emplace(task->upcId(), it);
  tasksByModule_.emplace(owner, it);
  for (Unit* u : const_cast<const Task*>(task.get())->units()) {
    tasksByUnit_[u] = it;
  }
}

std::shared_ptr<Task> Blackboard::taskForId(int id) const {
  auto it = tasksById_.find(id);
  if (it == tasksById_.end()) {
    return nullptr;
  }
  return it->second->task;
}

std::vector<std::shared_ptr<Task>> Blackboard::tasksOfModule(
    Module* module) const {
  std::vector<std::shared_ptr<Task>> result;
  auto range = tasksByModule_.equal_range(module);
  for (auto it = range.first; it != range.second; ++it) {
    result.emplace_back(it->second->task);
  }
  return result;
}

std::shared_ptr<Task> Blackboard::taskWithUnit(Unit* unit) const {
  auto it = tasksByUnit_.find(unit);
  if (it == tasksByUnit_.end()) {
    return nullptr;
  }
  return it->second->task;
}

TaskData Blackboard::taskDataWithUnit(Unit* unit) const {
  auto it = tasksByUnit_.find(unit);
  if (it == tasksByUnit_.end()) {
    return TaskData();
  }
  return *(it->second);
}

std::shared_ptr<Task> Blackboard::taskWithUnitOfModule(
    Unit* unit,
    Module* module) const {
  auto it = tasksByUnit_.find(unit);
  if (it == tasksByUnit_.end()) {
    return nullptr;
  }
  if (it->second->owner != module) {
    return nullptr;
  }
  return it->second->task;
}

void Blackboard::markTaskForRemoval(int upcId) {
  // Mark for removal, but keep it around until the next update
  tasksToBeRemoved_.push_back(upcId);
}

TaskStatus Blackboard::lastStatusOfTask(UpcId id) const {
  auto it = lastTaskStatus_.find(id);
  if (it == lastTaskStatus_.end()) {
    return TaskStatus::Unknown;
  }
  return it->second;
}

void Blackboard::updateUnitAccessCounts(tc::Client::Command const& command) {
  // Make sure we got a command to a unit and the unit exists
  if (command.code == tc::BW::Command::CommandUnit && command.args.size() > 0) {
    const int unitId = command.args[0];

    // Not sure if the default constructor of an int initializes it
    // to 0, hence being careful
    unitAccessCounts_[unitId] =
        (unitAccessCounts_.find(unitId) == unitAccessCounts_.end())
        ? 0
        : unitAccessCounts_[unitId] + 1;
  }
}

void Blackboard::postCommand(
    tc::Client::Command const& command,
    UpcId sourceId) {
  updateUnitAccessCounts(command);
  commands_.at(0).push_back({command, sourceId});

  if (command.code == tc::BW::Command::CommandUnit && command.args.size() > 0) {
    // Register source UPC as the last UPC that influenced this unit
    auto unitId = command.args[0];
    auto* unit = state_->unitsInfo().getUnit(unitId);
    if (unit == nullptr) {
      LOG(WARNING) << "Command posted for non-existent unit: "
                   << utils::commandString(state_, command);
      return;
    }
    unit->lastUpcId = sourceId;

    // Register UPC commands for all UPCs that led to this command
    unit->lastUpcCommands = Command::None;
    auto drawableCommand = Command::None;
    auto curId = sourceId;
    while (curId > kRootUpcId) {
      auto it = activeUpcs_.find(curId);
      if (it == activeUpcs_.end()) {
        LOG(WARNING) << "Active UPC entry missing for "
                     << utils::upcString(curId) << " (ancestor of "
                     << utils::upcString(sourceId) << ")";
        break;
      }

      for (auto& cit : it->second->command) {
        if (cit.second >= Unit::kLastUpcCommandThreshold) {
          unit->lastUpcCommands |= cit.first;

          if (drawableCommand == Command::None) {
            drawableCommand = cit.first;
          }
        }
      }

      curId = upcStorage_->sourceId(curId);
    }

    if (FLAGS_blackboard_drawunits) {
      utils::drawUnitCommand(
          state_, unit, drawableCommand, command.args[1], sourceId);
    }
    if (FLAGS_blackboard_logunits) {
      VLOG(0) << utils::commandString(state_, command);
    }
  }
}

std::vector<tc::Client::Command> Blackboard::commands(int stepsBack) const {
  std::vector<tc::Client::Command> comms;
  for (auto& cpost :
       commands_.at(-std::min(stepsBack, int(commands_.size()) - 1))) {
    comms.push_back(cpost.command);
  }
  return comms;
}

void Blackboard::updateTasksByUnit(Task* task) {
  auto it = std::find_if(tasks_.begin(), tasks_.end(), [&](auto& v) {
    return v.task.get() == task;
  });
  for (auto i = tasksByUnit_.begin(); i != tasksByUnit_.end();) {
    if (i->second == it) {
      i = tasksByUnit_.erase(i);
    } else {
      ++i;
    }
  }
  for (Unit* u : const_cast<const Task*>(task)->units()) {
    tasksByUnit_[u] = it;
  }
}

void Blackboard::clearCommands() {
  commands_.push();
}

void Blackboard::update() {
  // Remove tasks pending for removal
  for (auto id : tasksToBeRemoved_) {
    auto it = tasksById_.find(id);
    if (it == tasksById_.end()) {
      LOG(WARNING) << "Task " << id << " to be removed but does not exist";
      continue;
    }

    // Update mappings; this is quite painful for the multimap
    auto owner = it->second->owner;
    VLOG(2) << "Removing task with id " << id << " from " << owner->name();
    auto range = tasksByModule_.equal_range(owner);
    for (auto itM = range.first; itM != range.second; ++itM) {
      if (itM->second->task->upcId() == id) {
        tasksByModule_.erase(itM);
        break;
      }
    }
    // Remove all references in tasksByUnit_
    for (auto i = tasksByUnit_.begin(); i != tasksByUnit_.end();) {
      if (i->second == it->second) {
        i = tasksByUnit_.erase(i);
      } else {
        ++i;
      }
    }
    tasks_.erase(it->second);
    tasksById_.erase(it);
  }
  tasksToBeRemoved_.clear();

  // We want to keep all UPCs for which there is an entry in the Blackboard or
  // for which there is a task. Also, keep all their source UPCs.
  std::unordered_set<UpcId> activeUpcIds;
  for (auto& it : upcs_) {
    auto curId = it.first;
    while (curId >= kRootUpcId) {
      activeUpcIds.insert(curId);
      curId = upcStorage_->sourceId(curId);
    }
  }
  for (auto& it : tasksById_) {
    auto curId = it.first;
    while (curId >= kRootUpcId) {
      activeUpcIds.insert(curId);
      curId = upcStorage_->sourceId(curId);
    }
  }
  for (auto it = activeUpcs_.begin(); it != activeUpcs_.end();) {
    if (activeUpcIds.find(it->first) == activeUpcIds.end()) {
      VLOG(3) << "No more activity for " << utils::upcString(it->first)
              << ", removing from active UPCs";
      it = activeUpcs_.erase(it);
    } else {
      ++it;
    }
  }

  // Remove any dead units from the tasksByUnit_ mapping. Do this before
  // updating the tasks so that they can handle deaths and re-assignment the
  // same way.
  for (auto* u : state_->unitsInfo().getDestroyUnits()) {
    auto uit = tasksByUnit_.find(u);
    if (uit != tasksByUnit_.end()) {
      tasksByUnit_.erase(u);
    }
  }

  // Clear before adding stats for this round of updates
  taskTimeStats_.clear();

  // Update tasks in reverse order of their UPC Id, effectively running more
  // recently created tasks first.
  for (auto it = tasksById_.rbegin(); it != tasksById_.rend(); ++it) {
    auto task = it->second->task;

    if (task->status() != TaskStatus::Cancelled) {
      std::chrono::time_point<hires_clock> start;
      if (collectTimers_) {
        start = hires_clock::now();
      }
      task->update(state_);
      if (collectTimers_) {
        auto duration = hires_clock::now() - start;
        auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(duration);
        // Only log tasks that longer than threshold of 0 ms
        if (ms.count() > 0) {
          taskTimeStats_.push_back(
              std::make_tuple(it->first, it->second->owner->name(), ms));
        }
      }
    }
    lastTaskStatus_[task->upcId()] = task->status();

    // Update tasksByUnit_. This is not efficient, and would be best done
    // in Task::setUnits
    for (auto i = tasksByUnit_.begin(); i != tasksByUnit_.end();) {
      if (i->second == it->second) {
        i = tasksByUnit_.erase(i);
      } else {
        ++i;
      }
    }
    for (Unit* u : const_cast<const Task*>(task.get())->units()) {
      tasksByUnit_[u] = it->second;
    }

    // If auto-removal is turned on, schedule finished tasks for removal
    if (it->second->autoRemove && task->finished()) {
      VLOG(1) << "Blackboard: removing task " << utils::upcString(task->upcId())
              << " with status " << (int)task->status();
      tasksToBeRemoved_.push_back(it->first);
    }
  }

  // Check if any of the tasks that are not marked for removal have units
  // in common
  std::set<UpcId> tasksToBeRemoved_t(
      tasksToBeRemoved_.begin(), tasksToBeRemoved_.end());
  std::map<Unit*, UpcId> duplicateUnits;

  // Check if two tasks have overlapping units
  for (auto it = tasksById_.rbegin(); it != tasksById_.rend(); ++it) {
    // Create a new shared pointer to get a const pointer to access
    // the public const function units() instead of the protected one
    auto task = std::const_pointer_cast<const Task>(it->second->task);

    // Only check tasks that are not scheduled to be removed
    uint32_t warningsCount = 0;
    auto constexpr kMaxWarningsPerFrame = 3;
    if (tasksToBeRemoved_t.find(task->upcId()) == tasksToBeRemoved_t.end()) {
      for (auto const unit : task->units()) {
        auto unitIt = duplicateUnits.find(unit);
        if (unitIt != duplicateUnits.end()) {
          LOG_IF(WARNING, warningsCount++ < kMaxWarningsPerFrame)
              << "Task " << task->getName() << " "
              << utils::upcString(task->upcId())
              << " has unit in common with task "
              << utils::upcString(unitIt->second) << ": "
              << utils::unitString(unit);
        } else {
          duplicateUnits[unit] = task->upcId();
        }
      }
    }
    LOG_IF(WARNING, warningsCount > kMaxWarningsPerFrame)
        << "... and " << (warningsCount - kMaxWarningsPerFrame)
        << " other similar errors";
  }
}

void Blackboard::checkPostStep() {
  std::map<Unit*, UpcId> duplicateUnits;

  uint32_t warningsCount = 0;
  auto constexpr kMaxWarningsPerFrame = 3;
  for (auto upcTuple : upcs_) {
    auto upc = upcTuple.second.upc;

    for (auto unitProb : upc->unit) {
      if (unitProb.second == 1.0) {
        auto unitIt = duplicateUnits.find(unitProb.first);
        if (unitIt != duplicateUnits.end()) {
          LOG_IF(WARNING, warningsCount++ < kMaxWarningsPerFrame)
              << "Upc " << utils::upcString(upc, upcTuple.first) << " from "
              << upcTuple.second.origin->name()
              << " has unit in common with Upc "
              << utils::upcString(unitIt->second) << ": "
              << utils::unitString(unitProb.first);
        } else {
          duplicateUnits[unitProb.first] = upcTuple.first;
        }
      }
    }
  }
  LOG_IF(WARNING, warningsCount > kMaxWarningsPerFrame)
      << "... and " << (warningsCount - kMaxWarningsPerFrame)
      << " other similar errors";
}

} // namespace cherrypi
