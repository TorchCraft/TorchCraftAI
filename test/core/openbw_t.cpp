/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "blackboard.h"
#include "buildtype.h"
#include "cherrypi.h"
#include "modules.h"
#include "player.h"
#include "gameutils/selfplayscenario.h"

#include <torchcraft/constants.h>

using namespace cherrypi;

void upgrade(BasePlayer& bot, int pid, tc::BW::UpgradeType tech, int lvl) {
  bot.queueCmds(
      {tc::Client::Command(
          tc::BW::Command::CommandOpenbw,
          tc::BW::OpenBWCommandType::SetPlayerUpgradeLevel,
          pid,
          tech,
          lvl)});
}

void research(BasePlayer& bot, int pid, tc::BW::TechType tech, bool r = true) {
  bot.queueCmds(
      {tc::Client::Command(
          tc::BW::Command::CommandOpenbw,
          tc::BW::OpenBWCommandType::SetPlayerResearched,
          pid,
          tech,
          r)});
}

void setMinerals(BasePlayer& bot, int amount, int pid = -1) {
  if (pid < 0) {
    pid = bot.state()->playerId();
  }
  bot.queueCmds(
      {tc::Client::Command(
          tc::BW::Command::CommandOpenbw,
          tc::BW::OpenBWCommandType::SetPlayerMinerals,
          pid,
          amount)});
}

void setGas(BasePlayer& bot, int amount, int pid = -1) {
  if (pid < 0) {
    pid = bot.state()->playerId();
  }
  bot.queueCmds(
      {tc::Client::Command(
          tc::BW::Command::CommandOpenbw,
          tc::BW::OpenBWCommandType::SetPlayerGas,
          pid,
          amount)});
}

void setHealth(BasePlayer& bot, Unit* u, int amount) {
  bot.queueCmds(
      {tc::Client::Command(
          tc::BW::Command::CommandOpenbw,
          tc::BW::OpenBWCommandType::SetUnitHealth,
          u->id,
          amount)});
}

void setShield(BasePlayer& bot, Unit* u, int amount) {
  bot.queueCmds(
      {tc::Client::Command(
          tc::BW::Command::CommandOpenbw,
          tc::BW::OpenBWCommandType::SetUnitShield,
          u->id,
          amount)});
}

void setEnergy(BasePlayer& bot, Unit* u, int amount) {
  bot.queueCmds(
      {tc::Client::Command(
          tc::BW::Command::CommandOpenbw,
          tc::BW::OpenBWCommandType::SetUnitEnergy,
          u->id,
          amount)});
}

CASE("openbw/cheats/upgrade") {
  using namespace tc::BW;
  auto map = "test/maps/micro-big.scm";
  auto scenario = SelfPlayScenario(
      map, tc::BW::Race::Zerg, tc::BW::Race::Zerg, GameType::UseMapSettings);
  Player bot(scenario.makeClient1());
  Player enemy(scenario.makeClient2());

  bot.addModule(Module::make<TopModule>());
  bot.addModule(
      OnceModule::makeWithSpawns(
          {
              {UnitType::Protoss_Dragoon, 104, 132},
              {UnitType::Protoss_High_Templar, 104, 132},
          },
          "MySpawns"));
  bot.addModule(
      OnceModule::makeWithEnemySpawns(
          {{UnitType::Protoss_Dragoon, 90, 100}}, "TheirSpawns"));
  bot.addModule(Module::make<UPCToCommandModule>());

  enemy.addModule(Module::make<TopModule>());
  enemy.addModule(Module::make<UPCToCommandModule>());

  auto check = [&](auto func) {
    for (auto i = 0; i < 25; i++) {
      bot.step();
      enemy.step();
      if (func()) {
        break;
      }
    }
    EXPECT(func());
  };

  bot.init();
  enemy.init();
  auto state = bot.state();
  auto stateEnemy = enemy.state();
  while (state->unitsInfo().myUnits().size() == 0) {
    bot.step();
    enemy.step();
  }

  auto dragoon = state->unitsInfo().myUnits()[0];
  auto ht = state->unitsInfo().myUnits()[1];
  if (dragoon->type != buildtypes::Protoss_Dragoon) {
    std::swap(dragoon, ht);
  }
  EXPECT(dragoon->type == buildtypes::Protoss_Dragoon);
  EXPECT(ht->type == buildtypes::Protoss_High_Templar);

  auto enemyDragoon = stateEnemy->unitsInfo().myUnits()[0];
  EXPECT(enemyDragoon->type == buildtypes::Protoss_Dragoon);

  upgrade(bot, state->playerId(), tc::BW::UpgradeType::Singularity_Charge, 1);
  check([&]() { return dragoon->unit.groundRange == 24; });
  upgrade(bot, state->playerId(), tc::BW::UpgradeType::Singularity_Charge, 0);
  check([&]() { return dragoon->unit.groundRange == 16; });

  upgrade(
      bot, stateEnemy->playerId(), tc::BW::UpgradeType::Singularity_Charge, 1);
  check([&]() { return enemyDragoon->unit.groundRange == 24; });
  upgrade(
      bot, stateEnemy->playerId(), tc::BW::UpgradeType::Singularity_Charge, 0);
  check([&]() { return enemyDragoon->unit.groundRange == 16; });

  upgrade(
      bot, state->playerId(), tc::BW::UpgradeType::Protoss_Ground_Weapons, 2);
  check([&]() { return dragoon->unit.groundATK == 24; });
  upgrade(
      bot, state->playerId(), tc::BW::UpgradeType::Protoss_Ground_Weapons, 0);
  check([&]() { return dragoon->unit.groundATK == 20; });

  upgrade(
      bot,
      stateEnemy->playerId(),
      tc::BW::UpgradeType::Protoss_Ground_Weapons,
      2);
  check([&]() { return enemyDragoon->unit.groundATK == 24; });
  upgrade(
      bot,
      stateEnemy->playerId(),
      tc::BW::UpgradeType::Protoss_Ground_Weapons,
      0);
  check([&]() { return enemyDragoon->unit.groundATK == 20; });

  setMinerals(bot, 5000);
  check([&]() { return state->resources().ore == 5000; });
  setGas(bot, 1000);
  check([&]() { return state->resources().gas == 1000; });

  setHealth(bot, dragoon, 50);
  check([&]() { return dragoon->unit.health == 50; });
  // These things have regen, so in one frame it will tick up one...
  setShield(bot, dragoon, 20);
  check([&]() { return dragoon->unit.shield == 21; });
  setEnergy(bot, ht, 150);
  check([&]() { return ht->unit.energy == 151; });

  EXPECT(!state->hasResearched(buildtypes::Hallucination));
  research(bot, state->playerId(), tc::BW::TechType::Hallucination);
  check([&]() { return state->hasResearched(buildtypes::Hallucination); });
  bot.queueCmds(
      {tc::Client::Command(
          tc::BW::Command::CommandUnit,
          ht->id,
          tc::BW::UnitCommandType::Use_Tech_Unit,
          dragoon->id,
          0,
          0,
          tc::BW::TechType::Hallucination)});
  check([&]() { return state->unitsInfo().myUnits().size() == 4; });
}
