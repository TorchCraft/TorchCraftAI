/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "module.h"
#include "task.h"

namespace cherrypi {

class MasterHarassTask;
class HarassTask;
class BuildingHarassTask;

/**
 * Scouts the enemy's base with a worker.
 * Harasses the enemy while scouting.
 */
class HarassModule : public Module {
 public:
  // for gas steal, only build and not cancel
  // is implemented, deactivated by default
  struct BuildPolicy {
    BuildPolicy() : BuildPolicy(false, false) {}
    BuildPolicy(bool br, bool cr) : buildRefinery(br), cancelRefinery(cr) {}
    bool buildRefinery;
    bool cancelRefinery;
  };

  // harassment in itself
  // attack worker is deactivated by default because
  // not robust against bots' worker defence
  struct TargetPolicy {
    TargetPolicy() : TargetPolicy(true, false) {}
    TargetPolicy(bool asd, bool aw)
        : attackResourceDepot(asd), attackWorkers(aw) {}
    bool attackResourceDepot;
    bool attackWorkers;
  };

  // what to do if oponent responds
  // TODO: come back to base -- needs smartMove for far away target
  struct FleePolicy {
    FleePolicy() : FleePolicy(true) {}
    FleePolicy(bool tag) : turnAroundGeyser(tag) {}
    bool turnAroundGeyser;
  };

  virtual ~HarassModule() = default;

  virtual void step(State* s) override;

  // visitor-/double dispath- style to keep behavior in module and data in tasks
  void postCommand(State* state, MasterHarassTask* htask) {
    throw std::runtime_error("HarassModule: calling postCommand on base class");
  }
  void postCommand(State* state, HarassTask* htask);
  void postCommand(State* state, BuildingHarassTask* htask);

  void setTargetPolicy(TargetPolicy pol) {
    targetPolicy_ = pol;
  }
  void setBuildPolicy(BuildPolicy pol) {
    buildPolicy_ = pol;
  }
  void setFleePolicy(FleePolicy pol) {
    fleePolicy_ = pol;
  }

 protected:
  bool attack(State* state, HarassTask* htask);
  bool attackResourceDepot(State* state, HarassTask* htask);
  bool attackWorkers(State* state, HarassTask* htask);
  bool dangerousAttack(State* state, HarassTask* task);
  bool buildRefinery(State* state, HarassTask* task);
  bool flee(State* state, HarassTask* task);
  bool exploreEnemyBase(State* state, HarassTask* task);
  std::vector<Position> getFleePositions(State* state, HarassTask* task);

  // basic task/upc management
  void consumeUPC(State* state, UpcId upcId, std::shared_ptr<UPCTuple> upc);
  std::shared_ptr<MasterHarassTask>
  createTask(State*, UpcId, std::shared_ptr<UPCTuple>);

  // policies
  TargetPolicy targetPolicy_;
  BuildPolicy buildPolicy_;
  FleePolicy fleePolicy_;

  Unit* enemyGeyser(Position const& pos); // assumes a single enemy geyser...
  Unit* enemyRefinery(Position const& pos);
  void findClosestGeyser(State* state, Position const& nmyLoc);
  void checkEnemyRefineryBuilt(State* state, Position const& nmyLoc);

  std::unordered_map<Position, Unit*> enemyGeyser_;
  std::unordered_map<Position, Unit*> enemyRefinery_;
};

} // namespace cherrypi
