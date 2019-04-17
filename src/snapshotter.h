/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cereal/archives/binary.hpp>

#include "fmt/format.h"
#include "gameutils/scenariospecification.h"
#include "module.h"
#include "state.h"

namespace cherrypi {

/// A low-resolution snapshot of a unit state, used as a component of Snapshots.
struct SnapshotUnit {
  int type;
  int x;
  int y;
  int health;
  int shields;
  int energy;
  // It would be nice to also capture [cooldown, angle, velocity] but we can't
  // modify those properties via OpenBW

  template <class Archive>
  void serialize(Archive& ar) {
    ar(type, x, y, health, shields, energy);
  }
};

/// A low-resolution snapshot of a player state, used as a component of
/// Snapshots.
struct SnapshotPlayer {
  // Upgrade/Tech serialization format taken from
  // torchcraft::replayer::Frame::Resourcs
  int64_t upgrades;
  int64_t upgradeLevels;
  int64_t techs;
  std::vector<SnapshotUnit> units;

  template <class Archive>
  void serialize(Archive& ar) {
    ar(upgradeLevels, techs, units);
  }

  /// Convenience method for accessing TC-formatted upgradeLevels.
  /// @param upgradeId
  ///   BWAPI/buildtype->upgrade ID of the upgrade to retrieve
  int getUpgradeLevel(int upgradeId) const;

  /// Convenience method for accessing TC-formatted techs.
  /// @param upgradeId
  ///   BWAPI/buildtype->tech ID of the tech to retrieve
  bool hasTech(int techId) const;
};

/// A low-resolution snapshot of a game state,
/// intended for producing micro training scenarios
struct Snapshot {
  std::vector<SnapshotPlayer> players = {{}, {}};
  int mapBuildTileWidth;
  int mapBuildTileHeight;
  std::string mapTitle;

  template <class Archive>
  void serialize(Archive& ar) {
    ar(players);
  }
};

FixedScenario snapshotToScenario(const Snapshot&);
Snapshot stateToSnapshot(torchcraft::State*);
void saveSnapshot(const Snapshot& snapshot, const std::string& path);
Snapshot loadSnapshot(const std::string& path);

/// Records "snapshots" -- low-fidelity recordings of game state which can be
/// loaded as micro scenarios.
class Snapshotter {
 public:
  /// Is the current game state appropriate for taking a snapshot?
  virtual bool isCameraReady(torchcraft::State*) {
    return false;
  };

  /// Minimum number of frames in between taking snapshots
  int cooldownFramesMax = 24 * 10;

  /// Stop snapshotting a game after this many snapshots have been taken
  int maxSnapshots = 20;

  virtual std::string outputDirectory();
  std::string snapshotName = "snapshot";

  void step(torchcraft::State*);

 protected:
  int lastFrame_ = 0;
  int cooldownFrames_ = 0;
  int snapshots_ = 0;
};

} // namespace cherrypi
