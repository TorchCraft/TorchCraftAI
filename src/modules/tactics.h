/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "module.h"
#include "state.h"
#include "task.h"
#include "tilesinfo.h"

#include <random>

DECLARE_uint64(tactics_fight_or_flee_interval);

namespace cherrypi {

struct Unit;

class TacticsTask : public Task {
 public:
  TacticsTask(int upcId) : Task(upcId) {}

  virtual void update(State* state) override {
    removeDeadOrReassignedUnits(state);
  }

  virtual void cancel(State* state) override {
    units().clear();
    state->board()->updateTasksByUnit(this);

    Task::cancel(state);
  }

  void setUnits(State* state, std::unordered_set<Unit*> units) {
    this->units() = std::move(units);
    state->board()->updateTasksByUnit(this);
  }

  std::vector<Unit*> myUnits;
  Position targetPos;
  Position averagePos;
  bool isFighting = false;

  virtual const char* getName() const override {
    return "Tactics";
  };
};

struct TacticsGroup {
  std::vector<Unit*> enemyUnits;
  std::vector<Unit*> myUnits;
  Unit* targetUnit = nullptr;
  Position targetPos;
  Position averagePos;
  std::shared_ptr<TacticsTask> task;
  bool hasEnoughUnits = false;
  bool hasEnemyGroundUnits = false;
  bool hasEnemyAirUnits = false;
  bool hasEnemyBuildings = false;
  bool hasEnemyCloakedUnits = false;
  bool hasEnemyTanks = false;
  bool hasEnemyReavers = false;
  bool hasEnemyBunkers = false;
  bool hasEnemyWorkers = false;
  bool hasEnemyAntiGround = false;
  bool hasEnemyAntiAir = false;
  bool hasEnemyStaticDefence = false;
  bool enemiesAreAttacking = false;
  bool enemiesInOurBase = false;
  bool isAggressiveGroup = true;
  double score = 0.0;
  bool searchAndDestroy = false;
  bool isIdleGroup = false;
  bool isScoutGroup = false;
  bool enemyIsOnlyWorkers = true;
};

struct TacticsMapNode {
  Tile* tile = nullptr;
  TacticsGroup* group = nullptr;
  Unit* nearestEnemy = nullptr;
};

struct TacticsFightScores {
  double score = 0;
  bool airFight = true;
  bool groundFight = true;
};

struct TacticsState {
 public:
  int srcUpcId_;
  // The distance around each enemy unit that will be considered "inside" their
  // group. Any of our units in this area will be assigned to the group, and
  // this effectively ends up being the distance away from enemy units that
  // our units flee.
  float insideGroupDistance_ = 4 * 16;
  uint8_t visitNumber_ = 0;
  std::vector<uint8_t> tileVisitTracker_ =
      std::vector<uint8_t>(TilesInfo::tilesWidth * TilesInfo::tilesHeight);
  std::vector<uint8_t> tileSpotTakenTracker_ =
      std::vector<uint8_t>(TilesInfo::tilesWidth * TilesInfo::tilesHeight);
  std::vector<TacticsMapNode> nodeInsideGroupTracker_ =
      std::vector<TacticsMapNode>(
          TilesInfo::tilesWidth * TilesInfo::tilesHeight);
  std::vector<TacticsGroup*> nodeGroupEdgeTracker_ = std::vector<TacticsGroup*>(
      TilesInfo::tilesWidth * TilesInfo::tilesHeight);
  std::unordered_map<Unit*, TacticsGroup*> hardAssignedUnits_;
  std::unordered_map<Unit*, TacticsGroup*> softAssignedUnits_;
  std::list<TacticsGroup> groups_;

  // Used for creating groups
  void createTacticsGroups(State* state, std::vector<uint8_t>& inBaseArea);
  void collectMapNodesCoveredByGroups(State* state);
  void assignUnits(
      State* state,
      std::unordered_set<Unit*>& wasInAGroup,
      std::vector<Unit*> leftoverWorkers,
      std::vector<std::shared_ptr<Task>>& tasks);

  // Used for fight/flee prediction
  TacticsFightScores combatSimFightPrediction(
      State* state,
      TacticsGroup& g,
      std::unordered_map<Unit*, int>& nmyInStaticDefenceRange,
      std::unordered_map<Unit*, int>& nmyAlmostInStaticDefenceRange);
  Unit* getBestEnemyTarget(
      State* state,
      TacticsGroup& g,
      Unit* u,
      std::unordered_map<Unit*, int>& meleeTargetCount,
      std::unordered_map<Unit*, int>& lastTargetInRange,
      bool& anySpiderMinesNearby);
  bool shouldRunFromHiddenTarget(TacticsGroup& g, Unit* u, Unit* target);

  // Used for processing orders
  Position
  idleGroupTargetPos(State* state, Unit* u, std::vector<uint8_t>& inBaseArea);
  Position findRunPos(State* state, Unit* u, std::vector<uint16_t>& fleeScore);
  Position scoutGroupTargetPos(
      State* state,
      TacticsGroup& g,
      Unit* u,
      std::unordered_map<Unit*, std::pair<int, Position>>& scoutTarget,
      std::ranlux24& rngEngine);
  Position searchAndDestroyGroupTargetPos(
      State* state,
      Unit* u,
      std::unordered_map<Unit*, std::pair<int, Position>>&
          searchAndDestroyTarget,
      std::ranlux24& rngEngine);

 private:
  // Used for creating groups
  void addEnemyUnitToGroup(
      State* state,
      Unit* u,
      TacticsGroup* group,
      std::vector<uint8_t> const& inBaseArea);
  void assignUnitsBasedOnPreviousAssignments(
      State* state,
      std::unordered_set<Unit*>& wasInAGroup,
      std::vector<std::shared_ptr<Task>>& tasks);
  void collectAvailableUnits(State* state, std::vector<Unit*>& availableUnits);
  void assignScoutingUnits(State* state, std::vector<Unit*>& availableUnits);
  bool aggressiveUnit(State* state, Unit* unit);
  double scoreUnitForGroup(State* state, Unit* u, TacticsGroup& g);
  void assignNecessaryUnits(State* state, std::vector<Unit*>& availableUnits);
  void assignDetectors(std::vector<Unit*>& availableUnits);
  void assignLeftovers(
      State* state,
      std::vector<Unit*>& availableUnits,
      std::vector<Unit*>& leftoverWorkers);

  // Used for fight/flee prediction
  bool isAllyInRangeOfEnemy(TacticsGroup& g);
  void prepareCombatSimulationData(
      State* state,
      TacticsGroup& g,
      std::vector<Unit*>& nearbyAllies,
      std::unordered_set<Unit*>& nearbyEnemies,
      std::unordered_map<Unit*, int>& nmyInStaticDefenseRange,
      std::unordered_map<Unit*, int>& nmyAlmostInStaticDefenseRange);
  std::pair<double, double> combatSimCalculateFightScoreMod(
      State* state,
      TacticsGroup& g,
      std::vector<Unit*>& nearbyAllies,
      std::unordered_set<Unit*>& nearbyEnemies,
      std::unordered_map<Unit*, int>& nmyInStaticDefenseRange);

  // Used for order processing
  int getRandomCoord(int range, std::ranlux24& rngEngine);
  Position
  findMoveAwayPos(State* state, Unit* u, Position source, float distance);
};

/**
 * The Tactics module decides where on the map to allocate combat units.
 *
 * * Identifies clusters of enemy units, and which allied units are currently
 * engaged with them.
 * * Allocates other allied units to various jobs like attacking, defending,
 * and scouting.
 * * Uses a combat simulator to identify which clusters of allies should fight
 * and which should flee (and where they should go)
 *
 * Finally, outputs a UPC for each group of units indicating where they should
 * go or what they should fight.
 */
class TacticsModule : public Module {
 public:
  virtual void step(State* s) override;
  virtual void onGameEnd(State* s) override;

 protected:
  UpcId findSourceUpc(State* s);

  FrameNum lastProcess_ = 0;

  virtual void process(State* state, int srcUpcId);

  // Methods likely useful in all TacticsModule subclasses

  /// Create groups based on distance rules, useful for the
  /// scouting/worker/search and destroy functionality.
  void formGroups(
      State* state,
      TacticsState& tstate,
      std::vector<Unit*>& leftoverWorkers,
      std::unordered_set<Unit*>& wasInAGroup);

  /// Return unused workers, doesn't rely on group creation, just on existance
  /// of
  /// leftoverWorkers which specifies the workers and wasInAGroup which
  /// specifies
  /// which will automatically be reassigned based on attachement to a
  /// previously
  /// existing task
  void distributeLeftoverWorkers(
      std::unordered_set<Unit*>& unitSet,
      std::vector<Unit*>& leftoverWorkers,
      std::unordered_set<Unit*>& wasInAGroup);

  /// Takes the scouting & search and destroy groups created in formGroups and
  /// issues the commands, also returns leftoverWorkers.
  void processNonFightFleeGroup(
      State* state,
      TacticsState& tstate,
      TacticsGroup& g,
      std::vector<Unit*>& leftoverWorkers,
      std::unordered_set<Unit*>& wasInAGroup);

  /// Creates a move upc for unit with given target
  void moveUnit(State* state, UpcId srcUpcId, Unit* u, Position target);

  // Methods likely only usable in the subclasses very similar to the base
  // module.

  /// Uses combat sim + rules to put each unit into a fight or flee vector
  std::pair<double, double> distributeFightFlee(
      State* state,
      TacticsState& tstate,
      TacticsGroup& g,
      std::vector<Unit*>& fightUnits,
      std::vector<Unit*>& fleeUnits);

  /// Takes a group and the fight/flee assignments and issues commands
  void processOrders(
      State* state,
      TacticsGroup& g,
      int srcUpcId,
      double deleteScore,
      double moveScore,
      std::vector<Unit*>& fightUnits,
      std::vector<Unit*>& fleeUnits,
      std::vector<Unit*>& leftoverWorkers,
      std::unordered_set<Unit*>& wasInAGroup);

  std::vector<uint8_t> inBaseArea_ =
      std::vector<uint8_t>(TilesInfo::tilesWidth * TilesInfo::tilesHeight);
  FrameNum lastUpdateInBaseArea_ = 0;

  std::vector<uint16_t> fleeScore_ =
      std::vector<uint16_t>(TilesInfo::tilesWidth * TilesInfo::tilesHeight);
  FrameNum lastUpdateFleeScore_ = 0;

  std::unordered_map<Unit*, std::pair<int, Position>> searchAndDestroyTarget_;
  std::unordered_map<Unit*, std::pair<int, Position>> scoutTarget_;
  std::ranlux24 rngEngine_{42};

  std::unordered_map<Unit*, int> lastTargetInRange_;
  std::unordered_map<Unit*, int> lastMove_;
};

} // namespace cherrypi
