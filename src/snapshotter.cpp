/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "snapshotter.h"
#include <chrono>

namespace cherrypi {

FixedScenario snapshotToScenario(const Snapshot& snapshot) {
  FixedScenario output;

  // We use the empty (with revealers) 128x128 map instead of the empty 64x64
  // map because it more closely matches the dimensions of professional maps
  output.map = "test/maps/micro-empty-128.scm";

  int playerIndex = 0;
  for (auto& snapshotPlayer : snapshot.players) {
    auto& scenarioPlayer = output.players[playerIndex];
    for (auto& snapshotUnit : snapshotPlayer.units) {
      scenarioPlayer.units.emplace_back(SpawnPosition{
          1,
          tc::BW::UnitType::_from_integral_unchecked(snapshotUnit.type),
          snapshotUnit.x,
          snapshotUnit.y,
          0.0f,
          0.0f,
          snapshotUnit.health,
          snapshotUnit.shields,
          snapshotUnit.energy});
    }
    for (auto* upgradeType : buildtypes::allUpgradeTypes) {
      int upgradeId = upgradeType->upgrade;
      int upgradeLevel = snapshotPlayer.getUpgradeLevel(upgradeId);
      if (upgradeLevel > 0) {
        scenarioPlayer.upgrades.emplace_back(ScenarioUpgradeLevel{
            tc::BW::UpgradeType::_from_integral_unchecked(upgradeId),
            upgradeLevel});
      }
    }
    for (auto* techType : buildtypes::allTechTypes) {
      int techId = techType->tech;
      if (snapshotPlayer.hasTech(techId)) {
        scenarioPlayer.techs.emplace_back(
            tc::BW::TechType::_from_integral_unchecked(techId));
      }
    }
    ++playerIndex;
  }

  // A hack: If exactly one player has a Defiler,
  // make sure its owner is player #0.
  // This hack enables us to train on the 100k snapshots we dumped for Defiler
  // spellcasting -- which should have ensured correct player order but didn't
  // -- without re-dumping them.
  auto defilerTypeId = tc::BW::UnitType::_from_integral_unchecked(
      buildtypes::Zerg_Defiler->unit);
  auto hasDefiler = [&](const FixedScenarioPlayer& player) {
    return player.units.end() !=
        std::find_if(
               player.units.begin(),
               player.units.end(),
               [&](const SpawnPosition& unit) {
                 return unit.type == defilerTypeId;
               });
  };
  if (hasDefiler(output.players[1]) && !hasDefiler(output.players[0])) {
    std::iter_swap(output.players.begin(), output.players.begin() + 1);
  }

  return output;
}

Snapshot stateToSnapshot(torchcraft::State* state) {
  Snapshot output;
  output.mapBuildTileWidth =
      state->map_size[0] / tc::BW::XYWalktilesPerBuildtile;
  output.mapBuildTileHeight =
      state->map_size[1] / tc::BW::XYWalktilesPerBuildtile;
  output.mapTitle = state->map_title;

  // Assumption: There are two active players and any number of observer/neutral
  // players. Observers sometimes are Terran players who lift off their Command
  // Centers. Simple heuristic: Use "basic" (workers, overlords, etc.) units as
  // shibboleths of being an active player. Take the two players with the most
  // shibboleth units. This is a crude metric; it could fail in extreme
  // circumstances like weird maps or if a player is about to lose and hasn't
  // surrendered.
  std::vector<int> shibbolethTypes = {buildtypes::Terran_SCV->unit,
                                      buildtypes::Protoss_Probe->unit,
                                      buildtypes::Zerg_Drone->unit,
                                      buildtypes::Terran_Supply_Depot->unit,
                                      buildtypes::Protoss_Pylon->unit,
                                      buildtypes::Zerg_Overlord->unit};
  std::map<int, int> playerScore;
  for (auto& playerUnits : state->units) {
    for (auto& unit : playerUnits.second) {
      playerScore[unit.playerId] +=
          std::find(
              shibbolethTypes.begin(), shibbolethTypes.end(), unit.type) !=
              shibbolethTypes.end()
          ? 1000
          : 1;
    }
  }

  auto getActivePlayerId = [&]() {
    int output = -1;
    int maxShibboleths = -1;
    for (auto& pair : playerScore) {
      if (pair.second > maxShibboleths) {
        output = pair.first;
        maxShibboleths = pair.second;
      }
    }
    return output;
  };
  int playerId0 = getActivePlayerId();
  playerScore[playerId0] = -1;
  int playerId1 = getActivePlayerId();

  // Some replays have had frames with less than two players left in the game
  // I think this can happen if one player leaves/loses and the other opts to
  // remain in the game a little longer.
  if (playerId1 < 0) {
    throw std::runtime_error("Fewer than two players remain");
  }

  auto unitToSnapshot = [](const torchcraft::replayer::Unit& unit) {
    SnapshotUnit output;
    output.type = unit.type;
    output.x = unit.x;
    output.y = unit.y;
    output.health = unit.health;
    output.shields = unit.shield;
    output.energy = unit.energy;
    return output;
  };
  auto eligibleForSnapshot = [](const torchcraft::replayer::Unit& unit) {
    auto unitType = getUnitBuildType(unit.type);
    bool complete = unit.flags & torchcraft::replayer::Unit::Flags::Completed;
    bool isBuilding = unitType->isBuilding;
    return complete || isBuilding;
  };
  auto populatePlayer = [&](SnapshotPlayer& player, int playerId) {
    auto& resources = state->frame->resources[playerId];
    player.upgrades = resources.upgrades;
    player.upgradeLevels = resources.upgrades_level;
    player.techs = resources.techs;
    for (auto& unit : state->units[playerId]) {
      if (unit.playerId == playerId && eligibleForSnapshot(unit)) {
        output.players[playerId].units.push_back(unitToSnapshot(unit));
      }
    }
  };
  populatePlayer(output.players[0], playerId0);
  populatePlayer(output.players[1], playerId1);
  return output;
}

void saveSnapshot(const Snapshot& snapshot, const std::string& path) {
  std::ofstream stream(path, std::ios::binary);
  cereal::BinaryOutputArchive archive(stream);
  archive(snapshot);
}

Snapshot loadSnapshot(const std::string& path) {
  std::ifstream stream(path, std::ios::binary);
  cereal::BinaryInputArchive archive(stream);
  Snapshot output;
  archive(output);
  return output;
}

int SnapshotPlayer::getUpgradeLevel(int upgradeId) const {
  // Adapted from
  // https://github.com/TorchCraft/TorchCraft/blob/develop/include/torchcraft/state.h#L153
  auto upgradeBitMask = 1ll << upgradeId;
  if (!(upgrades & upgradeBitMask)) {
    return 0;
  }
  auto constexpr kLevelableUpgrades = 16;
  if (upgradeId >= kLevelableUpgrades) {
    return 1;
  }
  if (upgradeLevels & upgradeBitMask) {
    return 2;
  }
  if (upgradeLevels & (1ll << (upgradeId + kLevelableUpgrades))) {
    return 3;
  }
  return 1;
}

bool SnapshotPlayer::hasTech(int techId) const {
  // Adapted from
  // https://github.com/TorchCraft/TorchCraft/blob/develop/include/torchcraft/state.h#L171
  return techs & (1ll << techId);
}

std::string Snapshotter::outputDirectory() {
  return fmt::format("/checkpoint/{}/snapshots", getenv("USER"));
}

void Snapshotter::step(torchcraft::State* state) {
  cooldownFrames_ += lastFrame_;
  cooldownFrames_ -= state->frame_from_bwapi;
  lastFrame_ = state->frame_from_bwapi;
  if (cooldownFrames_ <= 0 && snapshots_ < maxSnapshots &&
      isCameraReady(state)) {
    ++snapshots_;
    cooldownFrames_ = cooldownFramesMax;
    auto snapshot = stateToSnapshot(state);
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    auto path =
        fmt::format("{}/{}-{}.bin", outputDirectory(), snapshotName, timestamp);
    int secondsTotal = state->frame_from_bwapi / 24;
    int seconds = secondsTotal % 60;
    int minutes = secondsTotal / 60;
    VLOG(0) << "Saving snapshot to " << path << " at " << minutes << "m"
            << seconds << "s";
    try {
      saveSnapshot(snapshot, path);
    } catch (std::exception const& exception) {
      LOG(WARNING) << "Exception saving snapshot: " << exception.what();
    }
  }
}

} // namespace cherrypi