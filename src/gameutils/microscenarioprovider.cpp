/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "microscenarioprovider.h"

#include "buildtype.h"
#include "microplayer.h"
#include "modules/once.h"

namespace cherrypi {

namespace {

// We don't want to reuse the same BWAPI instances too much, because the
// internal structures might overflow (dead units are not freed, for example)
//
// The BWAPI ID limit is 10,000 -- this smaller number gives us some buffer
// against eg. Scarabs, other units that get produced during the game
int constexpr kMaxUnits = 9000;

} // namespace

// It's possible to run this from not the rootdir of the repository,
// in which case you can set the mapPathPrefix to where the maps should be
// found. This is just the path to your cherrypi directory
void MicroScenarioProvider::setMapPathPrefix(const std::string& prefix) {
  mapPathPrefix_ = prefix;
}

std::unique_ptr<Reward> MicroScenarioProvider::getReward() const {
  return scenarioNow_.reward();
}

void MicroScenarioProvider::endScenario() {
  VLOG(3) << "endScenario()";

  if (!player1_ || !player2_) {
    return;
  }

  microPlayer1().onGameEnd();
  microPlayer2().onGameEnd();

  if (launchedWithReplay()) {
    player1_->queueCmds({{tc::BW::Command::Quit}});
    player2_->queueCmds({{tc::BW::Command::Quit}});
    // Send commands, and wait for game to finish properly
    while (!player1_->state()->gameEnded()) {
      player1_->step();
      player2_->step();
    }
  }

  player1_.reset();
  player2_.reset();
}

void MicroScenarioProvider::endGame() {
  VLOG(3) << "endGame()";

  ++resetCount_;
  endScenario();
  unitsThisGame_ = 0;
  game_.reset();
}

void MicroScenarioProvider::killAllUnits() {
  VLOG(3) << "killAllUnits()";

  if (!player1_ || !player2_) {
    return;
  }

  auto killPlayerUnits = [&](auto& player) {
    std::vector<tc::Client::Command> killCommands;
    auto killUnits = [&](auto& units) {
      for (const auto& unit : units) {
        killCommands.emplace_back(
            tc::BW::Command::CommandOpenbw,
            tc::BW::OpenBWCommandType::KillUnit,
            unit->id);
      }
    };
    auto& unitsInfo = player->state()->unitsInfo();
    killUnits(unitsInfo.myUnits());
    killUnits(unitsInfo.neutralUnits());
    player->queueCmds(std::move(killCommands));
  };

  int lastFrameKilled = 0;
  auto countPlayerUnits = [&](auto& player) {
    return player->state()->unitsInfo().myUnits().size();
  };
  while (countPlayerUnits(player1_) > 0 || countPlayerUnits(player2_) > 0) {
    player1_->step();
    player2_->step();
    auto* state1 = player1_->state();
    if (lastFrameKilled != state1->currentFrame()) {
      killPlayerUnits(player1_);
      killPlayerUnits(player2_);
      lastFrameKilled = state1->currentFrame();
    }
  }
}

void MicroScenarioProvider::createNewPlayers() {
  VLOG(3) << "createNewPlayers()";

  endScenario();
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
}

void MicroScenarioProvider::createNewGame() {
  VLOG(3) << "createNewGame()";

  endGame();
  // Any race is fine for scenarios.
  game_ = std::make_shared<GameMultiPlayer>(
      mapPathPrefix_ + scenarioNow_.map,
      tc::BW::Race::Terran,
      tc::BW::Race::Terran,
      GameType::UseMapSettings,
      replay_,
      gui_);
  client1_ = game_->makeClient1();
  client2_ = game_->makeClient2();
}

void MicroScenarioProvider::setupScenario() {
  VLOG(3) << "setupScenario() #" << scenarioCount_;
  ++scenarioCount_;

  bool queuedCommands = false;
  auto queueCommands =
      [&](const std::vector<torchcraft::Client::Command>& commands) {
        player1_->queueCmds(std::move(commands));
        queuedCommands = queuedCommands || !commands.empty();
      };
  auto sendCommands = [&]() {
    if (queuedCommands) {
      VLOG(5) << "Sending commands";
      player1_->step();
      player2_->step();
      queuedCommands = false;
    }
  };

  auto getPlayerId = [&](int playerIndex) {
    return (playerIndex == 0 ? player1_ : player2_)->state()->playerId();
  };
  // Add techs and upgrades first.
  for (int playerIndex = 0; playerIndex < int(scenarioNow_.players.size());
       ++playerIndex) {
    VLOG(4) << "Adding techs for Player " << playerIndex;
    for (auto& tech : scenarioNow_.players[playerIndex].techs) {
      VLOG(4) << "Adding tech for player " << playerIndex << ": " << tech;
      queueCommands({torchcraft::Client::Command(
          torchcraft::BW::Command::CommandOpenbw,
          torchcraft::BW::OpenBWCommandType::SetPlayerResearched,
          getPlayerId(playerIndex),
          tech,
          true)});
    }
    VLOG(4) << "Adding upgrades for Player " << playerIndex;
    for (auto& upgrade : scenarioNow_.players[playerIndex].upgrades) {
      VLOG(4) << "Adding upgrade for player " << playerIndex << ": "
              << upgrade.upgradeType << " #" << upgrade.level;
      // Note that this can only can set an upgrade to Level 1
      queueCommands({torchcraft::Client::Command(
          torchcraft::BW::Command::CommandOpenbw,
          torchcraft::BW::OpenBWCommandType::SetPlayerUpgradeLevel,
          getPlayerId(playerIndex),
          upgrade.upgradeType,
          upgrade.level)});
    }
  }
  sendCommands();

  // Next, we spawn units.
  //
  // Spawning units is tricky. There are a few considerations:
  // * There's a maximum (About 128) commands that can be processed each frame.
  // * We don't want one player's units to be around for too long before the
  //   other player's (to minimize the extra time to react/attack).
  // * Building placement can be blocked by other units, so they need to
  //   spawn first.
  // * If an add-on is spawned without its building, it will enter as neutral,
  //   which causes issues (IIRC UnitsInfo can't handle the change of owner).
  //
  // So, here are the distinct tiers in which we'll spawn units:
  // * Non-combat buildings (because units can block buildings)
  //   * Player 0 non-addon buildings (because addons spawn neutral otherwise)
  //   * Player 1 non-addon buildings
  //   * Addon buildings
  // * Combat buildings (Last, to minimize frames they could spend attacking)
  // * Player 0 non-workers
  // * Player 1 non-workers
  // * Player 0 workers
  // * Player 1 workers

  auto queueUnits = [&](const std::vector<SpawnPosition>& units,
                        std::shared_ptr<BasePlayer> player) {
    unitsThisGame_ += units.size();
    unitsTotal_ += units.size();
    queueCommands(OnceModule::makeSpawnCommands(
        units, player->state(), player->state()->playerId()));
  };
  auto extractUnits = [](std::vector<SpawnPosition>& units,
                         std::function<bool(const SpawnPosition&)> predicate) {
    std::vector<SpawnPosition> output;
    for (unsigned i = 0; i < units.size(); ++i) {
      if (predicate(units[i])) {
        output.push_back(units[i]);
        std::swap(units[i], units[units.size() - 1]);
        units.pop_back();
      }
    }
    return output;
  };
  // Predicates for spawning units in tiers
  auto producesCreep = [](const SpawnPosition& unit) {
    auto* type = getUnitBuildType(unit.type);
    return type->producesCreep;
  };
  auto isNonCombatNonAddonBuilding = [](const SpawnPosition& unit) {
    auto* type = getUnitBuildType(unit.type);
    return type->isBuilding && !type->isAddon && !type->hasAirWeapon &&
        !type->hasGroundWeapon && type != buildtypes::Terran_Bunker &&
        type != buildtypes::Protoss_Shield_Battery;
  };
  auto isAddon = [](const SpawnPosition& unit) {
    auto* type = getUnitBuildType(unit.type);
    return type->isAddon;
  };
  auto isCombatBuilding = [](const SpawnPosition& unit) {
    auto* type = getUnitBuildType(unit.type);
    return type->isBuilding && !type->isAddon;
  };
  auto isNonWorker = [](const SpawnPosition& unit) {
    auto* type = getUnitBuildType(unit.type);
    return type != buildtypes::Terran_SCV &&
        type != buildtypes::Protoss_Probe && type != buildtypes::Zerg_Drone;
  };
  auto isAnything = [](const SpawnPosition& unit) { return true; };

  std::vector<SpawnPosition> units0(scenarioNow_.players[0].units);
  std::vector<SpawnPosition> units1(scenarioNow_.players[1].units);
  std::vector<std::tuple<int, std::vector<SpawnPosition>>> tiers;

  // Semi-hack: OpenBW chokes when destroying a bunch of creep-producing
  // buildings at the same time. Let's not spawn them until we can fix that.
  extractUnits(units0, producesCreep);
  extractUnits(units1, producesCreep);
  // Add-ons still aren't getting assigned to buildings properly.
  extractUnits(units0, isAddon);
  extractUnits(units1, isAddon);
  tiers.emplace_back(
      std::make_tuple(0, extractUnits(units0, isNonCombatNonAddonBuilding)));
  tiers.emplace_back(
      std::make_tuple(1, extractUnits(units1, isNonCombatNonAddonBuilding)));
  tiers.emplace_back(std::make_tuple(0, extractUnits(units0, isAddon)));
  tiers.emplace_back(std::make_tuple(1, extractUnits(units1, isAddon)));
  tiers.emplace_back(
      std::make_tuple(0, extractUnits(units0, isCombatBuilding)));
  tiers.emplace_back(
      std::make_tuple(1, extractUnits(units1, isCombatBuilding)));
  tiers.emplace_back(std::make_tuple(0, extractUnits(units0, isNonWorker)));
  tiers.emplace_back(std::make_tuple(1, extractUnits(units1, isNonWorker)));
  tiers.emplace_back(std::make_tuple(0, extractUnits(units0, isAnything)));
  tiers.emplace_back(std::make_tuple(1, extractUnits(units1, isAnything)));

  for (auto i = 0u; i < tiers.size(); ++i) {
    auto& tier = tiers[i];
    int playerIndex = std::get<0>(tier);
    auto& units = std::get<1>(tier);
    VLOG(4) << "Spawning " << units.size() << " units for player "
            << playerIndex << " in Tier " << i;
    queueUnits(units, playerIndex == 0 ? player1_ : player2_);
    sendCommands();
  }

  // Lastly, add any scenario-specific functions
  for (auto& stepFunction : scenarioNow_.stepFunctions) {
    VLOG(4) << "Running a step function";
    player1_->addModule(
        std::make_shared<LambdaModule>(std::move(stepFunction)));
  }

  // In practice, it seems to take 4 additional steps for all units to show up
  // and be visible to players
  constexpr int kStepsForUnitsToShowUp = 4;
  size_t unitsSeenThisEpisode = 0;
  for (int i = 0; i < kStepsForUnitsToShowUp; ++i) {
    auto units = player1_->state()->unitsInfo().allUnitsEver().size();
    unitsSeenThisEpisode = std::max(unitsSeenThisEpisode, units);
    VLOG(4) << "Total units this step: " << units;
    player1_->step();
    player2_->step();
  }
  unitsSeenTotal_ += unitsSeenThisEpisode;
  VLOG(4) << "Total units seen all time: " << unitsSeenTotal_;
}
std::pair<std::shared_ptr<BasePlayer>, std::shared_ptr<BasePlayer>>
MicroScenarioProvider::startNewScenario(
    const std::function<void(BasePlayer*)>& setup1,
    const std::function<void(BasePlayer*)>& setup2) {
  VLOG(3) << "startNewScenario()";

  VLOG(3) << "Total units spawned: " << unitsThisGame_ << "/" << unitsTotal_;
  if (unitsThisGame_ > kMaxUnits) {
    endGame();
  } else {
    killAllUnits();
  }
  endScenario();

  auto lastMap = scenarioNow_.map;
  scenarioNow_ = getFixedScenario();

  bool needNewGame = launchedWithReplay() || !player1_ || !player2_ ||
      lastMap != scenarioNow_.map;
  if (needNewGame) {
    createNewGame();
  }
  createNewPlayers();
  setup1(player1_.get());
  setup2(player2_.get());
  setupScenario();
  microPlayer1().onGameStart();
  microPlayer2().onGameStart();
  return {player1_, player2_};
}

} // namespace cherrypi