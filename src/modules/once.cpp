/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "modules/once.h"
#include "common/rand.h"
#include "utils.h"

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

int getSpawnCoordinate(State* state, int base, int max, float noiseMax) {
  auto noise = noiseMax < 1e-4
      ? 0.0
      : common::Rand::sample(std::normal_distribution<double>(0., noiseMax));
  return int(
      tc::BW::XYPixelsPerWalktile * utils::clamp(int(base + noise), 0, max));
}
std::vector<tc::Client::Command> OnceModule::makeSpawnCommands(
    const std::vector<SpawnPosition>& spawns,
    State* state,
    int playerId) {
  std::vector<tc::Client::Command> cmds;
  for (auto& spawn : spawns) {
    for (int i = 0; i < spawn.count; ++i) {
      cmds.emplace_back(
          tc::BW::Command::CommandOpenbw,
          tc::BW::OpenBWCommandType::SpawnUnit,
          playerId,
          spawn.type,
          getSpawnCoordinate(
              state, spawn.x, state->mapWidth() - 1, spawn.spreadX),
          getSpawnCoordinate(
              state, spawn.y, state->mapHeight() - 1, spawn.spreadY));
    }
  }
  return cmds;
}

LambdaModule::StepFunctionState OnceModule::makeSpawnFn(
    const std::vector<SpawnPosition>& spawns,
    bool enemy) {
  return [spawns{std::move(spawns)}, enemy](State* state) mutable {
    auto cmds = makeSpawnCommands(
        spawns, state, enemy ? 1 - state->playerId() : state->playerId());
    for (const auto& c : cmds) {
      state->board()->postCommand(c, kRootUpcId);
    }
  };
}

std::shared_ptr<Module> OnceModule::makeWithSpawns(
    const std::vector<SpawnPosition>& spawns,
    std::string name) {
  return Module::make<OnceModule>(
      makeSpawnFn(std::move(spawns), false), std::move(name));
}

std::shared_ptr<Module> OnceModule::makeWithEnemySpawns(
    const std::vector<SpawnPosition>& spawns,
    std::string name) {
  return Module::make<OnceModule>(
      makeSpawnFn(std::move(spawns), true), std::move(name));
}

} // namespace cherrypi
