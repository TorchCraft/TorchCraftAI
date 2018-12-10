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
  void onDrawCommand(State* s, tc::Client::Command const& command);

  template <typename T, typename V, typename W>
  void addTree(
      State* s,
      std::string const& name,
      T dump_node,
      V get_children,
      W root);

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
      prob_distr.push_back(
          {{"type_prefix", "i"},
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

  int32_t getUnitTaskId(State* s, Unit* unit);
  std::string getBoardValueAsString(Blackboard::Data const& value);
  void dumpGameUpcs(State* s);
  void writeTrees(std::string const& dumpDirectory);

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
  // Draw commands
  std::unordered_map<std::string /* frame_id */, std::vector<nlohmann::json>>
      drawCommands_;
  // Graphs
  std::unordered_map<std::string /* filename */, TreeData> trees_;
  std::vector<nlohmann::json> treesMetadata_;
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

template <typename T, typename V, typename W>
void CherryVisDumperModule::addTree(
    State* s,
    std::string const& name,
    T dump_node,
    V get_children,
    W root) {
  std::string filename = "tree__" + std::to_string(trees_.size()) + "__f" +
      std::to_string(s->currentFrame()) + ".json.zstd";
  TreeData& g = trees_[filename];
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
  treesMetadata_.push_back(g.metadata);
}
}
