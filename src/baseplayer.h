/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "cherrypi.h"
#include "module.h"
#include "state.h"

namespace cherrypi {

/**
 * The main bot object.
 *
 * This class is used to play StarCraft Broodwar (TM) via the TorchCraft bridge.
 * The behavior and actions of the player are determined by a user-supplied list
 * of bot modules.
 */
class BasePlayer {
  using ClientCommands = std::vector<tc::Client::Command>;

 public:
  BasePlayer(std::shared_ptr<tc::Client> client, const StateConfig& = {});
  virtual ~BasePlayer();
  BasePlayer(const BasePlayer&) = delete;
  BasePlayer& operator=(const BasePlayer&) = delete;

  State* state() {
    return state_;
  }

  std::shared_ptr<Module> getTopModule() const;
  void addModule(std::shared_ptr<Module> module);
  void addModules(std::vector<std::shared_ptr<Module>> const& modules);

  template <typename T>
  std::shared_ptr<T> findModule() {
    for (auto& module : modules_) {
      auto m = std::dynamic_pointer_cast<T>(module);
      if (m != nullptr) {
        return m;
      }
    }
    return nullptr;
  };

  /// Add some commands to the queue, they will be executed on next step()
  void queueCmds(const std::vector<tc::Client::Command>& cmds);

  /// Log a warning if step() exceeds a maximum duration.
  /// Defaults to false.
  void setWarnIfSlow(bool warn);

  /// Delay step() to make the game run in approx. factor*fastest speed.
  void setRealtimeFactor(float factor);

  /// Set whether to perform consistency checks during the game.
  void setCheckConsistency(bool check);

  /// Set whether to gather timing statistics during the game.
  void setCollectTimers(bool collect);

  /// Set whether to log failed commands (via VLOG(0)).
  void setLogFailedCommands(bool log);

  /// Set whether to post drawing commands (if any are posted).
  /// Defaults to true.
  void setDraw(bool draw);

  virtual void stepModule(std::shared_ptr<Module> module);
  void stepModules();
  void step();
  size_t steps() const {
    return steps_;
  }

  virtual void init(){};

  void leave();

  void dumpTraceAlongReplay(
      std::string const& replayFile,
      std::string perspective = "");

 protected:
  using commandStartEndFrame =
      std::pair<tc::BW::UnitCommandType, std::pair<FrameNum, FrameNum>>;
  virtual void preStep();
  virtual void postStep();
  void logFailedCommands();

  std::shared_ptr<tc::Client> client_;
  int frameskip_ = 1;
  int combineFrames_ = 3;
  bool warnIfSlow_ = false;
  bool nonBlocking_ = false;
  bool checkConsistency_ = false;
  bool collectTimers_ = false;
  bool logFailedCommands_ = false;
  int lastFrameStepped_ = 0;
  int framesDropped_ = 0;
  float realtimeFactor_ = -1.0f;
  std::vector<std::shared_ptr<Module>> modules_;
  State* state_;
  std::shared_ptr<Module> top_;
  std::unordered_map<std::shared_ptr<Module>, Duration> moduleTimeSpent_;
  std::unordered_map<std::shared_ptr<Module>, Duration> moduleTimeSpentAgg_;
  Duration stateUpdateTimeSpent_;
  Duration stateUpdateTimeSpentAgg_;
  size_t steps_ = 0;
  bool initialized_ = false;
  bool firstStepDone_ = false;
  hires_clock::time_point lastStep_;
  bool draw_ = true;

  std::vector<tc::Client::Command> pendingCmds_;

  static const decltype(std::chrono::milliseconds(50)) kMaxStepDuration;
  static const decltype(std::chrono::seconds(9)) kMaxInitialStepDuration;
  static const decltype(std::chrono::milliseconds(42)) kStepDurationAtFastest;

  ClientCommands doStep();
};

} // namespace cherrypi
