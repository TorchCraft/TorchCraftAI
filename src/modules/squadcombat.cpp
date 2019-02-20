/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "squadcombat.h"
#include "squadcombat/squadtask.h"

#include "commandtrackers.h"
#include "common/rand.h"
#include "movefilters.h"
#include "player.h"
#include "state.h"
#include "upctocommand.h"
#include "utils.h"

#include "bwem/bwem.h"

#include <functional>
#include <glog/logging.h>

namespace cherrypi {

REGISTER_SUBCLASS_0(Module, SquadCombatModule);

void SquadCombatModule::step(State* s) {
  state = s;
  auto board = state->board();

  for (auto& model : models_) {
    model->forward(state);
  }

  // Form new squads based on new UPCs
  auto myUpcs = board->upcsFrom(this);

  // TODO: 0.1 is a magic number chosen based on the 0.11 floor of
  // Delete commands issued by Tactics. We should make this explicit.
  constexpr double kProbabilityThreshold = 0.1;
  auto considerMakingSquadFromUpc =
      [&](decltype(board->upcsWithCommand(
          Command::Delete, kProbabilityThreshold)) upcsWithCommand) {
        for (auto& upcs : upcsWithCommand) {
          auto id = upcs.first;
          auto upc = upcs.second;

          // Presumably this omits UPCs emitted by the Gather/Builder modules
          // for workers fighting back against harassment
          if (upc->commandProb(Command::Gather) > 0 ||
              upc->commandProb(Command::Create) > 0 ||
              myUpcs.find(id) != myUpcs.end() || upc->unit.empty()) {
            continue;
          }

          // Skip UPCs targeting allied units (Builder might want to remove a
          // blocking building, for example)
          //
          // ...is this check safe? How often will this land on our own units?
          bool targetsMyUnit = false;
          if (upc->position.is<UPCTuple::UnitMap>()) {
            for (auto& it : upc->position.get_unchecked<UPCTuple::UnitMap>()) {
              if (it.second > 0 && it.first->isMine) {
                targetsMyUnit = true;
                break;
              }
            }
          }
          if (targetsMyUnit) {
            continue;
          }

          if (formNewSquad(upc, id)) {
            board->consumeUPCs({id}, this);
          }
        }
      };

  considerMakingSquadFromUpc(
      board->upcsWithCommand(Command::Delete, kProbabilityThreshold));
  considerMakingSquadFromUpc(
      board->upcsWithCommand(Command::Flee, kProbabilityThreshold));

  // Update my units
  for (auto* unit : state->unitsInfo().myUnits()) {
    if (agents_.find(unit) == agents_.end()) {
      auto behaviorsDelete =
          std::make_shared<BehaviorSeries>(makeDeleteBehaviors());
      auto behaviorsFlee =
          std::make_shared<BehaviorSeries>(makeFleeBehaviors());
      agents_.emplace(unit, [&]() {
        Agent agent;
        agent.behaviorDelete = behaviorsDelete;
        agent.behaviorFlee = behaviorsFlee;
        return agent;
      }());
    }
  }

  // Erase dead units from agents_
  for (auto it = agents_.begin(); it != agents_.end();) {
    if (it->first->dead) {
      agents_.erase(it++);
    } else {
      ++it;
    }
  }

  // Update enemy units
  for (auto* u : state->unitsInfo().enemyUnits()) {
    if (enemyStates_.find(u) == enemyStates_.end()) {
      enemyStates_[u] = EnemyState();
    }
    auto& es = enemyStates_[u];
    if (u->flag(tc::Unit::Flags::Repairing)) {
      es.lastRepairing = state->currentFrame();
    } else if (
        es.lastRepairing != -1 &&
        state->currentFrame() - es.lastRepairing > 36) {
      es.lastRepairing = -1;
    }
  }

  // Erase dead units from enemyStates_
  for (auto it = enemyStates_.begin(); it != enemyStates_.end();) {
    if (it->first->dead) {
      enemyStates_.erase(it++);
    } else {
      it->second.damages = 0;
      ++it;
    }
  }

  // Update existing squads
  for (auto& task : board->tasksOfModule(this)) {
    updateTask(task);
  }
}

bool SquadCombatModule::formNewSquad(
    std::shared_ptr<UPCTuple> sourceUpc,
    int sourceUpcId) {
  auto upcString = utils::upcString(sourceUpcId);

  // Form a squad task with all units with non-zero probability
  std::unordered_set<Unit*> units;
  std::vector<Unit*> targets;

  for (auto& uprob : sourceUpc->unit) {
    if (uprob.second > 0) {
      units.insert(uprob.first);
    }
  }
  if (units.empty()) {
    VLOG(1) << "No units to take care of in " << upcString;
    return false;
  }

  std::shared_ptr<SquadTask> task;
  if (sourceUpc->position.is<UPCTuple::UnitMap>()) {
    for (auto it : sourceUpc->position.get_unchecked<UPCTuple::UnitMap>()) {
      if (it.second > 0) {
        targets.push_back(it.first);
      }
    }
    VLOG(2) << "Targeting " << targets.size() << " units";
    task = std::make_shared<SquadTask>(
        sourceUpcId,
        sourceUpc,
        units,
        std::move(targets),
        &enemyStates_,
        &agents_,
        &models_);
  } else if (sourceUpc->position.is<Position>()) {
    auto pos = sourceUpc->position.get_unchecked<Position>();
    task = std::make_shared<SquadTask>(
        sourceUpcId,
        sourceUpc,
        units,
        pos.x,
        pos.y,
        &enemyStates_,
        &agents_,
        &models_);
    VLOG(2) << "Targeting single position at " << pos.x << "," << pos.y;
  } else if (sourceUpc->position.is<torch::Tensor>()) {
    auto argmax = utils::argmax(
        sourceUpc->position.get_unchecked<torch::Tensor>(), sourceUpc->scale);
    int x, y;
    std::tie(x, y, std::ignore) = argmax;
    task = std::make_shared<SquadTask>(
        sourceUpcId, sourceUpc, units, x, y, &enemyStates_, &agents_, &models_);
    VLOG(2) << "Targeting position argmax at " << x << "," << y;
  } else {
    VLOG(0) << "No targets to attack in " << upcString;
    return false;
  }

  state->board()->postTask(task, this);
  task->setStatus(TaskStatus::Unknown);

  size_t numUnits = units.size();
  VLOG(1) << "Formed squad for " << upcString << " with " << numUnits
          << " units: " << utils::unitsString(units)
          << utils::unitsString(task->targets);
  return true;
}

void SquadCombatModule::updateTask(std::shared_ptr<Task> task) {
  auto squad = std::static_pointer_cast<SquadTask>(task);
  auto board = state->board();

  if (squad->status() != TaskStatus::Ongoing &&
      squad->status() != TaskStatus::Unknown) {
    auto upcString = utils::upcString(squad->upcId());
    char const* result;
    switch (squad->status()) {
      case TaskStatus::Success:
        result = "succeeded";
        break;
      case TaskStatus::Failure:
        result = "failed";
        break;
      case TaskStatus::Cancelled:
        result = "been cancelled";
        break;
      default:
        result = "UNRECOGNIZED STATUS";
    }
    VLOG(2) << "Squad for " << upcString << " " << result;
    board->markTaskForRemoval(squad);
    return;
  }

  auto upcs = squad->makeUPCs(state);
  for (auto upc : upcs) {
    if (upc) {
      board->postUPC(std::move(upc), task->upcId(), this);
    }
  }
}

void SquadCombatModule::enqueueModel(std::shared_ptr<MicroModel> model) {
  models_.push_back(model);
}

BehaviorList SquadCombatModule::makeDeleteBehaviors() {
  return BehaviorList{std::make_shared<BehaviorUnstick>(),
                      std::make_shared<BehaviorIfIrradiated>(),
                      std::make_shared<BehaviorIfStormed>(),
                      std::make_shared<BehaviorVsScarab>(),
                      std::make_shared<BehaviorFormation>(),
                      std::make_shared<BehaviorAsZergling>(),
                      std::make_shared<BehaviorAsMutaliskVsScourge>(),
                      std::make_shared<BehaviorAsMutaliskMicro>(),
                      std::make_shared<BehaviorAsScourge>(),
                      std::make_shared<BehaviorAsLurker>(),
                      std::make_shared<BehaviorAsHydralisk>(),
                      std::make_shared<BehaviorAsOverlord>(),
                      std::make_shared<BehaviorChase>(),
                      std::make_shared<BehaviorKite>(),
                      std::make_shared<BehaviorEngageCooperatively>(),
                      std::make_shared<BehaviorEngage>(),
                      std::make_shared<BehaviorLeave>(),
                      std::make_shared<BehaviorTravel>()};
}

BehaviorList SquadCombatModule::makeFleeBehaviors() {
  return BehaviorList{std::make_shared<BehaviorUnstick>(),
                      std::make_shared<BehaviorIfIrradiated>(),
                      std::make_shared<BehaviorIfStormed>(),
                      std::make_shared<BehaviorAsZergling>(),
                      std::make_shared<BehaviorAsLurker>(),
                      std::make_shared<BehaviorKite>(),
                      std::make_shared<BehaviorTravel>()};
}

} // namespace cherrypi
