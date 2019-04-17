/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "fivepool.h"
#include "gameutils/game.h"
#include "modules.h"
#include "player.h"
#include "test.h"
#include "utils.h"

DECLARE_double(rtfactor);

using namespace cherrypi;

namespace {

// used to test sending UPCs to scouting
class ScoutingUPCMakerModule : public Module {
 public:
  ScoutingUPCMakerModule() : Module() {}
  void step(State* state) override {
    state->board()->post(Blackboard::kMaxScoutWorkersKey, 0);
    state->board()->post(Blackboard::kMaxScoutExplorersKey, 0);
    if (sendScouts_ && !state->areaInfo().foundEnemyStartLocation()) {
      for (int i = 1; i <= 3; i++) {
        createUPC(state);
      }
      // send UPCs only once
      sendScouts_ = false;
    }
    if (sendExplorer_ && state->areaInfo().foundEnemyStartLocation()) {
      createUPC(state);
      sendExplorer_ = false;
    }
  }
  void setGoals(bool sendScouts, bool sendExplorer) {
    sendScouts_ = sendScouts;
    sendExplorer_ = sendExplorer;
  }

 protected:
  void createUPC(State* state) {
    auto upc = std::make_shared<UPCTuple>();
    for (auto unit : state->unitsInfo().myUnits()) {
      if (unit->type == buildtypes::Zerg_Drone) {
        upc->unit[unit] = .5;
      }
    }
    if (upc->unit.empty()) {
      LOG(WARNING) << "test: cannot make upc";
      return;
    }
    // dummy UPC, not to be confused with other UPCs sent by the module
    upc->command[Command::Scout] = 1;
    auto upcId = state->board()->postUPC(std::move(upc), -1, this);
    if (upcId < 0) {
      VLOG(2) << "[test] main scouting UPC not sent to the blackboard";
    }
  }
  bool sendScouts_ = false;
  bool sendExplorer_ = false;
};

class MockScoutingModule : public ScoutingModule {
 public:
  MockScoutingModule() : ScoutingModule() {}
  void step(State* state) override {
    ScoutingModule::step(state);
    if (!state->board()->tasksOfModule(this).empty()) {
      LOG_FIRST_N(INFO, 1) << "scout sent at frame" << state->currentFrame();
      state->board()->post("scout sent at frame", state->currentFrame());
    }
    int nbActiveTasks = 0;
    for (auto task : state->board()->tasksOfModule(this)) {
      if (!task->finished()) {
        nbActiveTasks++;
      }
    }
    state->board()->post("nb active scouting tasks", nbActiveTasks);
  }
};

class MockBuilderModule : public BuilderModule {
 public:
  MockBuilderModule() : BuilderModule() {}
  void step(State* state) override {
    BuilderModule::step(state);
    for (auto task : state->board()->tasksOfModule(this)) {
      task->cancel(state);
    }
  }
};

std::shared_ptr<Player> createMyPlayer(GameMultiPlayer* scenario) {
  std::shared_ptr<Player> bot =
      std::make_shared<Player>(scenario->makeClient1());
  bot->setFrameskip(3);

  bot->addModule(Module::make<CreateGatherAttackModule>());
  bot->addModule(Module::make<AutoBuildModule>());
  // bot->addModule(Module::make<FivePoolModule>());
  bot->addModule(Module::make<BuildingPlacerModule>());
  bot->addModule(Module::make<BuilderModule>());
  bot->addModule(Module::make<GathererModule>());
  bot->addModule(Module::make<CombatModule>());
  bot->addModule(Module::make<CombatMicroModule>());
  bot->addModule(Module::make<UPCToCommandModule>());

  bot->init();
  return bot;
}

std::shared_ptr<Player> createEnemyPlayer(GameMultiPlayer* scenario) {
  std::shared_ptr<Player> bot =
      std::make_shared<Player>(scenario->makeClient2());
  bot->setFrameskip(3);

  bot->addModule(Module::make<CreateGatherAttackModule>());
  bot->addModule(Module::make<StrategyModule>(
      StrategyModule::Duty::Scouting | StrategyModule::Duty::Harassment));
  bot->addModule(Module::make<GathererModule>());
  bot->addModule(Module::make<FivePoolModule>());
  bot->addModule(Module::make<MockScoutingModule>());
  bot->addModule(Module::make<HarassModule>());
  bot->addModule(Module::make<BuildingPlacerModule>());
  bot->addModule(Module::make<BuilderModule>());
  bot->addModule(Module::make<TacticsModule>());
  bot->addModule(Module::make<SquadCombatModule>());
  bot->addModule(Module::make<UPCToCommandModule>());
  bot->init();
  return bot;
}

std::shared_ptr<Player> createEnemyPlayerWithScoutingUPC(
    GameMultiPlayer* scenario,
    bool sendScouts,
    bool sendExplorer) {
  std::shared_ptr<Player> bot =
      std::make_shared<Player>(scenario->makeClient2());
  bot->setFrameskip(3);

  // bot->addModule(Module::make<CreateGatherAttackModule>());
  // bot->addModule(Module::make<GathererModule>());
  // bot->addModule(Module::make<FivePoolModule>());
  bot->addModule(Module::make<StrategyModule>(StrategyModule::Duty::Scouting));
  auto scoutingUPCMaker = Module::make<ScoutingUPCMakerModule>();
  scoutingUPCMaker->setGoals(sendScouts, sendExplorer);
  bot->addModule(scoutingUPCMaker);
  bot->addModule(Module::make<MockScoutingModule>());
  bot->addModule(Module::make<BuildingPlacerModule>());
  bot->addModule(Module::make<BuilderModule>());
  bot->addModule(Module::make<SquadCombatModule>());
  //  bot->addModule(Module::make<CombatMicroModule>());
  bot->addModule(Module::make<UPCToCommandModule>());
  bot->init();
  return bot;
}

int countDeadUnits(State* state) {
  auto& uinfo = state->unitsInfo();
  int cnt = 0;
  for (auto u : uinfo.allUnitsEver()) {
    cnt +=
        (u->playerId == state->playerId() &&
         std::find(uinfo.myUnits().begin(), uinfo.myUnits().end(), u) ==
             uinfo.myUnits().end());
  }
  return cnt;
}

} // namespace

// Disabling this because new worker defense intentionally avoids over-pulling
// workers
SCENARIO("scouting/detect/zerg_zerg[hide]") {
  auto scenario = GameMultiPlayer(
      "maps/(4)Fighting Spirit.scx", tc::BW::Race::Zerg, tc::BW::Race::Zerg);

  std::shared_ptr<Player> botNormal = createMyPlayer(&scenario);
  std::shared_ptr<Player> botIntruder = createEnemyPlayer(&scenario);

  auto stateNormal = botNormal->state();
  auto stateIntruder = botIntruder->state();
  int const kMaxFrames = 6000;
  do {
    botNormal->step();
    botIntruder->step();
    if (stateIntruder->board()->hasKey("scout sent at frame")) {
      botIntruder->setRealtimeFactor(FLAGS_rtfactor);
    }
    if ((stateNormal->currentFrame() > kMaxFrames) ||
        (stateIntruder->currentFrame() > kMaxFrames)) {
      break;
    }
  } while (
      !(stateNormal->gameEnded() || stateIntruder->gameEnded() ||
        countDeadUnits(stateIntruder) > 0));
  EXPECT(stateIntruder->board()->hasKey("scout sent at frame"));
  VLOG(0) << "scout sent at frame"
          << stateIntruder->board()->get<int>("scout sent at frame");
  EXPECT(stateIntruder->areaInfo().foundEnemyStartLocation());
  EXPECT(countDeadUnits(stateNormal) > 0);
  VLOG(0) << "dead defender " << countDeadUnits(stateNormal);
  VLOG(0) << "dead attacker " << countDeadUnits(stateIntruder);
  VLOG(0) << "Done after " << stateNormal->currentFrame() << " frames";
}

SCENARIO("scouting/detect/zerg_terran[hide]") {
  auto scenario = GameMultiPlayer(
      "maps/(4)Fighting Spirit.scx", tc::BW::Race::Terran, tc::BW::Race::Zerg);

  std::shared_ptr<Player> botNormal = createMyPlayer(&scenario);
  std::shared_ptr<Player> botIntruder = createEnemyPlayer(&scenario);

  auto stateNormal = botNormal->state();
  auto stateIntruder = botIntruder->state();
  int const kMaxFrames = 6000;
  do {
    botNormal->step();
    botIntruder->step();
    if ((stateNormal->currentFrame() > kMaxFrames) ||
        (stateIntruder->currentFrame() > kMaxFrames)) {
      break;
    }
  } while (
      !(stateNormal->gameEnded() || stateIntruder->gameEnded() ||
        countDeadUnits(stateIntruder) > 0));
  EXPECT(stateIntruder->board()->hasKey("scout sent at frame"));
  VLOG(0) << "scout sent at frame"
          << stateIntruder->board()->get<int>("scout sent at frame");
  EXPECT(stateIntruder->areaInfo().foundEnemyStartLocation());
  EXPECT(countDeadUnits(stateNormal) > 0);
  VLOG(0) << "dead defender " << countDeadUnits(stateNormal);
  VLOG(0) << "dead attacker " << countDeadUnits(stateIntruder);
  VLOG(0) << "Done after " << stateNormal->currentFrame() << " frames";
}

SCENARIO("scouting/detect/zerg_protoss[hide]") {
  auto scenario = GameMultiPlayer(
      "maps/(4)Fighting Spirit.scx", tc::BW::Race::Protoss, tc::BW::Race::Zerg);

  std::shared_ptr<Player> botNormal = createMyPlayer(&scenario);
  std::shared_ptr<Player> botIntruder = createEnemyPlayer(&scenario);

  auto stateNormal = botNormal->state();
  auto stateIntruder = botIntruder->state();
  int const kMaxFrames = 6000;
  do {
    botNormal->step();
    botIntruder->step();
    if ((stateNormal->currentFrame() > kMaxFrames) ||
        (stateIntruder->currentFrame() > kMaxFrames)) {
      break;
    }
  } while (
      !(stateNormal->gameEnded() || stateIntruder->gameEnded() ||
        countDeadUnits(stateIntruder) > 0));
  EXPECT(stateIntruder->board()->hasKey("scout sent at frame"));
  VLOG(0) << "scout sent at frame"
          << stateIntruder->board()->get<int>("scout sent at frame");
  EXPECT(stateIntruder->areaInfo().foundEnemyStartLocation());
  EXPECT(countDeadUnits(stateNormal) > 0);
  VLOG(0) << "dead defender " << countDeadUnits(stateNormal);
  VLOG(0) << "dead attacker " << countDeadUnits(stateIntruder);
  VLOG(0) << "Done after " << stateNormal->currentFrame() << " frames";
}

SCENARIO("scouting/detect/makeupc/noupc[hide]") {
  auto scenario = GameMultiPlayer(
      "maps/(4)Fighting Spirit.scx", tc::BW::Race::Protoss, tc::BW::Race::Zerg);

  std::shared_ptr<Player> botNormal = createMyPlayer(&scenario);
  std::shared_ptr<Player> botIntruder =
      createEnemyPlayerWithScoutingUPC(&scenario, false, false);

  auto stateNormal = botNormal->state();
  auto stateIntruder = botIntruder->state();
  int const kMaxFrames = 11000;
  do {
    botNormal->step();
    botIntruder->step();
    if ((stateNormal->currentFrame() > kMaxFrames) ||
        (stateIntruder->currentFrame() > kMaxFrames)) {
      break;
    }
    if (stateIntruder->areaInfo().foundEnemyStartLocation()) {
      bool cond =
          stateIntruder->board()->get<int>("nb active scouting tasks") == 0;
      if (!cond) {
        EXPECT(
            stateIntruder->board()->get<int>("nb active scouting tasks") == 0);
      }
    } else {
      bool cond =
          stateIntruder->board()->get<int>("nb active scouting tasks") == 1;
      if (!cond) {
        EXPECT(
            stateIntruder->board()->get<int>("nb active scouting tasks") == 1);
      }
    }
  } while (
      !(stateNormal->gameEnded() || stateIntruder->gameEnded() ||
        countDeadUnits(stateIntruder) > 0));
  EXPECT(stateIntruder->board()->hasKey("scout sent at frame"));
  VLOG(0) << "scout sent at frame"
          << stateIntruder->board()->get<int>("scout sent at frame");
  EXPECT(stateIntruder->areaInfo().foundEnemyStartLocation());
  EXPECT(stateIntruder->areaInfo().foundEnemyStartLocation());
  VLOG(0) << "dead defender " << countDeadUnits(stateNormal);
  VLOG(0) << "dead attacker " << countDeadUnits(stateIntruder);
  VLOG(0) << "Done after " << stateNormal->currentFrame() << " frames";
}

SCENARIO("scouting/detect/makeupc/noexplore[hide]") {
  auto scenario = GameMultiPlayer(
      "maps/(4)Fighting Spirit.scx", tc::BW::Race::Protoss, tc::BW::Race::Zerg);

  std::shared_ptr<Player> botNormal = createMyPlayer(&scenario);
  std::shared_ptr<Player> botIntruder =
      createEnemyPlayerWithScoutingUPC(&scenario, true, false);

  auto stateNormal = botNormal->state();
  auto stateIntruder = botIntruder->state();
  int const kMaxFrames = 6000;
  do {
    botNormal->step();
    botIntruder->step();
    if ((stateNormal->currentFrame() > kMaxFrames) ||
        (stateIntruder->currentFrame() > kMaxFrames)) {
      break;
    }
    if (stateIntruder->areaInfo().foundEnemyStartLocation()) {
      bool cond =
          stateIntruder->board()->get<int>("nb active scouting tasks") == 0;
      if (!cond) {
        EXPECT(
            stateIntruder->board()->get<int>("nb active scouting tasks") == 0);
      }
    } else {
      // 3 drones and 1 overlord
      bool cond =
          stateIntruder->board()->get<int>("nb active scouting tasks") == 4;
      if (!cond) {
        EXPECT(
            stateIntruder->board()->get<int>("nb active scouting tasks") == 4);
      }
    }
  } while (
      !(stateNormal->gameEnded() || stateIntruder->gameEnded() ||
        countDeadUnits(stateIntruder) > 0));
  EXPECT(stateIntruder->board()->hasKey("scout sent at frame"));
  VLOG(0) << "scout sent at frame"
          << stateIntruder->board()->get<int>("scout sent at frame");
  EXPECT(stateIntruder->areaInfo().foundEnemyStartLocation());
  EXPECT(stateIntruder->areaInfo().foundEnemyStartLocation());
  VLOG(0) << "dead defender " << countDeadUnits(stateNormal);
  VLOG(0) << "dead attacker " << countDeadUnits(stateIntruder);
  VLOG(0) << "Done after " << stateNormal->currentFrame() << " frames";
}

SCENARIO("scouting/detect/makeupc/explore[hide]") {
  auto scenario = GameMultiPlayer(
      "maps/(4)Fighting Spirit.scx", tc::BW::Race::Protoss, tc::BW::Race::Zerg);

  std::shared_ptr<Player> botNormal = createMyPlayer(&scenario);
  std::shared_ptr<Player> botIntruder =
      createEnemyPlayerWithScoutingUPC(&scenario, true, true);

  auto stateNormal = botNormal->state();
  auto stateIntruder = botIntruder->state();
  int const kMaxFrames = 6000;
  do {
    botNormal->step();
    botIntruder->step();
    if ((stateNormal->currentFrame() > kMaxFrames) ||
        (stateIntruder->currentFrame() > kMaxFrames)) {
      break;
    }
    if (stateIntruder->areaInfo().foundEnemyStartLocation()) {
      // 1 for the explorer
      bool cond =
          stateIntruder->board()->get<int>("nb active scouting tasks") == 1;
      if (!cond) {
        EXPECT(
            stateIntruder->board()->get<int>("nb active scouting tasks") == 1);
      }
      // at this stage the exploring task may succeed immediatly if the drone
      // was here already
      break;
    } else {
      // 3 drones and 1 overlord
      bool cond =
          stateIntruder->board()->get<int>("nb active scouting tasks") == 4;
      if (!cond) {
        EXPECT(
            stateIntruder->board()->get<int>("nb active scouting tasks") == 4);
      }
    }
  } while (
      !(stateNormal->gameEnded() || stateIntruder->gameEnded() ||
        countDeadUnits(stateIntruder) > 0));
  EXPECT(stateIntruder->board()->hasKey("scout sent at frame"));
  VLOG(0) << "scout sent at frame"
          << stateIntruder->board()->get<int>("scout sent at frame");
  EXPECT(stateIntruder->areaInfo().foundEnemyStartLocation());
  EXPECT(stateIntruder->areaInfo().foundEnemyStartLocation());
  VLOG(0) << "dead defender " << countDeadUnits(stateNormal);
  VLOG(0) << "dead attacker " << countDeadUnits(stateIntruder);
  VLOG(0) << "Done after " << stateNormal->currentFrame() << " frames";
}
