/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "rlbuildingplacer.h"

#include "common/autograd.h"
#include "modules/builderhelper.h"
#include "state.h"
#include "upc.h"
#include "utils.h"

#include <cpid/distributed.h>

#include <fmt/format.h>
#include <glog/logging.h>

using namespace cpid;
namespace dist = cpid::distributed;

namespace cherrypi {

REGISTER_SUBCLASS_0(Module, RLBuildingPlacerModule);

namespace {

constexpr char const* kOngoingConstructionsKey =
    "rlbuildingplacer_constructing";

template <typename F>
void visitPostData(State* state, UpcId upcId, F&& visitor) {
  auto storage = state->board()->upcStorage();
  auto post = storage->post(upcId);
  if (post == nullptr || post->data == nullptr) {
    // If this is the case, the UPC storage is not persistent, i.e. we're in
    // evaluation mode or don't want to record anything anyway.
    VLOG(2) << "No recorded post or data of " << utils::upcString(upcId);
    return;
  }
  auto data = std::static_pointer_cast<RLBPUpcData>(post->data);
  visitor(data);
}

void markConstructionStarted(State* state, UpcId upcId, UnitId unit) {
  VLOG(1) << "Building construction started for " << utils::upcString(upcId)
          << " (" << utils::unitString(state->unitsInfo().getUnit(unit)) << ")";
  visitPostData(state, upcId, [](std::shared_ptr<RLBPUpcData> data) {
    data->valid = true;
    data->started = true;
  });

  // It's possible (and actually likely) that this task will get cancelled
  // during construction due to build order re-planning. In this case, the
  // task will be destroyed. Hence, mark this building as being constructed
  // and check for completion in the module's step() function.
  auto constructions = state->board()->get(
      kOngoingConstructionsKey, std::unordered_map<int, int>());
  constructions[upcId] = unit;
  state->board()->post(kOngoingConstructionsKey, constructions);
}

void markConstructionFinished(State* state, UpcId upcId) {
  VLOG(1) << "Building construction finished for " << utils::upcString(upcId);
  visitPostData(state, upcId, [](std::shared_ptr<RLBPUpcData> data) {
    data->valid = true;
    data->started = true;
    data->finished = true;
  });

  // Remove corresponding entry from ongoing constructions
  auto constructions = state->board()->get(
      kOngoingConstructionsKey, std::unordered_map<int, int>());
  constructions.erase(upcId);
  state->board()->post(kOngoingConstructionsKey, constructions);
}

void markConstructionFailed(State* state, UpcId upcId) {
  VLOG(1) << "Building construction failed for " << utils::upcString(upcId);
  visitPostData(state, upcId, [](std::shared_ptr<RLBPUpcData> data) {
    // Just record that this is still a valid sample (as opposed to actions
    // that were never executed due to cancellation)
    data->valid = true;
  });

  // Remove corresponding entry from ongoing constructions
  auto constructions = state->board()->get(
      kOngoingConstructionsKey, std::unordered_map<int, int>());
  constructions.erase(upcId);
  state->board()->post(kOngoingConstructionsKey, constructions);
}

/**
 * A proxy task to track the outcome of building constructions.
 *
 * This is similar to BuildingPlacerTask; however, there's some extra work for
 * reliably tracking if construction starts and/or succeeds. The main difficulty
 * here is that autobuild will frequently cancel tasks, e.g. for re-planning,
 * and (a) generally, we want to ignore cancelled tasks (i.e. before
 * construction could start) during learning, but (b) we still want to track
 * successful placements despite of cancellation.
 */
class RLBuildingPlacerTask : public ProxyTask {
 private:
  bool reserved_ = false;
  bool reserveFailed_ = false;
  bool started_ = false;

 public:
  std::shared_ptr<UPCTuple> sourceUpc;
  BuildType const* type;
  Position pos;
  /// Need to send another UPC for this building?
  bool respawn = false;

  RLBuildingPlacerTask(
      UpcId targetUpcId,
      UpcId upcId,
      std::shared_ptr<UPCTuple> sourceUpc,
      BuildType const* type,
      Position pos)
      : ProxyTask(targetUpcId, upcId),
        sourceUpc(std::move(sourceUpc)),
        type(type),
        pos(std::move(pos)) {}
  virtual ~RLBuildingPlacerTask() = default;

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
      try {
        builderhelpers::fullReserve(state->tilesInfo(), type, pos);
        VLOG(3) << "Reserved for " << utils::upcString(upcId()) << " ("
                << utils::buildTypeString(type) << " at " << pos << ")";
      } catch (std::exception const& ex) {
        VLOG(0) << "Reserve for " << utils::upcString(upcId()) << " ("
                << utils::buildTypeString(type) << " at " << pos << ") failed";
        reserveFailed_ = true;
        return;
      }
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

  void checkIfBuildingStarted(State* state) {
    if (started_) {
      return;
    }

    // Monitor units that started morphing or that appeared at the requested
    // location.
    auto maxd = std::max(type->tileWidth, type->tileHeight) *
        tc::BW::XYWalktilesPerBuildtile;
    auto newUnits = state->unitsInfo().getNewUnits();
    auto newMorphingUnits = state->unitsInfo().getStartedMorphingUnits();
    std::copy(
        newMorphingUnits.begin(),
        newMorphingUnits.end(),
        std::back_inserter(newUnits));
    for (auto* unit : newUnits) {
      if (unit->isMine && unit->type == type &&
          utils::distance(unit, pos) <= maxd) {
        VLOG(2) << "Proxied building task for " << utils::upcString(upcId())
                << " (" << utils::upcString(targetUpcId_) << ": "
                << utils::buildTypeString(type) << " at " << pos
                << " found matching new/morphing unit "
                << utils::unitString(unit) << " at " << Position(unit);
        markConstructionStarted(state, targetUpcId_, unit->id);
        started_ = true;
        break;
      }
    }
  }

  virtual void update(State* state) override {
    if (reserveFailed_) {
      // The building location couldn't be reserved -- regard this as failure
      markConstructionFailed(state, targetUpcId_);
      cancel(state);
      return;
    }

    ProxyTask::update(state);

    if (finished()) {
      VLOG(2) << "Proxied building task for " << utils::upcString(upcId())
              << " (" << utils::upcString(targetUpcId_) << ": "
              << utils::buildTypeString(type) << " at " << pos
              << ") finished with status " << static_cast<int>(status());
      if (status() == TaskStatus::Failure) {
        VLOG(2) << "Proxied building task for " << utils::upcString(upcId())
                << " (" << utils::buildTypeString(type) << " at " << pos
                << ") failed; scheduling retry";
        markConstructionFailed(state, targetUpcId_);
        respawn = true;
        setStatus(TaskStatus::Unknown);
        target_ = nullptr;
        targetUpcId_ = kInvalidUpcId;
      } else if (status() == TaskStatus::Success) {
        markConstructionFinished(state, targetUpcId_);
      }

      unreserveLocation(state);
    } else {
      checkIfBuildingStarted(state);
    }
  }

  virtual void cancel(State* state) override {
    if (!reserveFailed_) {
      checkIfBuildingStarted(state);
    }

    ProxyTask::cancel(state);
    unreserveLocation(state);
  }
};

} // namespace

void RLBuildingPlacerModule::setTrainer(
    std::shared_ptr<cpid::Trainer> trainer) {
  trainer_ = trainer;
  model_ = std::dynamic_pointer_cast<BuildingPlacerModel>(trainer->model());
  if (model_ == nullptr) {
    throw std::runtime_error("Invalid model");
  }
}

void RLBuildingPlacerModule::setModel(
    std::shared_ptr<BuildingPlacerModel> model) {
  trainer_ = nullptr;
  model_ = model;
}

std::shared_ptr<BuildingPlacerModel> RLBuildingPlacerModule::model() const {
  return model_;
}

void RLBuildingPlacerModule::step(State* state) {
  auto board = state->board();

  // Game still active?
  if (trainer_ != nullptr) {
    if (!trainer_->isActive(handle_)) {
      throw std::runtime_error(
          fmt::format("{} no longer active", handle_.gameID()));
    }
    if (trainer_->isDone()) {
      throw std::runtime_error(
          fmt::format("{} stop requested", handle_.gameID()));
    }
  }

  // Cache BWEM base locations
  if (baseLocations_.empty()) {
    for (auto& area : state->areaInfo().areas()) {
      baseLocations_.insert(
          area.baseLocations.begin(), area.baseLocations.end());
    }
  }

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

  // Check ongoing constructions
  auto constructions = state->board()->get(
      kOngoingConstructionsKey, std::unordered_map<int, int>());
  for (auto& entry : constructions) {
    auto upcId = entry.first;
    auto* unit = state->unitsInfo().getUnit(entry.second);
    if (unit->completed()) {
      markConstructionFinished(state, upcId);
    } else if (unit->dead) {
      markConstructionFailed(state, upcId);
    }
  }

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
    std::shared_ptr<UpcPostData> postData;
    if (type->isBuilding && type->builder->isWorker) {
      std::tie(newUpc, postData) = upcWithPositionForBuilding(state, upc, type);
    }

    // Ignore the UPC if we can't determine a position
    if (newUpc == nullptr) {
      continue;
    }

    // Post new UPC along with a ProxyTask
    auto pos = newUpc->position.get<Position>();
    auto newUpcId =
        board->postUPC(std::move(newUpc), upcId, this, std::move(postData));
    if (newUpcId >= 0) {
      board->consumeUPC(upcId, this);
      auto task = std::make_shared<RLBuildingPlacerTask>(
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
          auto bptask = std::static_pointer_cast<RLBuildingPlacerTask>(task);
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
    auto bptask = std::static_pointer_cast<RLBuildingPlacerTask>(task);
    if (!bptask->respawn) {
      continue;
    }

    std::shared_ptr<UPCTuple> newUpc;
    std::shared_ptr<UpcPostData> postData;
    if (bptask->type->isBuilding && bptask->type->builder->isWorker) {
      std::tie(newUpc, postData) =
          upcWithPositionForBuilding(state, *bptask->sourceUpc, bptask->type);
    }

    if (newUpc == nullptr) {
      continue;
    }

    auto pos = newUpc->position.get<Position>();
    auto newUpcId = board->postUPC(
        std::move(newUpc), bptask->upcId(), this, std::move(postData));
    if (newUpcId >= 0) {
      bptask->respawn = false;
      bptask->setTarget(newUpcId);
      bptask->setPosition(pos);
      bptask->reserveLocation(state);
    }
  }
}

void RLBuildingPlacerModule::onGameStart(State* state) {
  if (model_ == nullptr) {
    LOG(WARNING)
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
  baseLocations_.clear();

  // If a trainer is set, start a new episode
  if (trainer_ != nullptr) {
    while (!(handle_ = trainer_->startEpisode())) {
      if (trainer_->isDone()) {
        // An exception is an easy way out in case we're signalled to stop
        throw std::runtime_error(
            fmt::format("{} trainer is done", handle_.gameID()));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    VLOG(0) << fmt::format(
        "{} started on {}", handle_.gameID(), state->mapName());
  }
}

void RLBuildingPlacerModule::onGameEnd(State* state) {
  if (trainer_ == nullptr) {
    return;
  }

  auto globalReward = state->won() ? 0.5f : -0.5f;
  if (state->board()->get<bool>("timeout", false)) {
    globalReward = 0.0f;
  }

  // Collect replay buffer frames for this game
  auto storage = state->board()->upcStorage();
  int numValid = 0;
  int numStarted = 0;
  int numFinished = 0;
  std::vector<std::shared_ptr<RLBPUpcData>> gameData;
  std::vector<std::shared_ptr<ReplayBufferFrame>> frames;
  float reward = globalReward;
  float nextReward = 0.0f;
  float totalReward = 0.0f;
  for (auto const* post : storage->upcPostsFrom(this)) {
    auto data = std::static_pointer_cast<RLBPUpcData>(post->data);
    // Ignore samples that ended up in cancelled tasks
    if (data == nullptr || !data->valid) {
      continue;
    }
    // Ignore samples that just consisted of a single valid action -- there's no
    // point in rewarding or punishing the model for that.
    if (data->sample.features.validLocations.sum().item<float>() <=
        1.0f + kfEpsilon) {
      continue;
    }

    numValid++;
    numStarted += (data->started ? 1 : 0);
    numFinished += (data->finished ? 1 : 0);

    // If this action resulted in building construction being started, it'll
    // receive the global game reward.
    nextReward = data->started ? globalReward : 0.0f;

    auto state = model_->makeInputBatch({data->sample}, at::kCPU);
    auto frame = trainer_->makeFrame(data->output, state, reward);
    trainer_->step(handle_, std::move(frame));

    totalReward += reward;
    reward = nextReward;
  }

  // Final end-of-game frame
  if (numValid > 0) {
    trainer_->step(
        handle_,
        trainer_->makeFrame(ag::VariantDict(), ag::VariantDict(), reward),
        true);
  }
  totalReward += reward;

  VLOG(0) << fmt::format(
      "{} collected {} samples: {} valid, {} started, {} finished",
      handle_.gameID(),
      storage->upcPostsFrom(this).size(),
      numValid,
      numStarted,
      numFinished);

  trainer_->metricsContext()->pushEvent("reward", totalReward);
}

std::tuple<std::shared_ptr<UPCTuple>, std::shared_ptr<UpcPostData>>
RLBuildingPlacerModule::upcWithPositionForBuilding(
    State* state,
    UPCTuple const& sourceUpc,
    BuildType const* type) {
  // First, get candidate area by simply running the rule-based version.
  std::shared_ptr<UPCTuple> seedUpc =
      builderhelpers::upcWithPositionForBuilding(state, sourceUpc, type);
  if (seedUpc == nullptr) {
    return std::make_tuple<>(nullptr, nullptr);
  }
  if (type->isRefinery) {
    // No need to run the model for refineries, really
    return std::make_tuple(seedUpc, nullptr);
  }
  if (type->isResourceDepot) {
    // For expansions, trust the position determined by the build order.
    // We check for expansions by just comparing the sharp position in the
    // original UPC to all possible base positions.
    Position upcPos;
    float prob;
    std::tie(upcPos, prob) = sourceUpc.positionArgMax();
    if (prob > 0.99f) {
      // Base locations are center-of-building, but positions in the UPC will
      // refer to the top left.
      Position basePos = upcPos + Position(8, 6);
      if (baseLocations_.find(basePos) != baseLocations_.end()) {
        VLOG(1) << "Assuming planned expansion at " << upcPos
                << ", not placing with model";
        return std::make_tuple(seedUpc, nullptr);
      }
    }
  }

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
  if (model_ == nullptr) {
    // We still construct a UpcData sample here so that we can keep track of the
    // rule-based version's performance more easily.
    return std::make_tuple(
        seedUpc,
        std::make_shared<RLBPUpcData>(
            type, std::move(sample), ag::tensor_list()));
  }

  auto batch = model_->makeInputBatch({sample});

  // The model outputs a probability distribution across every position and also
  // always operates on batches -- get rid of that dimensions by [0].
  // If we have a trainer, be sure to take the original model output rather than
  // the sampled action.
  ag::Variant output;
  torch::Tensor pOut;
  int action;
  if (trainer_) {
    output = trainer_->sample(trainer_->forward(batch, handle_));
    pOut = output["output"][0];
    action = output["action"].item<int32_t>();
  } else {
    torch::NoGradGuard guard;
    output = model_->forward(batch);
    pOut = output["output"][0];
    action = std::get<1>(pOut.max(0)).item<int32_t>();
  }
  VLOG(3) << fmt::format(
      "Output for {}: {}",
      utils::buildTypeString(type),
      common::tensorStats(pOut));

  // Translate to 2-dimensional action space (i.e. 2D walktile position)
  Position pos = sample.offsetToAction(action);

  // Re-use UPC from above for convenience and simply replace position
  auto upc = sampleUpc;
  upc->position = pos;
  sample.action = pos;
  VLOG(1) << fmt::format(
      "Selected position {} from seed position {} for {}",
      utils::positionString(pos),
      utils::positionString(seedPos),
      utils::buildTypeString(type));

  // Save GPU memory by moving outputs to the CPU
  output = common::applyTransform(
      output, [](torch::Tensor x) { return x.to(torch::kCPU); });
  return std::make_tuple(
      upc, std::make_shared<RLBPUpcData>(type, std::move(sample), output));
}

} // namespace cherrypi
