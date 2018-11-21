/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "state.h"

#include "upcfilter.h"
#include "utils.h"

#include <BWAPI.h>
#include <bwem/bwem.h>
#include <glog/logging.h>
#include <tcbwapi/tcgame.h>
#include <tcbwapi/tcunit.h>

#include <utility>

namespace cherrypi {

namespace {

int totalSupplyUsed(tc::State* tcstate, PlayerId player) {
  auto it = tcstate->units.find(player);
  if (it == tcstate->units.end()) {
    return 0;
  }

  int supply = 0;
  for (auto& unit : it->second) {
    supply += tc::BW::data::SupplyRequired[unit.type];
  }
  return supply;
}

} // namespace

State::State(std::shared_ptr<tc::Client> client)
    : client_(std::move(client)),
      tcstate_(client_->state()),
      board_(new Blackboard(this)),
      mapWidth_(tcstate_->map_size[0]),
      mapHeight_(tcstate_->map_size[1]) {
  currentFrame_ = tcstate_->frame_from_bwapi;
  // Note that for replays, playerId_ will default to 0 until setPerspective()
  // is called.
  playerId_ = tcstate_->player_id;
  neutralId_ = tcstate_->neutral_id;

  board_->addUPCFilter(std::make_shared<AssignedUnitsFilter>());
  board_->addUPCFilter(std::make_shared<SanityFilter>());

  initTechnologyStatus();
  initUpgradeStatus();
  findEnemyInfo();
  board_->init();
}

State::~State() {
  delete board_;

  // Free the BWEM map instance before freeing the TorchCraft BWAPI wrapper game
  // instance (since the former references data in the latter).
  map_.reset();
  tcbGame_.reset();
}

void State::setPerspective(PlayerId id) {
  if (!tcstate_->replay) {
    throw std::runtime_error("Cannot change perspective for non-replay games");
  }
  playerId_ = id;

  // TODO Consider adding an init() method that's called from Player::init()
  // instead of initializing in the constructor (where we can't set the
  // perspective).
  findEnemyInfo();
}

tc::Resources State::resources() const {
  return tcstate_->frame->resources[playerId_];
}

bool State::hasResearched(const BuildType* tech) const {
  if (!tech) {
    LOG(ERROR) << "Null pointer to tech type";
    return false;
  }
  auto tech_it = tech2StatusMap_.find(tech->tech);
  if (tech_it != tech2StatusMap_.end()) {
    return tech_it->second;
  }
  LOG(ERROR) << "Tech status requested for an unknown tech " << tech->tech;
  return false;
}

UpgradeLevel State::getUpgradeLevel(const BuildType* upgrade) const {
  if (!upgrade) {
    LOG(ERROR) << "Null pointer to upgrade type";
    return false;
  }
  auto upg_it = upgrade2LevelMap_.find(upgrade->upgrade);
  if (upg_it != upgrade2LevelMap_.end()) {
    return upg_it->second;
  }
  LOG(ERROR) << "Upgrade level requested for an unknown upgrade "
             << upgrade->upgrade;
  return 0;
}

void State::setCollectTimers(bool collect) {
  collectTimers_ = collect;
}

void State::update() {
  // Nuke before starting to collect timings
  stateUpdateTimeSpent_.clear();

  board_->clearCommands();

  bool firstFrame = false;
  if (map_ == nullptr) {
    firstFrame = true;
    auto start = hires_clock::now();
    map_ = BWEM::Map::Make();
    VLOG(1) << "Running BWEM analysis...";
    tcbGame_ = std::make_unique<tcbwapi::TCGame>();
    tcbGame_->setState(tcstate_);
    map_->Initialize(tcbGame_.get());
    map_->EnableAutomaticPathAnalysis();
    if (!map_->FindBasesForStartingLocations()) {
      VLOG(0) << "Failed to find BWEM bases for starting locations";
    }
    auto duration = hires_clock::now() - start;
    VLOG(1) << "Analysis done, found " << map_->Areas().size() << " areas and "
            << map_->ChokePointCount() << " choke points in "
            << std::chrono::duration_cast<std::chrono::milliseconds>(duration)
                   .count()
            << "ms";
  }

  currentFrame_ = tcstate_->frame_from_bwapi;

  // Update id -> unit mapping
  units_.clear();
  for (auto& e : tcstate_->frame->units) {
    for (auto& unit : e.second) {
      // Ignore "unknown" unit types that will just lead to confusion regarding
      // state checks and module functionality.
      auto ut = tc::BW::UnitType::_from_integral_nothrow(unit.type);
      if (!ut) {
        continue;
      }
      // Ignore units that our perspective can't observe (except for neutral
      // units on the first frame).
      // We want this for replays and when playing with map hack (full
      // observation).
      if (!(unit.visible & (1 << playerId_))) {
        if (e.first != neutralId_ || !firstFrame) {
          continue;
        }
      }
      units_[unit.id] = &unit;
    }
  }

  tilesInfo_.preUnitsUpdate();
  std::chrono::time_point<hires_clock> start;

  if (collectTimers_) {
    start = hires_clock::now();
  }
  unitsInfo_.update();
  if (collectTimers_) {
    auto timeTaken = hires_clock::now() - start;
    stateUpdateTimeSpent_.push_back(
        std::make_pair("UnitsInfo::update()", timeTaken));
  }
  tilesInfo_.postUnitsUpdate();

  if (!sawFirstEnemyUnit_) {
    for (auto eunit : unitsInfo().enemyUnits()) {
      board_->post(Blackboard::kEnemyRaceKey, eunit->type->race);
      sawFirstEnemyUnit_ = true;
      break;
    }
  }

  updateBWEM();
  start = hires_clock::now();
  areaInfo_.update();
  if (collectTimers_) {
    auto timeTaken = hires_clock::now() - start;
    stateUpdateTimeSpent_.push_back(
        std::make_pair("AreaInfo::update()", timeTaken));
  }

  updateTechnologyStatus();
  updateUpgradeStatus();
  updateTrackers();
  if (tcstate_->replay) {
    updateFirstToLeave();
  }

  if (collectTimers_) {
    start = hires_clock::now();
  }
  board_->update();
  if (collectTimers_) {
    auto timeTaken = hires_clock::now() - start;
    stateUpdateTimeSpent_.push_back(
        std::make_pair("Board::update()", timeTaken));
  }
}

bool State::gameEnded() const {
  return tcstate_->game_ended;
}

bool State::won() const {
  if (board_->hasKey("__mock_won_game__")) {
    return true; // used for testing
  }
  if (!tcstate_->game_ended) {
    return false;
  }

  if (tcstate_->replay) {
    // First, check if there's a clear winner wrt supply. It's common for the
    // winner to leave first after agreeing on the outcome via in-game chat.
    auto mySupply = totalSupplyUsed(tcstate_, playerId_);
    auto theirSupply = totalSupplyUsed(tcstate_, 1 - playerId_);

    if (mySupply > 1.5 * theirSupply) {
      return true;
    } else if (theirSupply > 1.5 * mySupply) {
      return false;
    }

    // Otherwise, the first one to leave loses the game
    if (firstToLeave_ >= 0) {
      return firstToLeave_ != playerId_;
    }

    // If nobody left, use supply count to determine game outcome.
    return mySupply > theirSupply;
  }

  if (tcstate_->game_won) {
    int killedEnemy = 0;
    auto* state = const_cast<State*>(this);
    for (auto& unit : state->unitsInfo().allUnitsEver()) {
      if (unit->dead && unit->isEnemy) {
        killedEnemy++;
      }
    }

    return (state->unitsInfo().myBuildings().size() >= 1 && killedEnemy > 0);
  } else {
    return false;
  }
}

bool State::lost() const {
  if (tcstate_->replay) {
    return !won();
  }

  return tcstate_->game_ended && !tcstate_->game_won;
}

void State::initTechnologyStatus() {
  const auto& techs = buildtypes::allTechTypes;
  for (auto* tech : techs) {
    if (!tech) {
      LOG(ERROR) << "Null pointer encountered when querying all techs";
    } else if (tech2StatusMap_.find(tech->tech) != tech2StatusMap_.end()) {
      LOG(ERROR) << "Multiple techs with the same ID encountered "
                 << "when querying all techs (" << tech->tech << ")";
    } else {
      tech2StatusMap_[tech->tech] = false;
    }
  }
}

void State::initUpgradeStatus() {
  const auto& upgrades = buildtypes::allUpgradeTypes;
  for (const auto* upg : upgrades) {
    if (!upg) {
      LOG(ERROR) << "Null pointer encountered when querying all upgrades";
    } else {
      // different levels of the same upgrade are represented by different
      // build types with the same ID, so check for ID uniqueness is
      // invalid in this case
      upgrade2LevelMap_[upg->upgrade] = 0;
    }
  }
}

void State::updateBWEM() {
  if (map_ == nullptr || tcbGame_ == nullptr) {
    return;
  }

  // Update BWEM instance with destroyed static and neutral units
  for (auto unit : unitsInfo_.getDestroyUnits()) {
    if (unit->playerId != neutralId_) {
      continue;
    }
    if (unit->type->isMinerals) {
      BWAPI::Unit bwu = tcbGame_->getUnit(unit->id);
      try {
        map_->OnMineralDestroyed(bwu);
      } catch (std::exception& e) {
        LOG(WARNING) << "Exception removing mineral from BWEM map: "
                     << e.what();
      }
    } else if (
        unit->type->isBuilding && !unit->type->isGas && !unit->lifted()) {
      BWAPI::Unit bwu = tcbGame_->getUnit(unit->id);
      if (bwu == nullptr) {
        LOG(WARNING) << "Destroyed unit " << utils::unitString(unit)
                     << " is unknown to TC game wrapper";
      } else {
        try {
          map_->OnStaticBuildingDestroyed(bwu);
        } catch (std::exception& e) {
          LOG(WARNING) << "Exception removing static building from BWEM map: "
                       << e.what();
        }
      }
    }
  }
}

void State::updateTechnologyStatus() {
  for (auto& tech2status : tech2StatusMap_) {
    auto tt = tc::BW::TechType::_from_integral_nothrow(tech2status.first);
    if (!tt) {
      continue;
    }
    if (!tech2status.second && tcstate_->hasResearched(*tt)) {
      tech2status.second = true;
    }
  }
}

void State::updateUpgradeStatus() {
  for (auto& upgrade2level : upgrade2LevelMap_) {
    auto ut = tc::BW::UpgradeType::_from_integral_nothrow(upgrade2level.first);
    if (!ut) {
      continue;
    }
    upgrade2level.second = tcstate_->getUpgradeLevel(*ut);
  }
}

void State::updateTrackers() {
  auto it = trackers_.begin();
  while (it != trackers_.end()) {
    auto tracker = *it;
    tracker->update(this);

    switch (tracker->status()) {
      case TrackerStatus::Timeout:
        VLOG(1) << "Timeout for tracker";
        break;
      case TrackerStatus::Success:
        VLOG(1) << "Tracker reported success";
        break;
      case TrackerStatus::Failure:
        VLOG(1) << "Tracker reported failure";
        break;
      case TrackerStatus::Cancelled:
        VLOG(1) << "Tracker was cancelled";
        break;
      default:
        // Keep tracker, advance in loop
        ++it;
        continue;
    }

    trackers_.erase(it++);
  }
}

void State::updateFirstToLeave() {
  // For replays, we need to figure out the winner ourselves. Here, we track
  // which player left first.
  if (firstToLeave_ >= 0) {
    return;
  }

  for (auto id : {playerId_, 1 - playerId_}) {
    auto it = tcstate_->player_info.find(id);
    if (it == tcstate_->player_info.end()) {
      LOG(ERROR) << "Missing player information for " << id;
      continue;
    }
    if (it->second.has_left) {
      firstToLeave_ = id;
      VLOG(0) << "Player " << it->second.name << " has left the game";
      break;
    }
  }
}

void State::findEnemyInfo() {
  std::string ename = "NONAME";
  tc::BW::Race erace = tc::BW::Race::Unknown;
  bool foundEnemy = false;

  for (auto& it : tcstate_->player_info) {
    auto& pinfo = it.second;
    VLOG(3) << "Player " << pinfo.id << " (" << pinfo.name << ") has race "
            << pinfo.race._to_string();
    // Naive assumption of two-player replays: all other players are considered
    // enemies
    if (pinfo.is_enemy ||
        (tcstate_->replay && pinfo.id != playerId_ && pinfo.id != neutralId_)) {
      if (foundEnemy) {
        if (tcstate_->replay) {
          // Ignore multiple "enemy" players in replays. There might be
          // observers.
          continue;
        }
        LOG(FATAL) << "More than one enemy? Can't do that, Steve";
      }
      ename = pinfo.name;
      erace = pinfo.race;
      foundEnemy = true;
    }
  }

  if (foundEnemy) {
    VLOG(0) << "Enemy: " << ename << " playing " << erace._to_string();
  } else {
    LOG(WARNING) << "No enemy information found, assuming defaults";
  }
  VLOG(0) << "Map: " << mapName();
  VLOG(0) << "Game is being played at LF" << latencyFrames();
  board_->post(Blackboard::kEnemyNameKey, ename);
  board_->post(Blackboard::kEnemyRaceKey, erace._to_integral());
}

tc::BW::Race State::myRace() const {
  return raceFromClient(playerId());
}

PlayerId State::firstOpponent() const {
  for (auto& it : tcstate_->player_info) {
    auto& pinfo = it.second;
    if (pinfo.is_enemy) {
      return pinfo.id;
    }
  }
  throw std::runtime_error("Cannot find any opponents");
}

tc::BW::Race State::raceFromClient(PlayerId playerId) const {
  auto it = tcstate_->player_info.find(playerId);
  if (it != tcstate_->player_info.end()) {
    return it->second.race;
  }
  return tc::BW::Race::Unknown;
}

} // namespace cherrypi
