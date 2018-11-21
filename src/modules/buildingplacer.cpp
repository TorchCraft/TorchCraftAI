/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "buildingplacer.h"
#include "builderhelper.h"

#include "state.h"
#include "task.h"
#include "utils.h"

#include <glog/logging.h>

DEFINE_string(
    bp_model,
    "",
    "Path to building placer model");

namespace cherrypi {

REGISTER_SUBCLASS_0(Module, BuildingPlacerModule);

namespace {

/// Proxies the task to create a building and re-tries at different locations if
/// necessary.
class BuildingPlacerTask : public ProxyTask {
 private:
  bool reserved_ = false;

 public:
  std::shared_ptr<UPCTuple> sourceUpc;
  BuildType const* type;
  Position pos;
  /// Need to send another UPC for this building?
  bool respawn = false;

  BuildingPlacerTask(
      UpcId targetUpcId,
      UpcId upcId,
      std::shared_ptr<UPCTuple> sourceUpc,
      BuildType const* type,
      Position pos)
      : ProxyTask(targetUpcId, upcId),
        sourceUpc(std::move(sourceUpc)),
        type(type),
        pos(std::move(pos)) {}
  virtual ~BuildingPlacerTask() = default;

  UpcId targetUpcId() {
    return targetUpcId_;
  }

  void setTarget(UpcId targetUpcId) {
    targetUpcId_ = targetUpcId;
  }

  void setPosition(Position p) {
    pos = std::move(p);
  }

  void reserveLocation(State* state) {
    if (!reserved_) {
      VLOG(3) << "Reserve for " << utils::upcString(upcId()) << " ("
              << utils::buildTypeString(type) << " at " << pos << ")";
      builderhelpers::fullReserve(state->tilesInfo(), type, pos);
    }
    reserved_ = true;
  }

  void unreserveLocation(State* state) {
    if (reserved_) {
      VLOG(3) << "Unreserve for " << utils::upcString(upcId()) << " ("
              << utils::buildTypeString(type) << " at " << pos << ")";
      builderhelpers::fullUnreserve(state->tilesInfo(), type, pos);
    }
    reserved_ = false;
  }

  virtual void update(State* state) override {
    ProxyTask::update(state);

    if (finished()) {
      VLOG(2) << "Proxied building task for " << utils::upcString(upcId())
              << " (" << utils::buildTypeString(type) << " at " << pos
              << ") finished";
      if (status() == TaskStatus::Failure) {
        VLOG(2) << "Proxied building task for " << utils::upcString(upcId())
                << " (" << utils::buildTypeString(type) << " at " << pos
                << ") failed; scheduling retry";
        respawn = true;
        setStatus(TaskStatus::Unknown);
        target_ = nullptr;
        targetUpcId_ = kInvalidUpcId;
      } else {
        unreserveLocation(state);
      }
    }
  }

  virtual void cancel(State* state) override {
    ProxyTask::cancel(state);
    unreserveLocation(state);
  }
};

} // namespace

void BuildingPlacerModule::step(State* state) {
  auto board = state->board();

  // Cache BWEM base locations
  if (baseLocations_.empty()) {
    for (auto& area : state->areaInfo().areas()) {
      baseLocations_.insert(
          area.baseLocations.begin(), area.baseLocations.end());
    }
  }

#ifdef HAVE_TORCH
  // Fully initialize the model by doing a dummy forward pass in the first
  // frame; we'll have enough time there then.
  if (firstStep_) {
    firstStep_ = false;

    // We'll also initialize the static map features now
    staticData_ = std::make_shared<BuildingPlacerSample::StaticData>(state);

    auto upc = UPCTuple();
    upc.command[Command::Create] = 1;
    upc.state = UPCTuple::BuildTypeMap{{buildtypes::Zerg_Hatchery, 1}};
    upcWithPositionForBuilding(state, upc, buildtypes::Zerg_Hatchery);
  }
#endif // HAVE_TORCH

  for (auto const& upct : board->upcsWithSharpCommand(Command::Create)) {
    auto upcId = upct.first;
    auto& upc = *upct.second;

    // Do we know what we want?
    auto ctMax = upc.createTypeArgMax();
    if (ctMax.second < 0.99f) {
      VLOG(4) << "Not sure what we want? argmax over build types ="
              << ctMax.second;
      continue;
    }
    BuildType const* type = ctMax.first;

    std::shared_ptr<UPCTuple> newUpc;
    if (type->isBuilding && type->builder->isWorker) {
      newUpc = upcWithPositionForBuilding(state, upc, type);
    }

    // Ignore the UPC if we can't determine a position
    if (newUpc == nullptr) {
      continue;
    }

    // Post new UPC along with a ProxyTask
    auto pos = newUpc->position.get<Position>();
    auto newUpcId = board->postUPC(std::move(newUpc), upcId, this);
    if (newUpcId >= 0) {
      board->consumeUPC(upcId, this);
      auto task = std::make_shared<BuildingPlacerTask>(
          newUpcId, upcId, upct.second, type, pos);
      task->reserveLocation(state);
      board->postTask(std::move(task), this, true);
    }
  }

  // We need to update the upc id of any SetCreatePriority commands whose
  // Create task we are proxying.
  for (auto const& upct :
       board->upcsWithSharpCommand(Command::SetCreatePriority)) {
    auto upcId = upct.first;
    auto& upc = *upct.second;
    if (upc.state.is<UPCTuple::SetCreatePriorityState>()) {
      auto st = upc.state.get_unchecked<UPCTuple::SetCreatePriorityState>();
      for (auto& task : board->tasksOfModule(this)) {
        if (task->upcId() == std::get<0>(st)) {
          auto bptask = std::static_pointer_cast<BuildingPlacerTask>(task);
          std::shared_ptr<UPCTuple> newUpc = std::make_shared<UPCTuple>(upc);
          std::get<0>(st) = bptask->targetUpcId();
          newUpc->state = st;
          auto newUpcId = board->postUPC(std::move(newUpc), upcId, this);
          if (newUpcId >= 0) {
            board->consumeUPC(upcId, this);
          }
          break;
        }
      }
    }
  }

  // Any scheduled retries?
  for (auto& task : board->tasksOfModule(this)) {
    auto bptask = std::static_pointer_cast<BuildingPlacerTask>(task);
    if (!bptask->respawn) {
      continue;
    }

    bptask->unreserveLocation(state);

    std::shared_ptr<UPCTuple> newUpc;
    if (bptask->type->isBuilding && bptask->type->builder->isWorker) {
      newUpc = upcWithPositionForBuilding(
          state, *bptask->sourceUpc, bptask->type);
    }

    if (newUpc == nullptr) {
      continue;
    }

    auto pos = newUpc->position.get<Position>();
    auto newUpcId = board->postUPC(std::move(newUpc), bptask->upcId(), this);
    if (newUpcId >= 0) {
      bptask->respawn = false;
      bptask->setTarget(newUpcId);
      bptask->setPosition(pos);
      bptask->reserveLocation(state);
    }
  }
}

void BuildingPlacerModule::onGameStart(State* state) {
#ifdef HAVE_TORCH
  if (model_ == nullptr && !FLAGS_bp_model.empty()) {
    model_ = BuildingPlacerModel().make();
    VLOG(0) << "Loading building placer model from " << FLAGS_bp_model;
    try {
      ag::load(FLAGS_bp_model, model_);
      if (common::gpuAvailable()) {
        model_->to(torch::kCUDA);
      }
      model_->eval();
    } catch (std::exception const& ex) {
      LOG(WARNING) << "Error loading building placer model from "
                   << FLAGS_bp_model << ": " << ex.what();
      model_ = nullptr;
    }
  }

  if (model_ == nullptr) {
    LOG_IF(WARNING, !FLAGS_bp_model.empty())
        << "No building placer model set, falling back to built-in rules";
  } else {
    // We want a flattened output and a real probablity distribution.
    model_->flatten(true);
    model_->logprobs(false);
    // Model output should be masked so we'll only receive valid build
    // locations.
    model_->masked(true);
  }

  staticData_ = nullptr;
  firstStep_ = true;
#endif // HAVE_TORCH
  baseLocations_.clear();
}

std::shared_ptr<UPCTuple> BuildingPlacerModule::upcWithPositionForBuilding(
      State* state,
      UPCTuple const& upc,
      BuildType const* type) {
  // Perform placement with rules so we a) have a fallback and b) a candidate
  // area for the model.
  std::shared_ptr<UPCTuple> seedUpc =
      builderhelpers::upcWithPositionForBuilding(state, upc, type);
  // Use rule-based placement if
  // - there's no model
  // - a refinery is requested
  // - placement fails
  // - an expansion is requested
#ifdef HAVE_TORCH
  if (model_ == nullptr) {
    return seedUpc;
  }
#endif // HAVE_TORCH
  if (type->isRefinery || seedUpc == nullptr) {
    return seedUpc;
  }
  if (type->isResourceDepot) {
    // For expansions, trust the position determined by the build order.
    // We check for expansions by just comparing the sharp position in the
    // original UPC to all possible base positions.
    Position upcPos;
    float prob;
    std::tie(upcPos, prob) = upc.positionArgMax();
    if (prob > 0.99f) {
      // Base locations are center-of-building, but positions in the UPC will
      // refer to the top left.
      Position basePos = upcPos + Position(8, 6);
      if (baseLocations_.find(basePos) != baseLocations_.end()) {
        VLOG(1) << "Assuming planned expansion at " << upcPos
                << ", not placing with model";
        return seedUpc;
      }
    }
  }

#ifdef HAVE_TORCH
  // Prepare input sample
  auto sampleUpc = std::make_shared<UPCTuple>(*seedUpc);
  Position seedPos;
  float prob;
  std::tie(seedPos, prob) = seedUpc->positionArgMax();
  if (prob < 0.99f) {
    throw std::runtime_error(
        "Unexpected low probability for pre-selected building location");
  }
  sampleUpc->position = state->areaInfo().tryGetArea(seedPos);
  sampleUpc->scale = 1;

  BuildingPlacerSample sample(state, sampleUpc, staticData_.get());
  auto batch = model_->makeInputBatch({sample});

  // The model outputs a probability distribution across every position and
  // always operates on batches -- get rid of that dimensions by [0].
  torch::NoGradGuard guard;
  auto output = model_->forward(batch);
  auto pOut = output["output"][0];
  auto action = std::get<1>(pOut.max(0)).item<int32_t>();
  VLOG(3) << fmt::format(
      "Output for {}: {}",
      utils::buildTypeString(type),
      common::tensorStats(pOut));

  // Translate to 2-dimensional action space (i.e. 2D walktile position)
  Position pos = sample.offsetToAction(action);

  // Re-use UPC from above for convenience and simply replace position
  sampleUpc->position = pos;
  VLOG(1) << "Seed pos " << seedPos << ", predicted pos " << pos;
  return sampleUpc;
#else // HAVE_TORCH
  return seedUpc;
#endif // HAVE_TORCH
}

} // namespace cherrypi
