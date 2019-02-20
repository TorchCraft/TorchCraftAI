/*
 * CopyrightOA (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "modules/harass.h"
#include "movefilters.h"
#include "state.h"
#include "utils.h"

#include <bwem/map.h>
#include <math.h>

namespace cherrypi {

REGISTER_SUBCLASS_0(Module, HarassModule);

/*
 * contains a common interface and helpers to check for status
 */
class MasterHarassTask : public Task {
 public:
  MasterHarassTask(UpcId upcId, Position nmyLocation, Unit* u)
      : Task(upcId, {u}), location_(nmyLocation) {
    setStatus(TaskStatus::Ongoing);
  }

  Position const& location() const {
    return location_;
  }

  void update(State* state) override {
    removeDeadOrReassignedUnits(state);
  }

  Unit* unitp() const {
    if (units().empty()) {
      LOG(DFATAL) << "taking the unit of a task without units";
    }
    return (*units().begin());
  }

  Position pos() const {
    auto unit = unitp();
    return Position(unit->x, unit->y);
  }

  virtual void postCommand(State* state, HarassModule* module) = 0;
  virtual const char* getName() const override {
    return "MasterHarass";
  };

 protected:
  Position location_;
};

class BuildingHarassTask : public MasterHarassTask {
 public:
  BuildingHarassTask(UpcId upcId, Position nmyLocation, Unit* u)
      : MasterHarassTask(upcId, nmyLocation, u) {}

  void postCommand(State* state, HarassModule* module) override {
    module->postCommand(state, this);
  }

  void update(State* state) override {
    MasterHarassTask::update(state);
    if (units().empty()) {
      setStatus(TaskStatus::Failure);
      VLOG(3) << "Building destroyed, building task failed";
      return;
    }
    if (unitp()->completed()) {
      setStatus(TaskStatus::Success);
      VLOG(3) << "building completed";
      return;
    }
  }
  virtual const char* getName() const override {
    return "BuildingHarass";
  };
};

class HarassTask : public MasterHarassTask {
 public:
  HarassTask(UpcId upcId, Position nmyLocation, Unit* u, Module* owner)
      : MasterHarassTask(upcId, nmyLocation, u), owner_(owner) {}

  void postCommand(State* state, HarassModule* module) override {
    module->postCommand(state, this);
  }

  void update(State* state) override {
    if (!units().empty()) {
      if (unitp()->dead) {
        VLOG(2) << "harasser dead";
      } else {
        auto t = state->board()->taskDataWithUnit(unitp());
        if (t.owner != owner_) {
          if (!t.task && status() != TaskStatus::Cancelled) {
            LOG(WARNING) << "harasser reassigned to no task";
          } else {
            VLOG(2) << "harasser reassgined to task " << t.task->upcId()
                    << " by " << t.owner->name();
          }
        }
      }
    }
    MasterHarassTask::update(state);
    if (units().empty()) {
      if (building()) { // check previous state
        // unit died while building: success
        // no check here for the cause of death
        // (might have been attacked while building)
        // the exact status has no effect on what to do next
        setStatus(TaskStatus::Success);
        VLOG(2) << "building supposedly succeeded";
        return;
      } else {
        setStatus(TaskStatus::Failure);
        VLOG(2) << "harassment task " << upcId() << " failed, "
                << "harasser dead or reassigned";
        return;
      }
    }
    checkBuild(state);
    checkAttack(state);
    checkFlee(state);
  }

  bool attacking() const {
    return attacking_;
  }

  bool moving() const {
    return utils::isExecutingCommand(unitp(), tc::BW::UnitCommandType::Move);
  }

  bool fleeing() const {
    return fleeing_;
  }

  bool building() const {
    return building_;
  }
  Unit* targetUnit() const {
    return targetUnit_;
  }

  Position const& targetPos() const {
    return targetPos_;
  }

  void attack(State* state, Unit* target) {
    initiateAction(state);
    auto me = unitp();
    auto cmd = tc::Client::Command(
        tc::BW::Command::CommandUnit,
        me->id,
        tc::BW::UnitCommandType::Attack_Unit,
        target->id);
    state->board()->postCommand(cmd, upcId());
  }

  void move(State* state, Position tgtPos) {
    initiateAction(state);
    auto me = unitp();
    auto cmd = tc::Client::Command(
        tc::BW::Command::CommandUnit,
        me->id,
        tc::BW::UnitCommandType::Move,
        -1,
        tgtPos.x,
        tgtPos.y);
    targetPos_ = tgtPos;
    shouldMove_ = state->currentFrame();
    state->board()->postCommand(cmd, upcId());
  }

  bool shouldMove(State* state) {
    // to avoid sending too many commands
    return shouldMove_ + movefilters::kTimeUpdateMove < state->currentFrame();
  }

  void flee(State* state, const Position& tgtPos) {
    move(state, tgtPos);
    fleeing_ = true;
  }

  void build(State* state, BuildType const* buildtype, Unit* gas) {
    initiateAction(state);
    auto me = unitp();
    Position pos = Position(
        (gas->unit.pixel_x - buildtype->dimensionLeft) / 8u,
        (gas->unit.pixel_y - buildtype->dimensionUp) / 8u);
    auto cmd = tc::Client::Command(
        tc::BW::Command::CommandUnit,
        me->id,
        tc::BW::UnitCommandType::Build,
        -1,
        pos.x,
        pos.y,
        buildtype->unit);
    state->board()->postCommand(cmd, upcId());
  }

  virtual const char* getName() const override {
    return "Harass";
  };

 protected:
  void initiateAction(State* state) {
    attacking_ = false; // attack
    targetUnit_ = nullptr;

    shouldMove_ = -1; // move
    targetPos_ = Position(-1, -1);
    fleeing_ = false;
  }

  void checkAttack(State* state) {
    attacking_ = false;
    targetUnit_ = nullptr;

    auto me = unitp();
    auto tcu = me->unit;
    auto orderTypes =
        tc::BW::commandToOrders(tc::BW::UnitCommandType::Attack_Unit);
    for (auto& order : tcu.orders) {
      for (auto ot : orderTypes) {
        auto odt = tc::BW::Order::_from_integral_nothrow(order.type);
        if (!odt) {
          continue;
        }
        if (*odt == ot) {
          auto unit = state->unitsInfo().getUnit(order.targetId);
          if (unit && !unit->dead) {
            attacking_ = true;
            targetUnit_ = unit;
            return;
          }
        }
      }
    }
  }

  void checkFlee(State* state) {
    fleeing_ = fleeing_ && moving();
  }

  void checkBuild(State* state) {
    building_ =
        utils::isExecutingCommand(unitp(), tc::BW::UnitCommandType::Build);
  }

  Module* owner_; // debug: keep track of reassignment

  Position targetPos_ = Position(-1, -1);

  Unit* targetUnit_ = nullptr;
  bool attacking_ = false;

  bool fleeing_ = false;
  int shouldMove_ = -1;
  bool building_ = false;
};

void HarassModule::step(State* state) {
  auto board = state->board();

  for (auto& pairIdUPC : board->upcsWithSharpCommand(Command::Harass)) {
    consumeUPC(state, pairIdUPC.first, pairIdUPC.second);
  }

  // post commands regarding all tasks, including new ones
  for (auto task : board->tasksOfModule(this)) {
    if (!task->finished())
      task->update(state);
    if (task->finished()) {
      VLOG(1) << "task finished!"
              << " status: " << (int)task->status();
      continue;
    }
    auto htask = std::dynamic_pointer_cast<MasterHarassTask>(task);
    // update persistent info
    auto nmyLoc = htask->location();
    findClosestGeyser(state, nmyLoc);
    checkEnemyRefineryBuilt(state, nmyLoc);
    htask->postCommand(state, this);
  }
}

void HarassModule::postCommand(State* state, BuildingHarassTask* htask) {
  // left blank here for now, should implment cancel / rebuild strategies
  // in case we are attacked
}

void HarassModule::postCommand(State* state, HarassTask* htask) {
  if (buildRefinery(state, htask)) {
    VLOG(3) << "building..."; // nothing to do at this stage
    return;
  }
  // beingAttackedByEnemies
  if (!htask->unitp()->beingAttackedByEnemies.empty()) {
    if (flee(state, htask)) {
      return;
    } else {
      VLOG(0) << "enemy attacked but no proper move found";
    }
  }
  if (attack(state, htask)) {
    VLOG(4) << "stop at attack";
    return;
  }
  if (exploreEnemyBase(state, htask)) {
    VLOG(4) << "exploring enemy base";
    return;
  }
}

bool HarassModule::buildRefinery(State* state, HarassTask* task) {
  if (task->building()) {
    return true;
  }
  auto nmyLoc = task->location();
  auto geyser = enemyGeyser(nmyLoc);
  if (!geyser || enemyRefinery(nmyLoc)) {
    return false;
  }
  if (!buildPolicy_.buildRefinery) {
    return false;
  }

  auto agent = task->unitp();
  // hardcoded 24
  if (utils::distance(agent, geyser) < 24) {
    VLOG(3) << "building extractor at " << Position(geyser->x, geyser->y);
    task->build(state, buildtypes::Zerg_Extractor, geyser);
    return true;
  }
  return false;
}

bool HarassModule::attack(State* state, HarassTask* htask) {
  if (targetPolicy_.attackWorkers && attackWorkers(state, htask)) {
    return true;
  }
  if (targetPolicy_.attackResourceDepot && attackResourceDepot(state, htask)) {
    return true;
  }
  return false;
}

bool HarassModule::attackResourceDepot(State* state, HarassTask* htask) {
  if (htask->attacking() && htask->targetUnit()->type->isResourceDepot) {
    return true;
  }
  auto unit = htask->unitp();
  auto visibleBuildings = unit->obstaclesInSightRange;
  for (auto bldg : visibleBuildings) {
    if (bldg->type->isResourceDepot &&
        !movefilters::dangerousAttack(unit, bldg)) {
      VLOG(3) << "attacking building " << utils::unitString(bldg);
      htask->attack(state, bldg);
      return true;
    }
  }
  return false;
}

bool HarassModule::attackWorkers(State* state, HarassTask* htask) {
  if (htask->attacking() && htask->targetUnit()->type->isWorker) {
    return true;
  }
  if (htask->attacking()) {
    VLOG(3) << "currently attacking " << utils::unitString(htask->targetUnit());
  }
  auto unit = htask->unitp();
  std::vector<Unit*>& visibleEnemies = unit->enemyUnitsInSightRange;
  auto area = state->areaInfo().getArea(unit->pos());
  if (area.id >= 0) {
    visibleEnemies = area.visibleUnits;
  }
  Unit* tgt = nullptr;
  float dist = kfInfty;
  for (auto nmy : visibleEnemies) {
    if (nmy->isEnemy && nmy->type->isWorker &&
        !movefilters::dangerousAttack(unit, nmy)) {
      auto d = utils::distance(nmy, unit);
      if (d < dist) {
        dist = d;
        tgt = nmy;
      }
    }
  }
  if (tgt) {
    VLOG(3) << "attacking worker " << utils::unitString(tgt);
    if (tgt->dead || !tgt->visible) {
      VLOG(3) << "attacking dead or invisible worker !?"
              << utils::unitString(tgt);
    }
    htask->attack(state, tgt);
    return true;
  }
  return false;
}

bool HarassModule::flee(State* state, HarassTask* task) {
  if (!task->shouldMove(state) && task->fleeing()) {
    VLOG(4) << "stop at shouldMove";
    return true;
  }

  auto unit = task->unitp();
  auto geyser = enemyGeyser(task->location());
  movefilters::PositionFilters posFilters;
  auto tgtPos = Position(-1, -1);
  if (geyser && !enemyRefinery(task->location()) &&
      fleePolicy_.turnAroundGeyser) {
    posFilters = movefilters::PositionFilters({movefilters::makePositionFilter(
        movefilters::getCloserTo(geyser),
        {movefilters::avoidAttackers(), movefilters::avoidThreatening()})});
    tgtPos = movefilters::smartMove(state, unit, posFilters);
    if (tgtPos.x > 0 && tgtPos.y > 0) {
      task->flee(state, tgtPos);
      return true;
    }
    VLOG(3) << "smartMove can't get closer to geyser";
  }
  auto choke = std::vector<Position>();
  posFilters = movefilters::PositionFilters(
      {movefilters::avoidEnemyUnitsInRange(unit->sightRange),
       movefilters::makePositionFilter(
           {movefilters::avoidAttackers(), movefilters::avoidThreatening()}),
       movefilters::avoidAttackers(),
       movefilters::fleeAttackers()});
  tgtPos = movefilters::smartMove(state, unit, posFilters);
  if (tgtPos.x <= 0 || tgtPos.y <= 0) {
    VLOG(1) << "harasser stuck, trying to go to the chokepoint"
            << " with " << unit->beingAttackedByEnemies.size() << " attackers";
    if (!choke.empty()) {
      tgtPos = choke[0];
    }
  }
  if (tgtPos.x > 0 && tgtPos.y > 0) {
    task->flee(state, tgtPos);
    VLOG(4) << "(frame " << state->currentFrame() << ") "
            << " pos " << Position(unit) << " target move is " << tgtPos;
    return true;
  }
  return false;
}

bool HarassModule::exploreEnemyBase(State* state, HarassTask* task) {
  // exploration strategy, should be made better
  auto nmyLoc = task->location();
  auto geyser = enemyGeyser(nmyLoc);
  if (geyser) {
    auto geyserPos = Position(geyser->x, geyser->y);
    if (geyserPos != task->targetPos() || task->shouldMove(state)) {
      task->move(state, geyserPos);
    }
    VLOG(4) << "stop at final Move "
            << " geyser position is " << geyserPos;
    return true;
  } else {
    // find mineral that is the further away
    auto dist = -kfInfty;
    Unit* mineral = nullptr;
    for (auto& bwem_res :
         state->map()->Minerals()) { // unitsInfo().resourceUnits()
      auto res = state->unitsInfo().getUnit(bwem_res->Unit()->getID());
      if (!res->type->isMinerals) {
        LOG_IF(ERROR, res->type != buildtypes::Zerg_Drone)
            << "bad type conversion between BWEM and buildtypes"
            << " current buildtype is " << bwem_res->Unit()->getType().c_str()
            << " gas is " << res->type->name;
        continue;
      }
      auto d = utils::distance(nmyLoc.x, nmyLoc.y, res->x, res->y);
      if (d <= 40 && d > dist) {
        dist = d;
        mineral = res;
      }
    }
    if (mineral) {
      task->move(state, Position(mineral));
      return true;
    }
  }
  if (nmyLoc != task->targetPos() || task->shouldMove(state)) {
    task->move(state, nmyLoc); // always valid
    VLOG(4) << "stop at goto nmyLoc";
    return true;
  } else if (!task->moving()) {
    LOG(ERROR) << "no possible action";
  }
  return false;
}

bool HarassModule::dangerousAttack(State* state, HarassTask* task) {
  return (
      task->attacking() &&
      movefilters::dangerousAttack(task->unitp(), task->targetUnit()));
}

void HarassModule::consumeUPC(
    State* state,
    UpcId upcId,
    std::shared_ptr<UPCTuple> upc) {
  auto board = state->board();
  board->consumeUPCs({upcId}, this);
  std::shared_ptr<MasterHarassTask> task_found = nullptr;
  auto loc = upc->position.get<Position>();
  for (auto task : board->tasksOfModule(this)) {
    auto htask = std::dynamic_pointer_cast<MasterHarassTask>(task);
    if (htask->location() == loc) {
      task_found = htask;
      break;
    }
  }
  // we create a single task per position for now
  if (!task_found) {
    auto task = createTask(state, upcId, upc);
    if (task) {
      board->postTask(task, this, true);
    }
  }
}

std::shared_ptr<MasterHarassTask> HarassModule::createTask(
    State* state,
    UpcId upcId,
    std::shared_ptr<UPCTuple> upc) {
  if (upc->unit.empty()) {
    LOG(ERROR) << "harass UPC without units";
  }
  auto const idUnitPair = *upc->unit.begin();
  if (idUnitPair.second <= 0) {
    LOG(WARNING) << "UPC with prob field <= 0";
    return nullptr; // no need to loop for now, this is a serious flaw in the
    // current code
  }
  auto loc = upc->position.get<Position>();
  if (idUnitPair.first->type->isWorker) {
    VLOG(1) << "task created " << upcId << " for location" << loc
            << " with worker unit " << utils::unitString(idUnitPair.first);
    return std::make_shared<HarassTask>(upcId, loc, idUnitPair.first, this);
  } else {
    if (!idUnitPair.first->type->isRefinery) {
      LOG(ERROR) << "trying to control of a non-worker, non-refinery unit"
                 << " aborting task";
      return nullptr;
    }
    VLOG(1) << "task created " << upcId << " for location" << loc
            << " with refinery unit " << utils::unitString(idUnitPair.first);
    return std::make_shared<BuildingHarassTask>(upcId, loc, idUnitPair.first);
  }
}

/*
 * helper functions, computes and accesses persistent data
 * Geysers and refineries around idenitified enemy locations
 */
Unit* HarassModule::enemyGeyser(Position const& pos) {
  auto p_geys = enemyGeyser_.find(pos);
  if (p_geys == enemyGeyser_.end()) {
    return nullptr;
  }
  return p_geys->second;
}

Unit* HarassModule::enemyRefinery(Position const& pos) {
  auto p_ref = enemyRefinery_.find(pos);
  if (p_ref == enemyRefinery_.end()) {
    return nullptr;
  }
  return p_ref->second;
}

std::vector<Position> HarassModule::getFleePositions(
    State* state,
    HarassTask* task) {
  // get the main directions for fleeing when targeted by the enmy
  auto agent = task->unitp();
  auto area =
      state->map()->GetNearestArea(BWAPI::WalkPosition(agent->x, agent->y));
  if (!area) { // we are in a map without clear areas, flee to our base ?
    if (state->areaInfo().foundMyStartLocation()) {
      return std::vector<Position>({state->areaInfo().myStartLocation()});
    } else { // no area, no start location, nowhere to flee
      return std::vector<Position>();
    }
  }
  auto fleePositions = std::vector<Position>();
  for (auto cp : area->ChokePoints()) {
    auto center = cp->Center();
    fleePositions.push_back(Position(center.x, center.y));
  }
  return fleePositions;
}

// find the geyser sufficiently close to enemy location in resourcesUnits
// never checks whether the resource is exhausted, not sure it would be useful
// can we have more than one geyser ?
void HarassModule::findClosestGeyser(State* state, Position const& nmyLoc) {
  if (enemyGeyser(nmyLoc)) {
    return;
  }
  auto dist = kfInfty;
  Unit* gas = nullptr;
  for (auto& bwem_res :
       state->map()->Geysers()) { // unitsInfo().resourceUnits()
    auto res = state->unitsInfo().getUnit(bwem_res->Unit()->getID());
    if (!res->type->isGas) {
      LOG_IF(ERROR, res->type != buildtypes::Zerg_Drone)
          << "bad type conversion between BWEM and buildtypes"
          << " current buildtype is " << bwem_res->Unit()->getType().c_str()
          << " gas is " << res->type->name;
      continue;
    }
    auto d = utils::distance(nmyLoc.x, nmyLoc.y, res->x, res->y);
    if (d < dist) {
      dist = d;
      gas = res;
    }
  }
  // harcoded distance of 100 for closest Geyser
  if (dist < 100) {
    VLOG(3) << "Geyser found at position" << Position(gas->x, gas->y);
    enemyGeyser_.emplace(nmyLoc, gas);
  } else {
    VLOG(3) << "Geyser not found near enemy"
            << " closest Geyser found is at distance " << dist;
    if (gas) {
      VLOG(3) << "HarassModule [...]: and position" << Position(gas->x, gas->y);
    }
  }
}

void HarassModule::checkEnemyRefineryBuilt(
    State* state,
    Position const& nmyLoc) {
  if (!enemyGeyser(nmyLoc)) {
    return;
  }
  auto nmyUnits = state->unitsInfo().enemyUnits();
  auto nmyRefinery = enemyRefinery(nmyLoc);
  if (nmyRefinery) {
    if (std::find(nmyUnits.begin(), nmyUnits.end(), nmyRefinery) !=
        nmyUnits.end()) {
      return;
    } else { // enemy refinery has been destroyed/cancelled
      VLOG(1) << "enemy refinery destroyed or cancelled";
      enemyRefinery_.erase(nmyLoc);
    }
  }
  // VLOG(3) << "here";
  auto geyser = enemyGeyser(nmyLoc);
  auto geyserPos = Position(geyser->x, geyser->y);
  for (auto unit : state->unitsInfo().enemyUnits()) {
    if (unit->type->isRefinery) {
      if (geyserPos.distanceTo(unit) == 0) {
        VLOG(1) << "enemy refinery found";
        enemyRefinery_.emplace(nmyLoc, unit);
        return;
      } else {
        VLOG(1) << "enemy refinery found but positions don't match"
                << " expected position" << geyserPos << " observed position"
                << Position(unit->x, unit->y) << " resource position"
                << Position(enemyGeyser(nmyLoc)->x, enemyGeyser(nmyLoc)->y);
      }
    }
  }
}

} // namespace cherrypi
