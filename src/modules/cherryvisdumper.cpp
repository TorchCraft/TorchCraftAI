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

#include <common/assert.h>
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
  ASSERT(s);
  std::string frameNow = std::to_string(s->currentFrame());
  // Dump units updates
  for (auto unit : s->unitsInfo().visibleUnits()) {
    // Do not include units until they are properly visible
    // This filters out units being constructed
    if (!unit->flag(tc::Unit::Flags::Targetable)) {
      continue;
    }
    if (trace_.unitsInfos_.find(unit->id) == trace_.unitsInfos_.end()) {
      trace_.unitsFirstSeen_[frameNow].push_back({
          {"id", unit->id},
          {"type", unit->type->unit},
          {"x", unit->unit.pixel_x},
          {"y", unit->unit.pixel_y},
      });
    }
    UnitData& infos = trace_.unitsInfos_[unit->id];
    int32_t current_task_id = getUnitTaskId(s, unit);
    if (infos.lastSeenTask != current_task_id) {
      json& j = trace_.unitsUpdates_[std::to_string(unit->id)][frameNow];
      j["task"] = current_task_id;
      infos.lastSeenTask = current_task_id;
    }
    if (infos.lastSeenType != unit->type->unit) {
      json& j = trace_.unitsUpdates_[std::to_string(unit->id)][frameNow];
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
    trace_.boardUpdates_[frameNow] = std::move(currentFrameUpdates);
  }
  if (!persistDrawCommands_) {
    trace_.drawCommands_[frameNow];
  }
}

void CherryVisDumperModule::onGameStart(State* state) {
  logSink_.reset();
  trace_ = {};
  if (state && enableLogsSink_) {
    logSink_ = std::make_unique<CherryVisLogSink>(this, state);
    if (FLAGS_trace_upc_details) {
      state->board()->upcStorage()->setPersistent(true);
    }
  }
}

std::string CherryVisDumperModule::parseReplayPath(std::string parsed) {
  // See corresponding code in OpenBW's version of BWAPI
  // https://github.com/OpenBW/bwapi/blob/develop-openbw/bwapi/BWAPI/Source/BWAPI/GameEvents.cpp#L380
  std::replace(parsed.begin(), parsed.end(), '\\', '/');
  // Remove illegal characters
  parsed.erase(
      std::remove_if(
          parsed.begin(),
          parsed.end(),
          [](char c) {
            if ((unsigned char)c >= 128)
              return false;
            if (c >= 'a' && c <= 'z')
              return false;
            if (c >= 'A' && c <= 'Z')
              return false;
            if (c >= '0' && c <= '9')
              return false;
            if (c == '-' || c == '-')
              return false;
            if (c == '_' || c == '_')
              return false;
            if (c == '.')
              return false;
            if (c == '/')
              return false;
            if (c == ' ')
              return false;
            return true;
          }),
      parsed.end());
  return parsed;
}

void CherryVisDumperModule::onGameEnd(State* state) {
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
  if (state) {
    dumpGameUpcs(state);
  }

  auto tensorsData = trace_.getTensorsData();

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
                   {"heatmaps", tensorsData->heatmapsMetadata_},
                   {"tensors_summaries", tensorsData->tensorsSummary_},
                   {"game_values", trace_.gameValues_},
                   {"_version", 0}};

  // TODO: We need to parse "replayFileName_"
  // e.g. %BOTNAME%_%BOTRACE%
  try {
    std::string dumpDirectory = getDumpDirectory();
    common::fsutils::mkdir(dumpDirectory);
    VLOG(1) << "Dumping bot trace to " << dumpDirectory;

    // trace.json
    std::string traceFile = dumpDirectory + "trace.json";
    common::zstd::ofstream replayFile(traceFile);
    replayFile << bot_dump.dump(-1, ' ', true);
    replayFile.close();

    // game_summary.json
    if (state) {
      writeGameSummary(state, dumpDirectory + "game_summary.json");
    }

    // tree__XXXX.json.zstd
    writeTrees(dumpDirectory);
  } catch (std::exception const& e) {
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
  trace_.drawCommands_[std::to_string(currentFrame(s))].push_back({
      {"code", command.code},
      {"args", command.args},
      {"str", command.str},
      {"cherrypi_ids_args_indices", cherryPiUnitsIdsArgs},
  });
}

void CherryVisDumperModule::flushDrawCommands(State* s) {
  trace_.drawCommands_[std::to_string(currentFrame(s))];
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
  ASSERT(s);
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

  std::string operator()(torch::Tensor d) const {
    return "tensor " + common::tensorInfo(d);
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

    common::zstd::ofstream treeFile(dumpDirectory + g.first);
    treeFile << nodes[allNodes[0]].dump(-1, ' ', true);
    treeFile.close();
  }
}

nlohmann::json CherryVisDumperModule::getTensorSummary(
    std::string const& name,
    at::Tensor const& t) {
  ASSERT(t.numel() > 0, "Cant produce a CherryVis summary for an empty Tensor");
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

FrameNum CherryVisDumperModule::currentFrame(State* s) const {
  if (s) {
    return s->currentFrame();
  }
  ASSERT(
      currentFrame_,
      "state=nullptr, please provide a frame number with setCurrentFrame");
  return currentFrame_.value();
}

void CherryVisDumperModule::setMultiDump(std::string cvisSuffix) {
  if (cvisSuffix.empty()) {
    // Try to autogenerate a suffix
    // NOTE: This is a race condition
    int i = 2;
    while (fsutils::isdir(fmt::format("{}.civs.{}", replayFileName_, i))) {
      ++i;
    }
    fsutils::mkdir(fmt::format("{}.civs.{}", replayFileName_, i));
    cvisSuffix = std::to_string(i);
  }
  cvisSuffix_ = "." + cvisSuffix;
}

std::unique_ptr<CherryVisDumperModule::TraceData::TraceTensors>
CherryVisDumperModule::TraceData::getTensorsData() {
  if (!tensorsData) {
    return std::make_unique<TraceTensors>();
  }
  // Ensure we finish any async operation
  tensorsData->tp.reset();
  return std::move(tensorsData);
}

void CherryVisDumperModule::TraceData::enqueueAsyncTensorOp(
    std::function<void(CherryVisDumperModule::TraceData::TraceTensors&)>&& fn) {
  if (!tensorsData) {
    tensorsData = std::make_unique<TraceTensors>();
    tensorsData->tp.emplace(1);
    tensorsData->tp->enqueue(
        []() { common::setCurrentThreadName("cvis_tensors"); });
  }
  tensorsData->tp->enqueue(std::move(fn), std::ref(*tensorsData.get()));
}

void CherryVisDumperModule::dumpTensorsSummary(
    State* s,
    std::unordered_map<std::string, ag::Variant> const& tensors) {
  trace_.enqueueAsyncTensorOp([currentFrame = currentFrame(s),
                               tensors](auto& tensorsTrace) {
    auto& frameSummaries =
        tensorsTrace.tensorsSummary_[std::to_string(currentFrame)];
    for (auto t : tensors) {
      if (!t.second.isTensor()) {
        LOG(ERROR) << "dumpTensorsSummary: tensors[" << t.first << "] is not "
                   << "a tensor - summary will not be dumped";
        continue;
      }
      // Skip tensors with 0 elements
      if (t.second.get().numel() == 0) {
        continue;
      }
      frameSummaries.push_back(getTensorSummary(t.first, t.second.get()));
    }
  });
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
    // Skip tensors with 0 elements
    if (t.second.get().numel() == 0) {
      continue;
    }

    trace_.enqueueAsyncTensorOp(
        [topLeftPixel,
         scalingToPixels,
         tensor = t.second,
         key = t.first,
         dumpDirectory = getDumpDirectory(),
         currentFrame = currentFrame(s)](auto& tensorsTrace) -> void {
          auto values = tensor.get().to(at::kCPU).to(at::kFloat);
          std::array<int64_t, 2> size = {values.size(0), values.size(1)};
          auto valuesA = values.accessor<float, 2>();

          try {
            std::string& filename = tensorsTrace.tensorNameToFile_[key];
            if (filename.empty()) {
              filename = fmt::format(
                  "tensor__{}__f{}.json.zstd.stream",
                  tensorsTrace.tensors_.size(),
                  currentFrame);
              fsutils::mkdir(dumpDirectory);
              tensorsTrace.tensors_.emplace(filename, dumpDirectory + filename);
              tensorsTrace.heatmapsMetadata_.push_back(
                  {{"name", key},
                   {"filename", filename},
                   {"first_frame", currentFrame}});
            }
            std::vector<float> data(size[0] * size[1], 0.0f);
            for (auto y = 0; y < size[0]; ++y) {
              for (auto x = 0; x < size[1]; ++x) {
                data[x + y * size[1]] = valuesA[y][x];
              }
            }
            auto cvisTensor = nlohmann::json(
                {{"top_left_pixel", topLeftPixel},
                 {"scaling", scalingToPixels},
                 {"dimension", size},
                 {"data", std::move(data)},
                 {"summary", getTensorSummary(key, tensor.get())}});
            tensorsTrace.tensors_.at(filename)
                << nlohmann::json({{"key", std::to_string(currentFrame)},
                                   {"value", cvisTensor}})
                       .dump(-1, ' ', true);
          } catch (std::exception const& e) {
            VLOG(0) << "Unable to dump tensor " << key << ": " << e.what();
          }
        });
  }
}

std::string CherryVisDumperModule::getDumpDirectory() const {
  std::string parsedFilename = parseReplayPath(replayFileName_);
  return parsedFilename + ".cvis" + cvisSuffix_ + "/";
}

void CherryVisDumperModule::enableLogsSink(State* state, bool on) {
  if (on) {
    ASSERT(state);
    logSink_ = std::make_unique<CherryVisLogSink>(this, state);
  } else {
    logSink_.reset();
  }
  enableLogsSink_ = on;
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
