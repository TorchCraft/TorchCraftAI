/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "scenarioprovider.h"

#include "microplayer.h"
#include "modules/once.h"

namespace cherrypi {

namespace {
// estimation of the number of played frames needed to propagate detection. This
// is 36 frames, assuming a frame skip of 3.
int constexpr kDetectionDelay = 12;

// We don't want to reuse the same bwapi instances too much, because the
// internal structures might overflow (dead units are not freed, for example)
int constexpr kMaxScenarioReuse = 100;
} // namespace

bool ScenarioProvider::isFinished(int currentStep, bool checkAttack) {
  if (player1_ == nullptr || player2_ == nullptr) {
    return true;
  }
  int units1 = player1_->state()->unitsInfo().myUnits().size();
  int units2 = player2_->state()->unitsInfo().myUnits().size();
  if (currentStep > maxFrame_ || (units1 == 0) || (units2 == 0)) {
    // trivial termination conditions
    return true;
  }

  // We consider the scenario to be finished when no pair of units can attack
  // each other. We need to remind the last step on which we could attack,
  // because detection takes a while to be propagated, hence we need to wait to
  // see if attacks are going to be possible again. If the last attack step is
  // uninitialized, or higher than the current step, we assume that we are at a
  // beginning of an episode and start counting from now.
  if (lastPossibleAttack_ < 0 || lastPossibleAttack_ > currentStep) {
    lastPossibleAttack_ = currentStep;
  }
  auto canAttack = [](const auto& allyUnits, const auto& enemyUnits) {
    for (const auto& u : allyUnits) {
      for (const auto& v : enemyUnits) {
        if (u->canAttack(v)) {
          return true;
        }
      }
    }
    return false;
  };

  bool canAttack1 = canAttack(
      player1_->state()->unitsInfo().myUnits(),
      player1_->state()->unitsInfo().enemyUnits());
  bool canAttack2 = canAttack(
      player2_->state()->unitsInfo().myUnits(),
      player2_->state()->unitsInfo().enemyUnits());

  bool possibleAttack = canAttack1 || canAttack2;
  // we might not be able to attack yet, for example in case the detection
  // status has not been updated yet. That's why we need to track the last time
  // we could attack to avoid premature ending.
  if (possibleAttack) {
    lastPossibleAttack_ = currentStep;
  }

  if (checkAttack && !possibleAttack) {
    return (currentStep - lastPossibleAttack_ > kDetectionDelay);
  }
  return false;
}

BaseMicroScenario::BaseMicroScenario(int maxFrame, bool gui)
    : ScenarioProvider(maxFrame, gui) {}

std::pair<std::shared_ptr<BasePlayer>, std::shared_ptr<BasePlayer>>
BaseMicroScenario::spawnNextScenario(
    const std::function<void(BasePlayer*)>& setup1,
    const std::function<void(BasePlayer*)>& setup2) {
  auto scenarioInfo = getScenarioInfo();

  launchedWithReplay_ = !replay_.empty();
  if (launchedWithReplay_ || !player1_) {
    // this is probably first run, we need to spawn the game
    // In micro, we don't care about races.
    scenario_ = std::make_shared<SelfPlayScenario>(
        mapPathPrefix_ + scenarioInfo.map,
        tc::BW::Race::Terran,
        tc::BW::Race::Terran,
        GameType::UseMapSettings,
        replay_,
        gui_);
    // Retain TorchCraft clients for efficient re-spawns.
    client1_ = scenario_->makeClient1();
    client2_ = scenario_->makeClient2();
    player1_ = std::make_shared<MicroPlayer>(client1_);
    player2_ = std::make_shared<MicroPlayer>(client2_);

    std::vector<tc::Client::Command> commands;
    commands.emplace_back(tc::BW::Command::SetSpeed, 0);
    commands.emplace_back(tc::BW::Command::SetGui, gui_);
    commands.emplace_back(tc::BW::Command::SetCombineFrames, 1);
    commands.emplace_back(tc::BW::Command::SetFrameskip, 1);
    commands.emplace_back(tc::BW::Command::SetBlocking, true);
    player1_->queueCmds(commands);
    player2_->queueCmds(commands);
  } else {
    // Reset player states by instantiating new ones
    player1_ = std::make_shared<MicroPlayer>(client1_);
    player2_ = std::make_shared<MicroPlayer>(client2_);
  }

  // setup the players
  setup1(player1_.get());
  setup2(player2_.get());

  // spawn units info
  auto queue = [&](const std::vector<torchcraft::Client::Command>& commands) {
    player1_->queueCmds(std::move(commands));
  };

  queue(OnceModule::makeSpawnCommands(
      scenarioInfo.allyList, player1_->state(), player1_->state()->playerId()));
  queue(OnceModule::makeSpawnCommands(
      scenarioInfo.enemyList,
      player2_->state(),
      player2_->state()->playerId()));
  for (int playerId = 0; playerId < int(scenarioInfo.players.size());
       ++playerId) {
    for (auto& tech : scenarioInfo.players[playerId].techs) {
      queue({torchcraft::Client::Command(
          torchcraft::BW::Command::CommandOpenbw,
          torchcraft::BW::OpenBWCommandType::SetPlayerResearched,
          playerId,
          tech,
          true)});
    }
    for (auto& upgrade : scenarioInfo.players[playerId].upgrades) {
      // Note that this can only can set an upgrade to Level 1
      queue({torchcraft::Client::Command(
          torchcraft::BW::Command::CommandOpenbw,
          torchcraft::BW::OpenBWCommandType::SetPlayerUpgradeLevel,
          playerId,
          upgrade,
          1)});
    }
  }
  for (auto& stepFunction : scenarioInfo.stepFunctions) {
    player1_->addModule(
        std::make_shared<LambdaModule>(std::move(stepFunction)));
  }

  // loop until all units are ready
  auto state1 = player1_->state();
  auto state2 = player2_->state();
  unsigned expectedAllies = 0u;
  unsigned expectedEnemies = 0u;
  for (auto& spawn : scenarioInfo.allyList) {
    expectedAllies += spawn.count;
  }
  for (auto& spawn : scenarioInfo.enemyList) {
    expectedEnemies += spawn.count;
  }

  while (state1->unitsInfo().myUnits().size() != expectedAllies ||
         state2->unitsInfo().myUnits().size() != expectedEnemies) {
    player1_->step();
    player2_->step();
  }

  // notify players of game start
  std::static_pointer_cast<MicroPlayer>(player1_)->onGameStart();
  std::static_pointer_cast<MicroPlayer>(player2_)->onGameStart();
  gameCount_++;
  return {player1_, player2_};
}

void BaseMicroScenario::sendKillCmds() {
  auto state1 = player1_->state();
  auto state2 = player2_->state();
  std::vector<tc::Client::Command> cmds;
  for (const auto& u : state1->unitsInfo().myUnits()) {
    cmds.push_back(tc::Client::Command(
        tc::BW::Command::CommandOpenbw,
        tc::BW::OpenBWCommandType::KillUnit,
        u->id));
  }
  for (const auto& u : state1->unitsInfo().neutralUnits()) {
    cmds.push_back(tc::Client::Command(
        tc::BW::Command::CommandOpenbw,
        tc::BW::OpenBWCommandType::KillUnit,
        u->id));
  }
  player1_->queueCmds(std::move(cmds));
  cmds.clear();
  for (const auto& u : state2->unitsInfo().myUnits()) {
    cmds.push_back(tc::Client::Command(
        tc::BW::Command::CommandOpenbw,
        tc::BW::OpenBWCommandType::KillUnit,
        u->id));
  }
  for (const auto& u : state2->unitsInfo().neutralUnits()) {
    cmds.push_back(tc::Client::Command(
        tc::BW::Command::CommandOpenbw,
        tc::BW::OpenBWCommandType::KillUnit,
        u->id));
  }
  player2_->queueCmds(std::move(cmds));
}

void BaseMicroScenario::cleanScenario() {
  if (!player1_ || !player2_) {
    return;
  }
  // notify players of game end
  std::static_pointer_cast<MicroPlayer>(player1_)->onGameEnd();
  std::static_pointer_cast<MicroPlayer>(player2_)->onGameEnd();

  auto state1 = player1_->state();
  auto state2 = player2_->state();
  if (launchedWithReplay_) {
    player1_->queueCmds({{tc::BW::Command::Quit}});
    player2_->queueCmds({{tc::BW::Command::Quit}});
    // Send commands, and wait for game to finish properly
    while (!state1->gameEnded()) {
      player1_->step();
      player2_->step();
    }
    player1_.reset();
    player2_.reset();
    launchedWithReplay_ = false;
    return;
  }

  // loop until units are actually dead
  int lastFrameKilled = 0;
  while (state1->unitsInfo().myUnits().size() != 0 ||
         state2->unitsInfo().myUnits().size() != 0) {
    VLOG(2) << "killing steps: state1 my="
            << state1->unitsInfo().myUnits().size() << " state1 enemy"
            << state1->unitsInfo().enemyUnits().size()
            << " state2 my=" << state2->unitsInfo().myUnits().size()
            << "state2 enemy=" << state1->unitsInfo().enemyUnits().size();
    player1_->step();
    player2_->step();
    if (lastFrameKilled != state1->currentFrame()) {
      sendKillCmds();
      lastFrameKilled = state1->currentFrame();
    }
  }

  VLOG(2) << "killing end: state1 my=" << state1->unitsInfo().myUnits().size()
          << " state1 enemy" << state1->unitsInfo().enemyUnits().size()
          << " state2 my=" << state2->unitsInfo().myUnits().size()
          << "state2 enemy=" << state1->unitsInfo().enemyUnits().size();

  if (gameCount_ > kMaxScenarioReuse) {
    gameCount_ = 0;
    player1_.reset();
    player2_.reset();
    scenario_.reset();
  }
}
} // namespace cherrypi
