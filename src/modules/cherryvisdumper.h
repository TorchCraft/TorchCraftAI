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
#include "state.h"
#include "unitsinfo.h"

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <variant>

namespace cherrypi {

class CherryVisLogSink;

#define CVIS_LOG(state)                                    \
  if (auto _cvisDumper = state->board()->getTraceDumper()) \
  _cvisDumper->getGlobalLogger().getStream(state, __FILE__, __LINE__)

#define CVIS_LOG_UNIT(state, unit)                         \
  if (auto _cvisDumper = state->board()->getTraceDumper()) \
  _cvisDumper->getUnitLogger(unit).getStream(state, __FILE__, __LINE__)

/**
 * Records bot internal state and dumps it to a file readable by CherryVis.
 */
class CherryVisDumperModule : public Module {
 public:
  class Logger;
  using Dumpable = std::variant<Unit*, Position, int, float, std::string>;

  class Logger {
   public:
    template <typename T, typename _U>
    struct TypeInfo {
      static constexpr const char* kName = nullptr;
    };
#define ACCEPT_TYPE(TYPE)                       \
  template <typename _U>                        \
  struct TypeInfo<TYPE, _U> {                   \
    static constexpr const char* kName = #TYPE; \
  };
    ACCEPT_TYPE(Unit*);
    ACCEPT_TYPE(Position);
    ACCEPT_TYPE(int);
    ACCEPT_TYPE(float);
    ACCEPT_TYPE(std::string);
    ACCEPT_TYPE(Dumpable);

    class LoggerStream {
     public:
      friend class Logger;
      LoggerStream(State* state, Logger& logger, const char* filename, int line)
          : state_(state), logger_(logger), filename_(filename), line_(line) {}
      ~LoggerStream() {
        logger_.addMessage(
            state_, message_.str(), std::move(attachments_), filename_, line_);
      }

      template <typename T>
      LoggerStream& operator<<(T const& m) {
        message_ << m;
        return *this;
      }

      template <typename K, typename V>
      LoggerStream& operator<<(std::unordered_map<K, V> m) {
        static_assert(TypeInfo<K, void>::kName, "Map Key has invalid type");
        static_assert(TypeInfo<V, void>::kName, "Map Value has invalid type");
        attachments_.push_back({
            {"map", std::move(m)},
            {"key_type", TypeInfo<K, void>::kName},
            {"value_type", TypeInfo<V, void>::kName},
        });
        return *this;
      }

     protected:
      std::stringstream message_;
      std::vector<nlohmann::json> attachments_;
      State* state_;
      Logger& logger_;
      const char* filename_;
      int line_;
    };

    LoggerStream getStream(State* state, const char* filename, int line) {
      return LoggerStream(state, *this, filename, line);
    }

    void addMessage(
        State* state,
        std::string message,
        std::vector<nlohmann::json> attachments = {},
        const char* full_filename = "",
        int line = 0,
        google::LogSeverity severity = 0);

    nlohmann::json to_json() const {
      return logs_;
    }

   protected:
    std::vector<nlohmann::json> logs_;
  };

  virtual ~CherryVisDumperModule() = default;

  void setReplayFile(std::string const& replayFile) {
    replayFileName_ = replayFile;
  }

  virtual void step(State* s) override;
  virtual void onGameStart(State* s) override;
  virtual void onGameEnd(State* s) override;

  Logger& getGlobalLogger() {
    return trace_.logs_;
  }

  Logger& getUnitLogger(Unit* u) {
    return trace_.unitsLogs_[std::to_string(u->id)];
  }

  void onDrawCommand(State* s, tc::Client::Command const& command);

  template <typename T, typename V, typename W>
  void addTree(
      State* s,
      std::string const& name,
      T dump_node,
      V get_children,
      W root);
  void dumpTensorsSummary(
      State* s,
      std::unordered_map<std::string, ag::Variant> const& tensors);

  /**
   * Dumps a heatmap that can be displayed as overlay of the terrain.
   * \param{tensors} is expected to be a dict of 2 dimensionnal at::Tensor
   * in a [y, x] order.
   */
  void dumpTerrainHeatmaps(
      State* s,
      std::unordered_map<std::string, ag::Variant> const& tensors,
      std::array<int, 2> const& topLeftPixel,
      std::array<float, 2> const& scalingToPixels);

  void dumpGameValue(State* s, std::string const& key, float value) {
    trace_.gameValues_[std::to_string(s->currentFrame())][key] = value;
  }

  static void writeGameSummary(State* final_state, std::string const& file);

  class TreeNode : public std::stringstream {
   public:
    std::vector<std::shared_ptr<TreeNode>> children;

    void setModule(std::string name) {
      node["module"] = name;
    }
    void setFrame(FrameNum f) {
      node["frame"] = f;
    }
    void setId(int32_t id, std::string prefix = "") {
      node["id"] = id;
      node["type_prefix"] = prefix;
    }
    void addUnitWithProb(Unit* unit, float proba) {
      prob_distr.push_back({{"type_prefix", "i"},
                            {"id", unit ? unit->id : -1},
                            {"proba", proba}});
    }
    nlohmann::json to_json() {
      if (!str().empty()) {
        node["description"] = str();
      }
      if (!prob_distr.empty()) {
        node["distribution"] = prob_distr;
      }
      return node;
    }

   protected:
    std::unordered_map<std::string, nlohmann::json> node;
    std::vector<nlohmann::json> prob_distr;
  };

 protected:
  struct UnitData {
    int32_t lastSeenTask = -1;
    int32_t lastSeenType = -1;
  };
  struct TreeData {
    nlohmann::json metadata;
    std::shared_ptr<TreeNode> graph;
    std::vector<std::shared_ptr<TreeNode>> allNodes;
  };
  struct TraceData {
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

    // Logs
    Logger logs_;
    std::unordered_map<std::string /* unit_id */, Logger> unitsLogs_;

    // Draw commands
    std::unordered_map<std::string /* frame_id */, std::vector<nlohmann::json>>
        drawCommands_;

    // Graphs
    std::unordered_map<std::string /* filename */, TreeData> trees_;
    std::vector<nlohmann::json> treesMetadata_;

    // Tensors
    std::unordered_map<
        std::string /* filename */,
        std::unordered_map<std::string /* frame */, nlohmann::json>>
        tensors_;
    std::unordered_map<std::string /* name */, std::string /* filename */>
        tensorNameToFile_;
    std::vector<nlohmann::json> heatmapsMetadata_;
    std::unordered_map<std::string /* frame_id */, std::vector<nlohmann::json>>
        tensorsSummary_;

    // Game values
    std::unordered_map<std::string /* frame_id */, nlohmann::json> gameValues_;
  };

  int32_t getUnitTaskId(State* s, Unit* unit);
  std::string getBoardValueAsString(Blackboard::Data const& value);
  void dumpGameUpcs(State* s);
  void writeTrees(std::string const& dumpDirectory);
  void writeTensors(std::string const& dumpDirectory);
  nlohmann::json getTensorSummary(std::string const& name, at::Tensor const& t);
  nlohmann::json getTensor1d(at::Tensor const& t);

  std::string replayFileName_;
  TraceData trace_;
  std::unique_ptr<CherryVisLogSink> logSink_;
};

class CherryVisLogSink : public google::LogSink {
 public:
  CherryVisLogSink(CherryVisDumperModule* module, State* state)
      : module_(module), state_(state), threadId_(std::this_thread::get_id()) {
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
      module_->getGlobalLogger().addMessage(
          state_,
          std::string(message, message_len),
          {},
          full_filename,
          line,
          severity);
    }
  }

 protected:
  CherryVisDumperModule* module_;
  State* state_;
  std::thread::id threadId_;
};

template <typename T, typename V, typename W>
void CherryVisDumperModule::addTree(
    State* s,
    std::string const& name,
    T dump_node,
    V get_children,
    W root) {
  std::string filename = "tree__" + std::to_string(trace_.trees_.size()) +
      "__f" + std::to_string(s->currentFrame()) + ".json.zstd";
  TreeData& g = trace_.trees_[filename];
  g.graph = std::make_shared<TreeNode>();
  uint32_t nodesCount = 0;
  std::vector<std::pair<W /* node */, std::shared_ptr<TreeNode>>> todo;

  // Process root
  dump_node(root, g.graph);
  ++nodesCount;
  g.allNodes.push_back(g.graph);
  std::vector<W> childs = get_children(root);
  for (auto c : childs) {
    todo.push_back(std::make_pair(c, g.graph));
  }

  // Process queue
  while (!todo.empty()) {
    auto doing = todo.back();
    todo.pop_back();

    // Node
    doing.second->children.emplace_back(std::make_shared<TreeNode>());
    dump_node(doing.first, doing.second->children.back());
    ++nodesCount;
    g.allNodes.push_back(doing.second->children.back());
    std::vector<W> childs = get_children(doing.first);
    for (auto c : childs) {
      todo.push_back(std::make_pair(c, doing.second->children.back()));
    }
  }
  g.metadata = {
      {"frame", s->currentFrame()},
      {"name", name},
      {"nodes", nodesCount},
      {"filename", filename},
  };
  trace_.treesMetadata_.push_back(g.metadata);
}

void to_json(nlohmann::json& json, CherryVisDumperModule::Logger const& logger);
void to_json(
    nlohmann::json& json,
    CherryVisDumperModule::Dumpable const& logger);
void to_json(nlohmann::json& json, Unit const* unit);
void to_json(nlohmann::json& json, Position const& p);
} // namespace cherrypi
