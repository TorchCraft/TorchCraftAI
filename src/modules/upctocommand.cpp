/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "state.h"
#include "task.h"
#include "utils.h"

#include "common/rand.h"
#include "fmt/format.h"
#include "modules/upctocommand.h"

#include <bwem/map.h>
#include <glog/logging.h>

namespace cherrypi {

REGISTER_SUBCLASS_0(Module, UPCToCommandModule);

void UPCToCommandModule::checkDuplicateCommand(
    State* state,
    const Unit* unit,
    UpcId newUpcId,
    UPCToCommandState& upcToCommandState) {
  if (upcToCommandState.commandToUnit.find(unit) ==
      upcToCommandState.commandToUnit.end()) {
    upcToCommandState.commandToUnit.insert(unit);
  } else {
    VLOG(0) << fmt::format(
        "More than one command to unit {0} from UPC {1} of task {2}",
        utils::unitString(unit),
        utils::upcString(state->board()->upcWithId(newUpcId), newUpcId),
        utils::upcTaskString(state, newUpcId));
  }
}

void UPCToCommandModule::registerCommand(
    State* state,
    const Unit* unit,
    UpcId upcId,
    tc::Client::Command command,
    UPCToCommandState& upcToCommandState) {
  checkDuplicateCommand(state, unit, upcId, upcToCommandState);
  upcToCommandState.commands.emplace_back(command);
  upcToCommandState.upcIds.push_back(upcId);
  VLOG(1) << "Command from " << utils::upcString(upcId) << ": "
          << utils::commandString(state, upcToCommandState.commands.back());
}

void UPCToCommandModule::postGameCommand(
    State* state,
    UPCToCommandState& upcToCommandState) {
  auto board = state->board();

  board->consumeUPCs(upcToCommandState.upcIds, this);
  for (size_t i = 0; i < upcToCommandState.commands.size(); i++) {
    board->postCommand(
        upcToCommandState.commands[i], upcToCommandState.upcIds[i]);
  }
}

void UPCToCommandModule::stepUPC(
    State* state,
    UPCToCommandState& upcToCommandState,
    UpcId upcId,
    UPCTuple* const upc) {
  auto issue = [&](Unit* unit, auto&&... args) {
    auto commandType = tc::BW::Command::CommandUnit;
    auto command = tc::Client::Command(
        commandType, unit->id, std::forward<decltype(args)>(args)...);
    registerCommand(state, unit, upcId, std::move(command), upcToCommandState);
  };

  if (upc->unit.size() == 1 && upc->commandProb(Command::Gather) == 1) {
    Unit* unit = upc->unit.begin()->first;
    auto dest = upc->positionUArgMax().first;
    if (dest) {
      issue(unit, tc::BW::UnitCommandType::Right_Click_Unit, dest->id);
    } else {
      auto pos = upc->positionArgMax().first;
      for (Unit* target : state->unitsInfo().visibleUnits()) {
        if (target->x == pos.x && target->y == pos.y) {
          issue(unit, tc::BW::UnitCommandType::Right_Click_Unit, target->id);
          break;
        }
      }
    }
  } else if (
      upc->unit.size() == 1 && upc->commandProb(Command::Create) == 1 &&
      upc->state.is<UPCTuple::BuildTypeMap>()) {
    auto& createType = upc->state.get_unchecked<UPCTuple::BuildTypeMap>();
    if (createType.size() != 1) {
      VLOG(4) << "No single create type in state. Skipping.";
      return;
    }
    const BuildType* type = createType.begin()->first;
    Unit* unit = upc->unit.begin()->first;
    if (unit == nullptr) {
      LOG(WARNING) << "null unit";
      return;
    }
    if (upc->unit[unit] < 1) {
      VLOG(4) << "Unit probability " << upc->unit[unit] << " < 1 for "
              << utils::unitString(unit) << ". Skipping.";
      return;
    }

    if (unit->type->isWorker && type->isBuilding) {
      auto p = upc->positionArgMax().first;
      issue(unit, tc::BW::UnitCommandType::Build, -1, p.x, p.y, type->unit);
    } else if (type->isAddon) {
      issue(unit, tc::BW::UnitCommandType::Build_Addon, -1, 0, 0, type->unit);
    } else if (type->isUnit()) {
      if (type->isBuilding) {
        issue(unit, tc::BW::UnitCommandType::Morph, -1, 0, 0, type->unit);
      } else {
        if (type == buildtypes::Protoss_Archon ||
            type == buildtypes::Protoss_Dark_Archon) {
          LOG(WARNING) << " FIXME: morph archon!";
        }
        issue(unit, tc::BW::UnitCommandType::Train, -1, 0, 0, type->unit);
        // We need to keep track of the supply only since resources are
        // immediately accounted for in the game when issuing the training
        // order.
      }
    } else if (type->isUpgrade()) {
      issue(unit, tc::BW::UnitCommandType::Upgrade, -1, 0, 0, type->upgrade);
    } else if (type->isTech()) {
      issue(unit, tc::BW::UnitCommandType::Research, -1, 0, 0, type->tech);
    } else {
      LOG(WARNING) << "Cannot handle create command with "
                   << utils::unitString(unit);
    }
  } else if (
      upc->commandProb(Command::Move) == 1 ||
      upc->commandProb(Command::Flee) == 1) {
    for (auto& uprob : upc->unit) {
      if (uprob.second == 0) {
        continue;
      }
      auto pos = upc->positionArgMax().first;
      auto unit = uprob.first;
      issue(unit, tc::BW::UnitCommandType::Move, -1, pos.x, pos.y);
    }
  } else if (upc->commandProb(Command::Delete) == 1) {
    // Determine target first, it will be the same for all units.
    UnitId targetId = -1;
    Position targetPos;
    if (upc->position.is<UPCTuple::UnitMap>()) {
      auto& map = upc->position.get_unchecked<UPCTuple::UnitMap>();
      if (map.empty()) {
        VLOG(0) << "Empty unit map for UPC position";
        return;
      }
      if (map.begin()->second == 1) {
        targetId = map.begin()->first->id;
      } else {
        VLOG(0) << "Non-sharp unit map element for UPC position";
        return;
      }
    } else {
      targetPos = upc->positionArgMax().first;
    }

    for (auto& uprob : upc->unit) {
      if (uprob.second == 0) {
        continue;
      }
      auto unit = uprob.first;
      if (targetId >= 0) {
        issue(unit, tc::BW::UnitCommandType::Attack_Unit, targetId);
      } else {
        issue(
            unit,
            tc::BW::UnitCommandType::Attack_Move,
            -1,
            targetPos.x,
            targetPos.y);
      }
    }
  } else if (upc->commandProb(Command::Cancel) == 1) {
    for (auto& uprob : upc->unit) {
      if (uprob.second == 0) {
        continue;
      }
      auto unit = uprob.first;
      issue(unit, tc::BW::UnitCommandType::Cancel_Morph);
    }
  } else if (upc->commandProb(Command::ReturnCargo) == 1) {
    for (auto& uprob : upc->unit) {
      if (uprob.second == 0) {
        continue;
      }
      auto unit = uprob.first;
      issue(unit, tc::BW::UnitCommandType::Return_Cargo);
    }
  }
}

void UPCToCommandModule::step(State* state) {
  UPCToCommandState upcToCommandState;

  auto allUpcsMap = state->board()->upcs();
  std::vector<std::pair<UpcId, std::shared_ptr<UPCTuple>>> allUpcs;
  allUpcs.reserve(allUpcsMap.size());
  for (auto& upct : allUpcsMap) {
    allUpcs.push_back(std::move(upct));
  }

  // Step through UPCs in a random order, in case we hit command limits.
  // This will ensure we execute as many commands as possible, randomly
  // picked from the UPCs.
  std::shuffle(
      allUpcs.begin(),
      allUpcs.end(),
      common::Rand::makeRandEngine<std::minstd_rand>());
  for (auto const& upct : allUpcs) {
    auto upcId = upct.first;
    auto upc = upct.second.get();
    stepUPC(state, upcToCommandState, upcId, upc);
  }

  postGameCommand(state, upcToCommandState);
  temporaryDebugDrawing(state, upcToCommandState);
}

// TODO: better interface for drawing stuff?
void UPCToCommandModule::temporaryDebugDrawing(
    State* state,
    UPCToCommandState& upcToCommandState) {
  if (!VLOG_IS_ON(3))
    return;
  for (auto& area : state->map()->Areas()) {
    for (auto& base : area.Bases()) {
      utils::drawCircle(
          state,
          Position(base.Location() * tc::BW::XYWalktilesPerBuildtile) + 2,
          16,
          254);
    }
  }

  for (Unit* u : state->unitsInfo().liveUnits()) {
    if (u->gone) {
      utils::drawCircle(state, u, 12);
    } else {
      utils::drawCircle(state, u, 8, tc::BW::Color::Yellow);
    }
  }

  auto forAllTiles = [&](TilesInfo& tt, auto&& f) {
    size_t stride = TilesInfo::tilesWidth - tt.mapTileWidth();
    Tile* ptr = tt.tiles.data();
    for (unsigned tileY = 0; tileY != tt.mapTileHeight();
         ++tileY, ptr += stride) {
      for (unsigned tileX = 0; tileX != tt.mapTileWidth(); ++tileX, ++ptr) {
        f(*ptr);
      }
    }
  };

  forAllTiles(state->tilesInfo(), [&](Tile& t) {
    if (t.reservedAsUnbuildable) {
      utils::drawCircle(state, Position(t) + 2, 16, tc::BW::Color::Red);
    }
  });
}

} // namespace cherrypi
