/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "modules/once.h"
#include "common/rand.h"

namespace cherrypi {

OnceModule::OnceModule(StepFunctionState fn, std::string name)
    : LambdaModule(std::move(fn), std::move(name)) {
  key_ = name + "_flag_" + std::to_string(reinterpret_cast<uintptr_t>(this));
}

OnceModule::OnceModule(StepFunctionStateModule fn, std::string name)
    : LambdaModule(std::move(fn), std::move(name)) {
  key_ = name + "_flag_" + std::to_string(reinterpret_cast<uintptr_t>(this));
}

void OnceModule::step(State* state) {
  auto board = state->board();
  if (!board->get<bool>(key_, false)) {
    fn_.match(
        [&](StepFunctionState fn) { fn(state); },
        [&](StepFunctionStateModule fn) { fn(state, this); });
    board->post(key_, true);
  }
}

std::shared_ptr<Module> OnceModule::makeWithCommands(
    std::vector<tc::Client::Command> commands,
    std::string name) {
  return Module::make<OnceModule>(
      [commands{std::move(commands)}](State * state) {
        for (auto& comm : commands) {
          state->board()->postCommand(comm, kRootUpcId);
        }
      },
      std::move(name));
}

std::vector<tc::Client::Command> OnceModule::makeSpawnCommands(
    std::vector<OnceModule::SpawnInfo> spawns,
    int playerId) {
  std::vector<tc::Client::Command> cmds;
  for (auto& si : spawns) {
    std::normal_distribution<double> distribX(
        0., std::max(si.randomPositionSpreadX, 1e-4));
    std::normal_distribution<double> distribY(
        0., std::max(si.randomPositionSpreadY, 1e-4));
    double noiseX =
        (si.randomPositionSpreadX < 1e-4) ? 0 : common::Rand::sample(distribX);
    double noiseY =
        (si.randomPositionSpreadY < 1e-4) ? 0 : common::Rand::sample(distribY);
    cmds.emplace_back(
        tc::BW::Command::CommandOpenbw,
        tc::BW::OpenBWCommandType::SpawnUnit,
        playerId,
        si.type,
        int((si.x + noiseX) * tc::BW::XYPixelsPerWalktile),
        int((si.y + noiseY) * tc::BW::XYPixelsPerWalktile));
  }
  return cmds;
}

LambdaModule::StepFunctionState OnceModule::makeSpawnFn(
    std::vector<OnceModule::SpawnInfo> spawns,
    bool enemy) {
  return [ spawns{std::move(spawns)}, enemy ](State * state) mutable {
    auto cmds = makeSpawnCommands(
        spawns, enemy ? 1 - state->playerId() : state->playerId());
    for (const auto& c : cmds) {
      state->board()->postCommand(c, kRootUpcId);
    }
  };
}

std::shared_ptr<Module> OnceModule::makeWithSpawns(
    std::vector<SpawnInfo> spawns,
    std::string name) {
  return Module::make<OnceModule>(
      makeSpawnFn(std::move(spawns), false), std::move(name));
}

std::shared_ptr<Module> OnceModule::makeWithEnemySpawns(
    std::vector<SpawnInfo> spawns,
    std::string name) {
  return Module::make<OnceModule>(
      makeSpawnFn(std::move(spawns), true), std::move(name));
}

} // namespace cherrypi
