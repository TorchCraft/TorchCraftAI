/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "builder.h"
#include "builderc.h"
#include "builderhelper.h"

#include "commandtrackers.h"
#include "movefilters.h"
#include "state.h"
#include "utils.h"

#include <cstdint>
#include <deque>
#include <glog/logging.h>

namespace cherrypi {

REGISTER_SUBCLASS_0(Module, BuilderModule);

void BuilderModule::step(State* state) {
  auto board = state->board();
  int frame = state->currentFrame();

  if (bcdata_ == nullptr) {
    bcdata_ = std::make_shared<BuilderControllerData>();
  }

  // Update shared data
  if (frame - bcdata_->lastIncomeHistoryUpdate >= 8) {
    int framesSinceLastUpdate = frame - bcdata_->lastIncomeHistoryUpdate;
    bcdata_->lastIncomeHistoryUpdate = frame;

    static const int resourcePerFrameAverageSize = 15 * 20;
    auto calculateAverage = [&](double& dst, auto& hist, int current) {
      for (int i = 0; i != framesSinceLastUpdate; ++i) {
        if (hist.size() >= resourcePerFrameAverageSize) {
          hist.pop_front();
        }
        hist.push_back(current);
      }
      int totalDifference = 0;
      for (size_t i = 1; i < hist.size(); ++i) {
        totalDifference += std::max(hist[i] - hist[i - 1], 0);
      }
      dst = (double)totalDifference / hist.size();
    };
    auto res = state->resources();
    calculateAverage(
        bcdata_->currentMineralsPerFrame, bcdata_->mineralsHistory, res.ore);
    calculateAverage(bcdata_->currentGasPerFrame, bcdata_->gasHistory, res.gas);
  }

  for (auto i = bcdata_->recentAssignedBuilders.begin();
       i != bcdata_->recentAssignedBuilders.end();) {
    if (frame - std::get<0>(i->second) >= 15 * 25) {
      i = bcdata_->recentAssignedBuilders.erase(i);
    } else {
      ++i;
    }
  }

  bcdata_->res = state->resources();

  // Check for new UPCs
  for (auto const& upct : board->upcsWithSharpCommand(Command::Create)) {
    auto upcId = upct.first;
    auto& upc = *upct.second;

    // Do we know what we want and can we build it?
    BuildType const* type;
    auto createType = upc.createTypeArgMax();
    if (createType.first == nullptr || createType.second < 0.99f) {
      VLOG(4) << "Not sure what we want? argmax(createType)="
              << utils::buildTypeString(createType.first)
              << " with p=" << createType.second << ", skipping "
              << utils::upcString(upcId);
      continue;
    } else if (createType.first->builder == nullptr) {
      VLOG(4) << "Don't know how to build " << createType.first->name
              << ", skipping " << utils::upcString(upcId);
      continue;
    }
    type = createType.first;

    // If we require a building position, do we have a dirac one?
    Position pos;
    if (type->isBuilding && type->builder->isWorker) {
      float prob;
      std::tie(pos, prob) = upc.positionArgMax();
      if (prob < 0.99f || pos.x < 0 || pos.y < 0) {
        VLOG(4) << "Not sure where we want " << type->name
                << "? argmax(position)=" << prob << " with p=" << pos
                << ", skipping " << utils::upcString(upcId);
        continue;
      }
    }

    if (type->isBuilding && type->builder->isWorker) {
      auto controller = std::make_shared<WorkerBuilderController>(
          this, type, upc.unit, bcdata_, pos);
      auto task = std::make_shared<ControllerTask>(
          upcId, std::unordered_set<Unit*>(), state, controller);
      controller->setPriority((float)upcId);
      board->consumeUPC(upcId, this);
      board->postTask(task, this, true);
    } else {
      auto controller =
          std::make_shared<BuilderController>(this, type, upc.unit, bcdata_);
      auto task = std::make_shared<ControllerTask>(
          upcId, std::unordered_set<Unit*>(), state, controller);
      controller->setPriority((float)upcId);
      board->consumeUPC(upcId, this);
      board->postTask(task, this, true);
    }
    VLOG(1) << "New task for " << utils::upcString(upcId) << " for "
            << type->name;
  }

  // Update the priority of any build tasks according to SetCreatePriority UPCs
  for (auto const& upct :
       board->upcsWithSharpCommand(Command::SetCreatePriority)) {
    auto upcId = upct.first;
    auto& upc = *upct.second;
    if (upc.state.is<UPCTuple::SetCreatePriorityState>()) {
      auto st = upc.state.get_unchecked<UPCTuple::SetCreatePriorityState>();
      for (auto& task : board->tasksOfModule(this)) {
        if (task->upcId() == std::get<0>(st)) {
          auto ctask = std::static_pointer_cast<ControllerTask>(task);
          auto controller = std::dynamic_pointer_cast<BuilderControllerBase>(
              ctask->controller());
          float prev = controller->priority();
          controller->setPriority(std::get<1>(st));
          VLOG(2) << "Priority of " << task->upcId() << " "
                  << controller->type()->name << " changed from " << prev
                  << " to " << controller->priority();
          break;
        }
      }
    }
    board->consumeUPC(upcId, this);
  }

  // Update all controllers. There are potentially multiple tasks per controller
  // (due to delayed unit allocations), so make sure we update every controller
  // only once.
  auto tasks = board->tasksOfModule(this);
  std::sort(tasks.begin(), tasks.end(), [](auto& a, auto& b) {
    auto actask = std::static_pointer_cast<ControllerTask>(a);
    auto acontroller =
        std::static_pointer_cast<BuilderControllerBase>(actask->controller());
    auto bctask = std::static_pointer_cast<ControllerTask>(b);
    auto bcontroller =
        std::static_pointer_cast<BuilderControllerBase>(bctask->controller());
    return acontroller->priority() < bcontroller->priority();
  });
  std::unordered_set<Controller*> stepped;
  for (auto& v : tasks) {
    auto ctask = std::static_pointer_cast<ControllerTask>(v);
    auto controller =
        std::dynamic_pointer_cast<BuilderControllerBase>(ctask->controller());
    if (controller != nullptr) {
      if (stepped.find(controller.get()) == stepped.end()) {
        controller->step(state);
        stepped.insert(controller.get());
      }
    } else {
      LOG(WARNING) << "Invalid controller for builder task " << ctask->upcId();
    }

    if (ctask->finished()) {
      for (auto& v2 : tasks) {
        if (v != v2) {
          auto ctask2 = std::static_pointer_cast<ControllerTask>(v2);
          if (!ctask2->finished()) {
            auto controller2 = std::dynamic_pointer_cast<BuilderControllerBase>(
                ctask2->controller());
            if (controller2 == controller) {
              ctask2->cancel(state);
            }
          }
        }
      }
    }
  }
}

} // namespace cherrypi
