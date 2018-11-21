/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#include "blackboard.h"
#include "buildtype.h"
#include "cherrypi.h"
#include "module.h"
#include "unitsinfo.h"

#include <nlohmann/json.hpp>
#include <thread>

namespace cherrypi {

class CherryVisLogSink;

/**
 * Records bot internal state and dumps it to a file readable by CherryVis.
 */
class CherryVisDumperModule : public Module {
 public:
  virtual ~CherryVisDumperModule() = default;

  void setReplayFile(std::string const& replayFile) {
    replayFileName_ = replayFile;
  }

  virtual void step(State* s) override;
  virtual void onGameStart(State* s) override;
  virtual void onGameEnd(State* s) override;

  void handleLog(
      google::LogSeverity severity,
      const char* full_filename,
      std::string message);

  static void writeGameSummary(State* final_state, std::string const& file);

 protected:
  struct UnitData {
    int32_t lastSeenTask = -1;
  };

  int32_t getUnitTaskId(State* s, Unit* unit);
  std::string getBoardValueAsString(Blackboard::Data const& value);

  std::string replayFileName_;
  // We assign IDs to tasks so we can store them only once
  std::unordered_map<std::shared_ptr<Task>, int32_t> taskToId_;
  std::vector<nlohmann::json> tasks_;
  // Blackboard
  std::unordered_map<std::string, std::string> boardKnownValues_;
  nlohmann::json boardUpdates_;
  // Units
  std::unordered_map<UnitId, UnitData> unitsInfos_;
  std::unordered_map<std::string /* unit_id */, nlohmann::json> unitsUpdates_;
  std::unordered_map<std::string /* frame_id */, std::vector<nlohmann::json>>
      unitsFirstSeen_;
  // Logs handling
  std::vector<nlohmann::json> logs_;
  std::unique_ptr<CherryVisLogSink> logSink_; // First destructed in ~dtor
};

class CherryVisLogSink : public google::LogSink {
 public:
  CherryVisLogSink(CherryVisDumperModule* module)
      : module_(module), threadId_(std::this_thread::get_id()) {
    google::AddLogSink(this);
  }
  virtual ~CherryVisLogSink() {
    google::RemoveLogSink(this);
  }

  virtual void send(
      google::LogSeverity severity,
      const char* full_filename,
      const char* base_filename,
      int line,
      const struct ::tm* tm_time,
      const char* message,
      size_t message_len) override {
    // TODO: In case of self-play, the opponents share the same thread_id
    if (threadId_ == std::this_thread::get_id()) {
      module_->handleLog(
          severity, full_filename, std::string(message, message_len));
    }
  }

 protected:
  CherryVisDumperModule* module_;
  std::thread::id threadId_;
};
}
