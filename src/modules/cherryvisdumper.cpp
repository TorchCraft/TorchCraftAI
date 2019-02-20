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
#include "state.h"
#include "upcstorage.h"
#include "utils.h"
#include "zstdstream.h"

#include <common/fsutils.h>

using json = nlohmann::json;
namespace fsutils = common::fsutils;

DEFINE_bool(
    trace_upc_details,
    false,
    "Dump UPCs with all their details - "
    "enables persistent storage of UPCs in the Blackboard");

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
    if (trace_.unitsInfos_.find(unit->id) == trace_.unitsInfos_.end()) {
      trace_.unitsFirstSeen_[std::to_string(s->currentFrame())].push_back({
          {"id", unit->id},
          {"type", unit->type->unit},
          {"x", unit->unit.pixel_x},
          {"y", unit->unit.pixel_y},
      });
    }
    UnitData& infos = trace_.unitsInfos_[unit->id];
    int32_t current_task_id = getUnitTaskId(s, unit);
    if (infos.lastSeenTask != current_task_id) {
      json& j = trace_.unitsUpdates_[std::to_string(unit->id)]
                                    [std::to_string(s->currentFrame())];
      j["task"] = current_task_id;
      infos.lastSeenTask = current_task_id;
    }
    if (infos.lastSeenType != unit->type->unit) {
      json& j = trace_.unitsUpdates_[std::to_string(unit->id)]
                                    [std::to_string(s->currentFrame())];
      j["type"] = unit->type->unit;
      infos.lastSeenType = unit->type->unit;
    }
  }

  // Dump blackboard updates
  json currentFrameUpdates = json::object();
  bool hasUpdates = false;
  s->board()->iterValues([this, &currentFrameUpdates, &hasUpdates](
                             std::string const& key,
                             Blackboard::Data const& value) {
    std::string valueStr(getBoardValueAsString(value));
    if (trace_.boardKnownValues_.find(key) == trace_.boardKnownValues_.end() ||
        trace_.boardKnownValues_[key] != valueStr) {
      trace_.boardKnownValues_[key] = valueStr;
      currentFrameUpdates[key] = valueStr;
      hasUpdates = true;
    }
  });
  if (hasUpdates) {
    trace_.boardUpdates_[std::to_string(s->currentFrame())] =
        std::move(currentFrameUpdates);
  }
  trace_.drawCommands_[std::to_string(s->currentFrame())];
}

void CherryVisDumperModule::onGameStart(State* s) {
  logSink_.reset();
  trace_ = TraceData();
  logSink_ = std::make_unique<CherryVisLogSink>(this, player_->state());
  if (FLAGS_trace_upc_details) {
    s->board()->upcStorage()->setPersistent(true);
  }
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

  // Dump all UPCs
  dumpGameUpcs(s);

  // Create JSON
  json bot_dump = {{"types_names", buildTypesToName},
                   {"tasks", trace_.tasks_},
                   {"logs", trace_.logs_},
                   {"units_logs", trace_.unitsLogs_},
                   {"units_updates", trace_.unitsUpdates_},
                   {"units_first_seen", trace_.unitsFirstSeen_},
                   {"board_updates", trace_.boardUpdates_},
                   {"draw_commands", trace_.drawCommands_},
                   {"trees", trace_.treesMetadata_},
                   {"heatmaps", trace_.heatmapsMetadata_},
                   {"tensors_summaries", trace_.tensorsSummary_},
                   {"game_values", trace_.gameValues_},
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

    // tree__XXXX.json.zstd
    writeTrees(dumpDirectory);

    // tensor__XXXX.json.zstd
    writeTensors(dumpDirectory);
  } catch (std::exception& e) {
    LOG(ERROR) << "Exception while writing bot trace for CVis: " << e.what();
  }
}

void CherryVisDumperModule::onDrawCommand(
    State* s,
    tc::Client::Command const& command) {
  std::vector<int> cherryPiUnitsIdsArgs;
  switch (command.code) {
    case tc::BW::Command::DrawUnitLine:
      cherryPiUnitsIdsArgs = {0, 1};
      break;
    case tc::BW::Command::DrawUnitPosLine:
    case tc::BW::Command::DrawUnitCircle:
      cherryPiUnitsIdsArgs = {0};
      break;
    default:
      break;
  }
  trace_.drawCommands_[std::to_string(s->currentFrame())].push_back({
      {"code", command.code},
      {"args", command.args},
      {"str", command.str},
      {"cherrypi_ids_args_indices", cherryPiUnitsIdsArgs},
  });
}

int32_t CherryVisDumperModule::getUnitTaskId(State* s, Unit* unit) {
  TaskData t = s->board()->taskDataWithUnit(unit);
  if (t.owner && t.task) {
    auto it = trace_.taskToId_.find(t.task);
    int32_t task_id;
    if (it == trace_.taskToId_.end()) {
      task_id = trace_.taskToId_.size();
      trace_.taskToId_[t.task] = task_id;
      trace_.tasks_.push_back({{"name", t.task->getName()},
                               {"owner", t.owner->name()},
                               {"upc_id", t.task->upcId()},
                               {"creation_frame", t.creationFrame}});
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
  json summary({
      // Required fields
      {"p0_name", "cherrypi"},
      {"p0_race", s->myRace()._to_string()},
      {"p0_win", s->won()},
      {"p0_cherrypi_crash", false}, // If we are here, we didnt crash...
      {"p1_race", s->raceFromClient(s->firstOpponent())._to_string()},
      {"p1_win", s->lost() && s->currentFrame() != 0},
      {"draw", s->currentFrame() == 0},
      {"game_duration_frames", s->currentFrame()},
      {"map", s->mapName()},
  });

  auto addBlackboardValue = [&](std::string const& k, std::string const& bk) {
    if (s->board()->hasKey(bk) && s->board()->get(bk).is<std::string>()) {
      summary[k] = s->board()->get<std::string>(bk);
    }
  };
  addBlackboardValue("p1_name", Blackboard::kEnemyNameKey);
  addBlackboardValue("cp_opening_bo", Blackboard::kOpeningBuildOrderKey);
  addBlackboardValue("cp_final_bo", Blackboard::kBuildOrderKey);

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

void CherryVisDumperModule::dumpGameUpcs(State* s) {
  // 1- Construct the tree in the right order
  std::unordered_map<UpcId /* source */, std::vector<UpcId> /* children */>
      tree;
  UpcStorage* storage = s->board()->upcStorage();
  auto const& allUpcs = storage->getAllUpcs();
  for (auto const& upcPost : allUpcs) {
    tree[upcPost.sourceId].push_back(upcPost.upcId);
  }

  // 2- Dump!
  auto dump_node = [&allUpcs](UpcId id, std::shared_ptr<TreeNode> node) {
    node->setId(id, "u");
    if (id == 0) {
      node->setFrame(0);
      node->setModule("Init");
      *node << "Origin";
      return;
    }
    auto upc = allUpcs[id - 1];
    node->setFrame(upc.frame);
    node->setModule(upc.module->name());
    if (upc.upc) {
      for (auto u : upc.upc->unit) {
        node->addUnitWithProb(u.first, u.second);
      }
    }
    *node << utils::upcString(upc.upc, id);
  };
  auto get_children = [&tree](UpcId id) { return tree[id]; };
  addTree(s, "gameupcs", dump_node, get_children, UpcId(0));
}

void CherryVisDumperModule::writeTrees(std::string const& dumpDirectory) {
  std::vector<nlohmann::json> children;
  for (auto& g : trace_.trees_) {
    auto const& allNodes = g.second.allNodes;
    std::unordered_map<std::shared_ptr<TreeNode>, nlohmann::json> nodes(
        allNodes.size());
    // Given the order in which we dump the trees
    // And because the tree is acyclic and directed,
    //   the following works
    for (int i = allNodes.size() - 1; i >= 0; --i) {
      auto curNode = allNodes[i];
      nodes[curNode] = curNode->to_json();
      children.clear();
      for (auto c : curNode->children) {
        children.push_back(std::move(nodes[c]));
      }
      nodes[curNode]["children"] = std::move(children);
    }

    zstd::ofstream treeFile(dumpDirectory + g.first);
    treeFile << nodes[allNodes[0]].dump(-1, ' ', true);
    treeFile.close();
  }
}

nlohmann::json CherryVisDumperModule::getTensorSummary(
    std::string const& name,
    at::Tensor const& t) {
  // median requires contiguous tensors
  auto tensor = t.to(at::kFloat).contiguous();
  std::vector<int> shape;
  for (int d = 0; d < tensor.dim(); ++d) {
    shape.push_back(tensor.size(d));
  }
  auto min = tensor.min().to(at::kCPU).item<float>();
  auto max = tensor.max().to(at::kCPU).item<float>();
  constexpr int kHistNumBuckets = 10;
  return {
      {"shape", shape},
      {"type", t.type().toString()},
      {"min", min},
      {"max", max},
      {"mean", tensor.mean().to(at::kCPU).item<float>()},
      {"std", tensor.std().to(at::kCPU).item<float>()},
      {"median", tensor.median().to(at::kCPU).item<float>()},
      {"absmedian", tensor.abs().median().to(at::kCPU).item<float>()},
      {"name", name},
      {"hist",
       {{"num_buckets", kHistNumBuckets},
        {"min", min},
        {"max", max},
        // Tensor::histc is only available on CPU
        {"values",
         getTensor1d(tensor.to(at::kCPU).histc(kHistNumBuckets, min, max))}}}};
}

nlohmann::json CherryVisDumperModule::getTensor1d(at::Tensor const& t) {
  auto tensor = t.to(at::kFloat).to(at::kCPU);
  auto tensorA = tensor.accessor<float, 1>();
  std::vector<float> values(tensor.size(0), 0);
  for (auto i = 0; i < tensor.size(0); ++i) {
    values[i] = tensorA[i];
  }
  return values;
}

void CherryVisDumperModule::dumpTensorsSummary(
    State* s,
    std::unordered_map<std::string, ag::Variant> const& tensors) {
  auto& frameSummaries =
      trace_.tensorsSummary_[std::to_string(s->currentFrame())];
  for (auto t : tensors) {
    if (!t.second.isTensor()) {
      LOG(ERROR) << "dumpTensorsSummary: tensors[" << t.first << "] is not "
                 << "a tensor - summary will not be dumped";
      continue;
    }
    frameSummaries.push_back(getTensorSummary(t.first, t.second.get()));
  }
}

void CherryVisDumperModule::dumpTerrainHeatmaps(
    State* s,
    std::unordered_map<std::string, ag::Variant> const& tensors,
    std::array<int, 2> const& topLeftPixel,
    std::array<float, 2> const& scalingToPixels) {
  for (auto const& t : tensors) {
    if (!t.second.isTensor()) {
      LOG(ERROR) << "dumpTerrainHeatmaps: tensors[" << t.first << "] is not "
                 << "a tensor - summary will not be dumped";
      continue;
    }
    if (t.second.get().dim() != 2) {
      LOG(ERROR) << "Heatmap " << t.first << " has dimension "
                 << t.second.get().dim() << " but should be 2";
      continue;
    }
    auto values = t.second.get().to(at::kCPU).to(at::kFloat);
    std::array<int64_t, 2> size = {values.size(0), values.size(1)};
    auto valuesA = values.accessor<float, 2>();

    std::string& filename = trace_.tensorNameToFile_[t.first];
    if (filename.empty()) {
      filename = fmt::format(
          "tensor__{}__f{}.json.zstd",
          trace_.tensors_.size(),
          s->currentFrame());
      trace_.heatmapsMetadata_.push_back({
          {"name", t.first},
          {"filename", filename},
          {"first_frame", s->currentFrame()},
      });
    }
    std::vector<float> data(size[0] * size[1], 0.0f);
    for (auto y = 0; y < size[0]; ++y) {
      for (auto x = 0; x < size[1]; ++x) {
        data[x + y * size[1]] = valuesA[y][x];
      }
    }

    trace_.tensors_[filename][std::to_string(s->currentFrame())] =
        nlohmann::json(
            {{"top_left_pixel", topLeftPixel},
             {"scaling", scalingToPixels},
             {"dimension", size},
             {"data", data},
             {"summary", getTensorSummary(t.first, t.second.get())}});
  }
}

void CherryVisDumperModule::writeTensors(std::string const& dumpDirectory) {
  for (auto& t : trace_.tensors_) {
    zstd::ofstream tensorFile(dumpDirectory + t.first);
    tensorFile << nlohmann::json(t.second).dump(-1, ' ', true);
    tensorFile.close();
  }
}

// ========= Logger
void CherryVisDumperModule::Logger::addMessage(
    State* state,
    std::string message,
    std::vector<nlohmann::json> attachments,
    const char* full_filename,
    int line,
    google::LogSeverity severity) {
  logs_.push_back({{"frame", state->currentFrame()},
                   {"attachments", std::move(attachments)},
                   {"file", std::string(full_filename)},
                   {"line", line},
                   {"message", std::move(message)},
                   {"sev", severity}});
}

void to_json(
    nlohmann::json& json,
    CherryVisDumperModule::Logger const& logger) {
  json = logger.to_json();
}

template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...)->overloaded<Ts...>;
void to_json(nlohmann::json& json, CherryVisDumperModule::Dumpable const& key) {
  json = std::visit(overloaded{[](auto v) { return nlohmann::json(v); }}, key);
}

void to_json(nlohmann::json& json, Unit const* unit) {
  json = nlohmann::json({{"type", "unit"}, {"id", unit->id}});
}

void to_json(nlohmann::json& json, Position const& p) {
  json = nlohmann::json({{"type", "position"}, {"x", p.x}, {"y", p.y}});
}

} // namespace cherrypi
