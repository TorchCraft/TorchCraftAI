/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "baseplayer.h"
#include <string_view>

#include "utils.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <thread>
#include <tuple>

#include <glog/logging.h>

#include "modules/cherryvisdumper.h"

namespace cherrypi {

const std::chrono::milliseconds BasePlayer::kMaxStepDuration =
    std::chrono::milliseconds(40);
const std::chrono::seconds BasePlayer::kMaxInitialStepDuration =
    std::chrono::seconds(9);
const std::chrono::milliseconds BasePlayer::kStepDurationAtFastest =
    std::chrono::milliseconds(42);

BasePlayer::BasePlayer(std::shared_ptr<tc::Client> client)
    : client_(std::move(client)), top_(nullptr) {
  // Make sure we start from an initialized client so that we have a valid
  // TorchCraft state from the start
  if (client_->state()->map_size[0] <= 0) {
    LOG(ERROR) << "TorchCraft state has not been initialized yet";
    throw std::runtime_error("Uninitialized TorchCraft state");
  }

  state_ = new State(client_);
  state_->setCollectTimers(collectTimers_);
  state_->board()->setCollectTimers(collectTimers_);
  lastFrameStepped_ = state_->currentFrame();
}

BasePlayer::~BasePlayer() {
  delete state_;
}

std::shared_ptr<Module> BasePlayer::getTopModule() const {
  return top_;
}

void BasePlayer::addModule(std::shared_ptr<Module> module) {
  if (module == nullptr) {
    throw std::runtime_error("Attempting to add null module to player");
  }

  if (std::find_if(
          modules_.begin(),
          modules_.end(),
          [module](std::shared_ptr<Module> target) {
            return module->name() == target->name();
          }) != modules_.end()) {
    LOG(ERROR) << "Module named " << module->name()
               << " already added. Hence skipping";
    return;
  }

  modules_.push_back(module);
  if (!top_) {
    top_ = module;
    VLOG(1) << "Added module '" << module->name() << "' as top module";
  } else {
    VLOG(1) << "Added module '" << module->name() << "'";
  }
  module->setPlayer(this);
}

void BasePlayer::addModules(
    std::vector<std::shared_ptr<Module>> const& modules) {
  for (auto& m : modules) {
    addModule(m);
  }
}

void BasePlayer::setWarnIfSlow(bool warn) {
  warnIfSlow_ = warn;
}

void BasePlayer::setRealtimeFactor(float factor) {
  realtimeFactor_ = factor;
}

void BasePlayer::setCheckConsistency(bool check) {
  checkConsistency_ = check;
}

void BasePlayer::setCollectTimers(bool collect) {
  collectTimers_ = collect;
  state_->setCollectTimers(collectTimers_);
  state_->board()->setCollectTimers(collectTimers_);
}

void BasePlayer::setLogFailedCommands(bool log) {
  logFailedCommands_ = log;
}

void BasePlayer::setDraw(bool draw) {
  draw_ = draw;
}

void BasePlayer::stepModules() {
  // Call step on all modules
  for (auto& module : modules_) {
    stepModule(module);
  }
  steps_++;
}

void BasePlayer::stepModule(std::shared_ptr<Module> module) {
  std::chrono::time_point<hires_clock> start;
  if (collectTimers_) {
    start = hires_clock::now();
  }
  module->step(state_);
  if (collectTimers_) {
    auto duration = hires_clock::now() - start;
    moduleTimeSpent_[module] = duration;
    moduleTimeSpentAgg_[module] += duration;
  }
}

void BasePlayer::step() {
  if (state_->gameEnded()) {
    // Return here if the game is over. Otherwise, client_->receive() will
    // just wait and time out eventually.
    VLOG(0) << "Game did end already";
    return;
  }

  std::vector<std::string> updates;
  if (!client_->receive(updates)) {
    throw std::runtime_error(
        std::string("Receive failure: ") + client_->error());
  }
  setLoggingFrame(client_->state()->frame_from_bwapi);

  auto start = hires_clock::now();
  auto commands = doStep();
  if (state_->gameEnded()) {
    return;
  }

  auto maxDurationForWarn =
      firstStepDone_ ? kMaxStepDuration : kMaxInitialStepDuration;
  if (!firstStepDone_) {
    // Subsequent frames are subject to a tighter time budget
    commands.emplace_back(
        tc::BW::Command::SetMaxFrameTimeMs,
        std::chrono::duration_cast<std::chrono::milliseconds>(kMaxStepDuration)
            .count());
    firstStepDone_ = true;
  }

  auto isDrawCommand = [](tc::Client::Command const& cmd) {
    return cmd.code >= tc::BW::Command::DrawLine &&
        cmd.code <= tc::BW::Command::DrawTextScreen;
  };
  // Dump draw cmds to bot trace dumper - if available
  if (auto traceModule = findModule<CherryVisDumperModule>()) {
    for (auto const& cmd : commands) {
      if (isDrawCommand(cmd)) {
        traceModule->onDrawCommand(state_, cmd);
      }
    }
  }
  if (!draw_) {
    commands.erase(
        std::remove_if(commands.begin(), commands.end(), isDrawCommand),
        commands.end());
  }

  if (!client_->send(commands)) {
    throw std::runtime_error(std::string("Send failure: ") + client_->error());
  }

  auto framesDroppedThisStep =
      std::max(0, state_->currentFrame() - lastFrameStepped_ - combineFrames_);
  lastFrameStepped_ = state_->currentFrame();
  if (framesDroppedThisStep > 0) {
    framesDropped_ += framesDroppedThisStep;
    auto frameDropPercentage = 100 * framesDropped_ / state_->currentFrame();
    LOG(WARNING) << "Dropped " << framesDroppedThisStep << " frames.";
    LOG(WARNING) << "Total frames dropped: " << framesDropped_ << " ("
                 << frameDropPercentage << "%)";
  }

  auto duration = hires_clock::now() - start;
  if (warnIfSlow_ && duration > maxDurationForWarn) {
    auto taskTimeStats = state_->board()->getTaskTimeStats();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
    LOG(WARNING) << "Maximum duration exceeded; step took " << ms.count()
                 << "ms";
    LOG(WARNING) << "Timings for this step:";
    ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        stateUpdateTimeSpent_);
    LOG(WARNING) << "  State::update(): " << ms.count() << "ms";
    auto stateUpdateTimes = state_->getStateUpdateTimes();
    for (auto stTime : stateUpdateTimes) {
      LOG(WARNING) << "    " << stTime.first << ": " << stTime.second.count()
                   << "ms";
    }
    for (auto& module : modules_) {
      ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          moduleTimeSpent_[module]);
      LOG(WARNING) << "  " << module->name() << ": " << ms.count() << "ms";
    }
    for (auto& stat : taskTimeStats) {
      LOG(WARNING) << "      Task: " << std::get<0>(stat) << " from "
                   << std::get<1>(stat) << ": " << std::get<2>(stat).count()
                   << "ms";
    }
  }

  if (realtimeFactor_ > 0) {
    auto target = combineFrames_ * kStepDurationAtFastest;
    auto timeSinceLastStep = hires_clock::now() - lastStep_;
    auto left = (target - timeSinceLastStep) / realtimeFactor_;
    if (left.count() > 0) {
      std::this_thread::sleep_for(left);
    }
  }

  size_t const logFreq = 100;
  if ((steps_ % logFreq == 0) && collectTimers_) {
    VLOG(1) << "Aggregate timings for previous " << logFreq << " steps:";
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        stateUpdateTimeSpentAgg_);
    VLOG(1) << "  State::update(): " << ms.count() << "ms";
    for (auto& module : modules_) {
      ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          moduleTimeSpentAgg_[module]);
      VLOG(1) << "  " << module->name() << ": " << ms.count() << "ms";
    }

    moduleTimeSpentAgg_.clear();
    stateUpdateTimeSpentAgg_ = Duration();
  }

  lastStep_ = hires_clock::now();
  unsetLoggingFrame();
}

void BasePlayer::leave() {
  VLOG(0) << "Leaving game";
  pendingCmds_.emplace_back(tc::BW::Command::Quit);
}

void BasePlayer::preStep() {
  std::chrono::time_point<hires_clock> start;
  if (collectTimers_) {
    start = hires_clock::now();
  }
  state_->update();
  if (collectTimers_) {
    auto duration = hires_clock::now() - start;
    stateUpdateTimeSpent_ = duration;
    stateUpdateTimeSpentAgg_ += duration;
  }
}

void BasePlayer::postStep() {
  if (checkConsistency_) {
    state_->board()->checkPostStep();
  }

  // Visualize our base so that we immediately know where we are on the map
  if (VLOG_IS_ON(1)) {
    if (state_->areaInfo().foundMyStartLocation()) {
      auto myBase = state_->areaInfo().myStartLocation();
      utils::drawCircle(state_, myBase, 50, tc::BW::Color::Blue);
      utils::drawCircle(state_, myBase, 52, tc::BW::Color::Blue);
    }
  }

  VLOG(2) << state_->board()->upcs().size() << " UPC tuples in blackboard";
}

/// Do the actual per-step work.
BasePlayer::ClientCommands BasePlayer::doStep() {
  setLoggingFrame(client_->state()->frame_from_bwapi);
  if (logFailedCommands_) {
    logFailedCommands();
  }

  preStep();
  if (state_->gameEnded()) {
    VLOG(1) << "Game has ended, not stepping through modules again";
    for (auto& module : modules_) {
      module->onGameEnd(state_);
    }
    return ClientCommands();
  }
  for (const auto& cmd : pendingCmds_) {
    state_->board()->postCommand(cmd, kRootUpcId);
  }
  pendingCmds_.clear();
  stepModules();
  postStep();

  return state_->board()->commands();
}

void BasePlayer::logFailedCommands() {
  auto lastCommands = client_->lastCommands();
  auto status = client_->lastCommandsStatus();
  for (size_t i = 0; i < status.size(); i++) {
    if (status[i] != 0) {
      auto& comm = lastCommands[i];
      int st = int(status[i]);
      if (st & 0x40) {
        // BWAPI error
        VLOG(0) << "Command failed: " << utils::commandString(state_, comm)
                << " ("
                << "code " << st << ", BWAPI code " << (st & ~0x40) << ")";
        // For unit commands with BWAPI "busy" error code, show some more
        // information to make debugging easier
        if (VLOG_IS_ON(1) && comm.code == +tc::BW::Command::CommandUnit &&
            (st & ~0x40) == 3) {
          std::ostringstream oss;
          auto unit = state_->unitsInfo().getUnit(comm.args[0]);
          for (auto order : unit->unit.orders) {
            oss << "(frame=" << order.first_frame << ", type=" << order.type
                << ", targetId=" << order.targetId
                << ", targetX=" << order.targetX
                << ", targetY=" << order.targetY << ") ";
          }
          VLOG(1) << "Current orders for " << utils::unitString(unit) << ": "
                  << oss.str();
          VLOG(1) << "Current flags for " << utils::unitString(unit) << ": "
                  << unit->unit.flags;
        }
      } else {
        VLOG(0) << "Command failed: " << utils::commandString(state_, comm)
                << " (" << st << ")";
      }
    }
  }
}

void BasePlayer::queueCmds(const std::vector<tc::Client::Command>& cmds) {
  pendingCmds_.insert(pendingCmds_.end(), cmds.begin(), cmds.end());
}

void BasePlayer::dumpTraceAlongReplay(std::string const& replayFile) {
  if (!findModule<CherryVisDumperModule>()) {
    addModule(Module::make("CherryVisDumper"));
  }
  findModule<CherryVisDumperModule>()->setReplayFile(replayFile);
  state_->board()->setTraceDumper(findModule<CherryVisDumperModule>());
}
} // namespace cherrypi
