/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <list>

#include "areainfo.h"
#include "blackboard.h"
#include "cherrypi.h"
#include "tilesinfo.h"
#include "tracker.h"
#include "unitsinfo.h"

#include <chrono>
#include <utility>

namespace BWEM {
class Map;
}
namespace tcbwapi {
class TCGame;
}

namespace cherrypi {

/// Type to represent upgrade level values
typedef int UpgradeLevel;

/**
 * Game state.
 *
 * The game state serves as the main input and output for bot modules. It
 * provides a global (player-wide) blackboard and access to the
 * TorchCraft::State object in the form of wrapper functions and data
 * structures.
 */
class State {
 public:
  explicit State(std::shared_ptr<tc::Client> client);
  State(State const&) = delete;
  virtual ~State();

  // Don't keep unit pointers around. They'll be invalidated on torchcraft state
  // updates.
  std::unordered_map<int32_t, tc::Unit*> const& units() const {
    return units_;
  }
  tc::Unit* unit(int32_t id) const {
    return units_.find(id) == units_.end() ? nullptr : units_.at(id);
  }

  FrameNum currentFrame() const {
    return currentFrame_;
  }

  /// Current game time in seconds, assuming "fastest" speed
  float currentGameTime() const {
    return (currentFrame_ * 42.0f) / 1000;
  }

  FrameNum latencyFrames() const {
    return tcstate_->lag_frames;
  }

  PlayerId playerId() const {
    return playerId_;
  }

  PlayerId neutralId() const {
    return neutralId_;
  }

  /// For replays, treat units from this player as allied units.
  void setPerspective(PlayerId id);

  int mapWidth() const {
    return mapWidth_;
  }
  int mapHeight() const {
    return mapHeight_;
  }
  const std::string mapName() const {
    return tcstate_->map_name;
  }
  const std::string mapTitle() const {
    return tcstate_->map_title;
  }
  Rect mapRect() const {
    return Rect(0, 0, mapWidth_, mapHeight_);
  }

  tc::Resources resources() const;

  Blackboard* board() const {
    return board_;
  }
  BWEM::Map* map() const {
    return map_.get();
  }

  template <typename T, typename... Args>
  std::shared_ptr<T> addTracker(Args&&... args) {
    auto tracker = std::make_shared<T>(std::forward<Args>(args)...);
    trackers_.emplace_back(tracker);
    return tracker;
  }
  std::list<std::shared_ptr<Tracker>> const& trackers() const {
    return trackers_;
  }

  UnitsInfo& unitsInfo() {
    return unitsInfo_;
  }
  TilesInfo& tilesInfo() {
    return tilesInfo_;
  }
  AreaInfo& areaInfo() {
    return areaInfo_;
  }

  std::vector<std::pair<std::string, std::chrono::milliseconds>>
  getStateUpdateTimes() const {
    std::vector<std::pair<std::string, std::chrono::milliseconds>>
        stateUpdateTimes;
    for (auto nameTime : stateUpdateTimeSpent_) {
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          nameTime.second);
      stateUpdateTimes.push_back(std::make_pair(nameTime.first, ms));
    }
    return stateUpdateTimes;
  }

  /// Get the current level of a given upgrade
  /// @param upgrade Upgrade for which to check the level
  /// @return Level of a given upgrade
  UpgradeLevel getUpgradeLevel(const BuildType* upgrade) const;

  /// Check whether a given technology has been researched
  /// @param tech Technology to check for
  /// @return true, if the technology has been researched
  bool hasResearched(const BuildType* tech) const;

  /// Updates internal mappings after the torchcraft state has been updated.
  void update();

  /// Get my race as reported by the game
  tc::BW::Race myRace() const;

  /// Get the player id of the first opponent
  PlayerId firstOpponent() const;

  /// Get the race that the game returns, for a given player
  tc::BW::Race raceFromClient(PlayerId playerId) const;

  /// Returns true if the game has ended.
  /// This will be the case if either player doesn't control any units for a
  /// some frames or if TorchCraft signals that the game has ended.
  bool gameEnded() const;
  bool won() const;
  bool lost() const;

  /// The underlying TorchCraft state.
  /// It's recommended to access the game state via the other functions of this
  /// class instead.
  tc::State* tcstate() {
    return tcstate_;
  }

  void setCollectTimers(bool collect);

  UnitsInfo* unitsInfoPtr() {
    return &unitsInfo_;
  }
  TilesInfo* tilesInfoPtr() {
    return &tilesInfo_;
  }
  AreaInfo* areaInfoPtr() {
    return &areaInfo_;
  }

  void setMapHack(bool h) {
    mapHack_ = h;
  }
  bool mapHack() {
    return mapHack_;
  }

 private:
  typedef std::unordered_map<int, bool> Tech2StatusMap;
  typedef std::unordered_map<int, UpgradeLevel> Upgrade2LevelMap;

  void initTechnologyStatus();
  void initUpgradeStatus();
  void updateBWEM();
  void updateTechnologyStatus();
  void updateUpgradeStatus();
  void updateTrackers();
  void updateFirstToLeave();
  void findEnemyInfo();

  std::shared_ptr<tc::Client> client_;
  tc::State* tcstate_;
  std::unordered_map<int32_t, tc::Unit*> units_;

  std::unique_ptr<BWEM::Map> map_;
  std::unique_ptr<tcbwapi::TCGame> tcbGame_;

  Blackboard* board_;
  std::list<std::shared_ptr<Tracker>> trackers_;

  FrameNum currentFrame_ = 0;
  PlayerId playerId_ = 0;
  PlayerId neutralId_ = 0;
  PlayerId firstToLeave_ = -1;
  int mapWidth_ = 0;
  int mapHeight_ = 0;

  UnitsInfo unitsInfo_{this};
  TilesInfo tilesInfo_{this};
  AreaInfo areaInfo_{this};

  bool sawFirstEnemyUnit_ = false;
  bool collectTimers_ = false;

  Tech2StatusMap tech2StatusMap_;
  Upgrade2LevelMap upgrade2LevelMap_;

  std::vector<std::pair<std::string, Duration>> stateUpdateTimeSpent_;

  bool mapHack_ = false;
};

} // namespace cherrypi
