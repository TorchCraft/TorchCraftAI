/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <unordered_map>

#include "baseplayer.h"
#include "cherryvisdumper.h"
#include "controller.h"
#include "fsutils.h"
#include "state.h"
#include "utils.h"
#include "zstdstream.h"

using json = nlohmann::json;

namespace cherrypi {

REGISTER_SUBCLASS_0(Module, CherryVisDumperModule);

void CherryVisDumperModule::step(State* s) {
  // Dump units updates
  for (auto unit : s->unitsInfo().visibleUnits()) {
    // Do not include units until they are properly visible
    // This filters out units being constructed
    if (!unit->flag(tc::Unit::Flags::Targetable)) {
      continue;
    }
    if (unitsInfos_.find(unit->id) == unitsInfos_.end()) {
      unitsFirstSeen_[std::to_string(s->currentFrame())].push_back(
          {
              {"id", unit->id},
              {"type", unit->type->unit},
              {"x", unit->unit.pixel_x},
              {"y", unit->unit.pixel_y},
          });
    }
    UnitData& infos = unitsInfos_[unit->id];
    int32_t current_task_id = getUnitTaskId(s, unit);
    if (infos.lastSeenTask != current_task_id) {
      json& j = unitsUpdates_[std::to_string(unit->id)];
      j[std::to_string(s->currentFrame())] = {{"task", current_task_id}};
      infos.lastSeenTask = current_task_id;
    }
  }

  // Dump blackboard updates
  json currentFrameUpdates = json::object();
  bool hasUpdates = false;
  s->board()->iterValues(
      [this, &currentFrameUpdates, &hasUpdates](
          std::string const& key, Blackboard::Data const& value) {
        std::string valueStr(getBoardValueAsString(value));
        if (boardKnownValues_.find(key) == boardKnownValues_.end() ||
            boardKnownValues_[key] != valueStr) {
          boardKnownValues_[key] = valueStr;
          currentFrameUpdates[key] = valueStr;
          hasUpdates = true;
        }
      });
  if (hasUpdates) {
    boardUpdates_[std::to_string(s->currentFrame())] =
        std::move(currentFrameUpdates);
  }
}

void CherryVisDumperModule::onGameStart(State* s) {
  taskToId_.clear();
  tasks_.clear();
  unitsInfos_.clear();
  unitsUpdates_.clear();
  unitsFirstSeen_.clear();
  boardUpdates_.clear();
  boardKnownValues_.clear();
  logSink_.reset();
  logs_.clear();
  logSink_ = std::make_unique<CherryVisLogSink>(this);
}

void CherryVisDumperModule::onGameEnd(State* s) {
  if (replayFileName_.empty()) {
    VLOG(1) << "No replay file provided, will not dump bot trace data";
    return;
  }

  // Dump build types names
  std::unordered_map<std::string, std::string> buildTypesToName;
  for (auto it : buildtypes::allUnitTypes) {
    buildTypesToName[std::to_string(it->unit)] = it->name;
  }

  // Stop logging
  logSink_.reset();

  // Create JSON
  json bot_dump = {{"types_names", buildTypesToName},
                   {"tasks", tasks_},
                   {"logs", logs_},
                   {"units_updates", unitsUpdates_},
                   {"units_first_seen", unitsFirstSeen_},
                   {"board_updates", boardUpdates_},
                   {"_version", 0}};

  // TODO: We need to parse "replayFileName_"
  // e.g. %BOTNAME%_%BOTRACE%
  try {
    std::string dumpDirectory(replayFileName_ + ".cvis/");
    VLOG(1) << "Dumping bot trace to " << dumpDirectory;
    fsutils::mkdir(dumpDirectory);

    // trace.json
    zstd::ofstream replayFile(dumpDirectory + "trace.json");
    replayFile << bot_dump.dump(-1, ' ', true);
    replayFile.close();

    // game_summary.json
    writeGameSummary(s, dumpDirectory + "game_summary.json");

  } catch (std::exception& e) {
    LOG(ERROR) << "Exception while writing bot trace for CVis: " << e.what();
  }
}

void CherryVisDumperModule::handleLog(
    google::LogSeverity severity,
    const char* full_filename,
    std::string message) {
  logs_.push_back(
      {{"frame", player_->state()->currentFrame()},
       {"file", std::string(full_filename)},
       {"message", std::move(message)},
       {"sev", severity}});
}

int32_t CherryVisDumperModule::getUnitTaskId(State* s, Unit* unit) {
  TaskData t = s->board()->taskDataWithUnit(unit);
  if (t.owner && t.task) {
    auto it = taskToId_.find(t.task);
    int32_t task_id;
    if (it == taskToId_.end()) {
      task_id = taskToId_.size();
      taskToId_[t.task] = task_id;
      tasks_.push_back(
          {{"name", t.task->getName()}, {"owner", t.owner->name()}});
      return task_id;
    } else {
      return it->second;
    }
  }
  return -1;
}

void CherryVisDumperModule::writeGameSummary(
    State* s,
    std::string const& file) {
  json summary(
      {
          // Required fields
          {"p0_name", "cherrypi"},
          {"p0_race", s->myRace()._to_string()},
          {"p0_win", s->won()},
          {"p0_cherrypi_crash", false}, // If we are here, we didnt crash...
          {"p1_name", s->board()->get<std::string>(Blackboard::kEnemyNameKey)},
          {"p1_race", s->raceFromClient(s->firstOpponent())._to_string()},
          {"p1_win", s->lost() && s->currentFrame() != 0},
          {"draw", s->currentFrame() == 0},
          {"game_duration_frames", s->currentFrame()},
          // Additional fields
          {"map", s->mapName()},
          {"cp_opening_bo",
           s->board()->get<std::string>(Blackboard::kOpeningBuildOrderKey)},
          {"cp_final_bo",
           s->board()->get<std::string>(Blackboard::kBuildOrderKey)},
      });

  // ... add other metrics below, and see them in the ladder visualizer :)

  // Write to file
  std::ofstream gameSummary(file);
  gameSummary << summary.dump(-1, ' ', true);
  gameSummary.close();
}

struct BoardValueToString {
  std::string operator()(bool d) const {
    return d ? "true" : "false";
  }

  std::string operator()(std::string const& d) const {
    return d;
  }

  std::string operator()(Position const& d) const {
    return cherrypi::utils::positionString(d);
  }

  std::string operator()(std::shared_ptr<SharedController> d) const {
    return d ? d->getName() : "null";
  }

  std::string operator()(std::unordered_map<int, int> const& d) const {
    return "map of size " + std::to_string(d.size());
  }

  template <typename T>
  std::string operator()(T const& d) const {
    return std::to_string(d);
  }
};

std::string CherryVisDumperModule::getBoardValueAsString(
    Blackboard::Data const& value) {
  return mapbox::util::apply_visitor(BoardValueToString(), value);
}
}
