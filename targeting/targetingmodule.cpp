/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 */

#include "targetingmodule.h"

#include "bwem/bwem.h"
#include "common/rand.h"
#include "modules/cherryvisdumper.h"
#include "parameters.h"
#include "state.h"
#include "utils.h"
#include <BWAPI/Color.h>

#include "featurize.h"
#include "flags.h"
#include "keys.h"

#include <cmath>
#include <glog/logging.h>
#include <iomanip>
#include <sstream>

using namespace common;
namespace cherrypi {

const int TargetingModule::kNumPairFeatures = 2;

namespace {
static void dealWithDetector(State* state, Unit* unit, int upcId) {
  int sight = unit->type->sightRange;
  utils::drawCircle(
      state, {unit->x, unit->y}, sight * tc::BW::XYPixelsPerWalktile, 255);
  state->board()->postCommand(
      tc::Client::Command(
          tc::BW::Command::CommandUnit,
          unit->id,
          tc::BW::UnitCommandType::Hold_Position),
      upcId);
}
static void dealWithMedic(State* state, Unit* unit, int upcId) {
  // we find the closest bio units and we move towards it.
  int x, y;
  float minDist = 1e8;
  for (const auto& u : state->unitsInfo().myUnits()) {
    if (u->type == buildtypes::Terran_Marine ||
        u->type == buildtypes::Terran_Ghost ||
        u->type == buildtypes::Terran_Firebat) {
      if (utils::distance(u, unit) < minDist &&
          u->unit.health < u->unit.max_health) {
        minDist = utils::distance(u, unit);
        x = u->x;
        y = u->y;
      }
      return;
    }
  }
  if (minDist < 10000) {
    if (minDist >= 1.9) {
      state->board()->postCommand(
          tc::Client::Command(
              tc::BW::Command::CommandUnit,
              unit->id,
              tc::BW::UnitCommandType::Move,
              -1,
              x,
              y),
          upcId);
    }
  } else {
    state->board()->postCommand(
        tc::Client::Command(
            tc::BW::Command::CommandUnit,
            unit->id,
            tc::BW::UnitCommandType::Hold_Position),
        upcId);
  }
}

static void dealWithLurker(State* state, Unit* unit, int x, int y, int upcId) {
  bool inRange =
      utils::distance(unit->x, unit->y, x, y) <= unit->unit.groundRange;
  if (inRange) {
    if (!unit->burrowed() &&
        !utils::isExecutingCommand(unit, tc::BW::UnitCommandType::Burrow)) {
      state->board()->postCommand(
          tc::Client::Command(
              tc::BW::Command::CommandUnit,
              unit->id,
              tc::BW::UnitCommandType::Burrow),
          upcId);
    }
  } else {
    if (unit->burrowed() &&
        !utils::isExecutingCommand(unit, tc::BW::UnitCommandType::Unburrow)) {
      state->board()->postCommand(
          tc::Client::Command(
              tc::BW::Command::CommandUnit,
              unit->id,
              tc::BW::UnitCommandType::Unburrow),
          upcId);
    } else {
      state->board()->postCommand(
          tc::Client::Command(
              tc::BW::Command::CommandUnit,
              unit->id,
              tc::BW::UnitCommandType::Move,
              -1,
              x,
              y),
          upcId);
    }
  }
}
} // namespace

void TargetingModule::onGameStart(State* state) {
  started_ = true;
  first_state_sent_ = false;
  aggregatedReward_ = 0.;
  sampling_hist_linear_.clear();
  sampling_hist_quad_.clear();
  prevEnemyHp_.clear();
  prevAllyHp_.clear();
  total_HP_begining_ = -1;
}

void TargetingModule::onGameEnd(State* state) {
  if (!started_) {
    return;
  }
  started_ = false;
  first_state_sent_ = false;
  aggregatedReward_ = 0.;
  prevEnemyHp_.clear();
  prevAllyHp_.clear();
  total_HP_begining_ = -1;
  reset();
}
void TargetingModule::step(State* state) {
  if (!started_)
    return;
  for (auto const& upct :
       state->board()->upcsWithCommand(Command::Delete, 0.5)) {
    state->board()->consumeUPCs({upct.first}, this);
    std::unordered_map<int, Unit*> allies, enemies;
    for (const auto& u : upct.second->unit) {
      allies[u.first->id] = u.first;
    }
    auto upc = upct.second;
    if (upc->position.is<UPCTuple::UnitMap>()) {
      for (auto& u : upc->position.get_unchecked<UPCTuple::UnitMap>()) {
        if (!u.first->detected()) {
          // we filter undetected units
          VLOG(2) << "Filtering enemy undetected " << u.first->id;
          continue;
        }
        enemies[u.first->id] = u.first;
        if (u.first == nullptr) {
          LOG(FATAL) << "got null enemy";
        }
      }
    }
    VLOG(2) << "Got " << allies.size() << " allies and " << enemies.size()
            << " enemies";
    double instantReward = computeReward(allies, enemies);
    aggregatedReward_ += instantReward;
    if (enemies.size() == 0) {
      continue;
    }
    if (state->currentFrame() >= lastFramePlayed_ + (int)FLAGS_frame_skip) {
      lastFramePlayed_ = state->currentFrame();
      switch (baseline_) {
        case Targeting::Even_Split:
          evenSplit_heuristic(allies, enemies);
          break;
        case Targeting::Weakest_Closest:
          wc_heuristic(allies, enemies);
          break;
        case Targeting::Weakest_Closest_NOK:
          wcnok_heuristic(allies, enemies, false, false);
          break;
        case Targeting::Weakest_Closest_NOK_NC:
          wcnok_heuristic(allies, enemies, true, false);
          break;
        case Targeting::Weakest_Closest_NOK_smart:
          wcnok_heuristic(allies, enemies, true, true);
          break;
        case Targeting::Noop:
          assignment_.clear();
          break;
        case Targeting::BuiltinAI:
          assignment_.clear();
          for (const auto& u : allies) {
            if (u.second->type->isDetector) {
              dealWithDetector(state, u.second, upct.first);
            }
            if (u.second->type == buildtypes::Terran_Medic) {
              dealWithMedic(state, u.second, upct.first);
            }
          }
          if (state->currentFrame() >= lastFrame + 60) {
            lastFrame = state->currentFrame();
            int meanX = 0;
            int meanY = 0;
            for (const auto& u : enemies) {
              meanX += u.second->x;
              meanY += u.second->y;
            }
            meanX /= enemies.size();
            meanY /= enemies.size();
            for (const auto& u : allies) {
              if (u.second->type->isDetector ||
                  u.second->type == buildtypes::Terran_Medic) {
                continue;
              }
              if (u.second->type == buildtypes::Zerg_Lurker) {
                dealWithLurker(state, u.second, meanX, meanY, upct.first);
              }
              state->board()->postCommand(
                  tc::Client::Command(
                      tc::BW::Command::CommandUnit,
                      u.second->id,
                      tc::BW::UnitCommandType::Attack_Move,
                      -1,
                      meanX,
                      meanY),
                  upct.first);
            }
          }
          return;
          break;
        case Targeting::Closest:
          closest_heuristic(allies, enemies);
          break;
        case Targeting::Random_NoChange:
          random_nochange_heuristic(allies, enemies);
          break;
        case Targeting::Trainer:
          play_with_model(state, allies, enemies);
          break;
        case Targeting::Random:
        default:
          random_heuristic(allies, enemies);
          break;
      }
    } else {
      VLOG(3) << "Rest frame";
      assignment_ = oldAssignment_;
    }
    if (baseline_ == Targeting::BuiltinAI)
      return;
    ;
    for (const auto& u : allies) {
      if (u.second->type->isDetector) {
        dealWithDetector(state, u.second, upct.first);
        continue;
      }
      if (u.second->type == buildtypes::Terran_Medic) {
        dealWithMedic(state, u.second, upct.first);
        continue;
      }
      if (assignment_.count(u.first) == 0) {
        // no assignment. There are two cases: either there is an enemy unit
        // it can attack, and in that case we give no order so that builtin AI
        // will pick it up, or there is no enemy attackable, in that case we
        // send hold_position to avoid feeing
        bool canAttack = false;
        for (const auto& v : enemies) {
          if (u.second->canAttack(v.second)) {
            canAttack = true;
            break;
          }
        }
        if (canAttack && baseline_ != Targeting::Noop) {
          // LOG(WARNING) << "Weird: not target found for unit " << u.second;
          state->board()->postCommand(
              tc::Client::Command(
                  tc::BW::Command::CommandUnit,
                  u.second->id,
                  tc::BW::UnitCommandType::Stop),
              upct.first);
        }
        if (!canAttack &&
            !utils::isExecutingCommand(
                u.second, tc::BW::UnitCommandType::Hold_Position)) {
          state->board()->postCommand(
              tc::Client::Command(
                  tc::BW::Command::CommandUnit,
                  u.second->id,
                  tc::BW::UnitCommandType::Hold_Position),
              upct.first);
        }
        continue;
      }
      if (enemies.count(assignment_.at(u.first)) == 0) {
        // LOG(WARNING) << "Got invalid enemy in assignment. Likely dead?";
        continue;
      }
      int cd = std::max(u.second->unit.groundCD, u.second->unit.airCD);

      double range = (u.second->flying()) ? u.second->unit.airRange
                                          : u.second->unit.groundRange;
      auto enemy = enemies.at(assignment_.at(u.first));
      double distance = utils::pxDistanceBB(u.second, enemy) /
          double(tc::BW::XYPixelsPerWalktile);
      VLOG(3) << u.first << " is targeting " << enemy << " CD=" << cd
              << " ennemy health=" << enemy->unit.health;
      VLOG(3) << "Stats !! range = " << range << " distance = " << distance
              << " Gap = " << distance - range << " INRANGE "
              << enemy->inRangeOf(u.second)
              << " canAttack=" << u.second->canAttack(enemy)
              << " canAttackBack=" << enemy->canAttack(u.second)
              << " HEALTH = " << u.second->unit.health + u.second->unit.shield
              << " SPEED " << u.second->topSpeed;

      int color = BWAPI::Colors::Yellow;
      // int color = BWAPI::Colors::Green;
      // if (range < distance && cd != 0) {
      //   color = BWAPI::Colors::Red;
      // } else {
      //   if (range < distance) {
      //     color = BWAPI::Colors::Yellow;
      //   } else {
      //     color = BWAPI::Colors::Purple;
      //   }
      // }
      utils::drawLine(
          state, {u.second->x, u.second->y}, {enemy->x, enemy->y}, color);
      // utils::drawCircle(
      //     state,
      //     {u.second->x, u.second->y},
      //     range * tc::BW::XYPixelsPerWalktile,
      //     (range < distance) ? (int)BWAPI::Colors::Red : 255);

      if (u.second->type->unit == tc::BW::UnitType::Zerg_Lurker) {
        dealWithLurker(state, u.second, enemy->x, enemy->y, upct.first);
      }

      if (oldAssignment_.count(u.first) == 0 ||
          oldAssignment_.at(u.first) != assignment_.at(u.first)) {
        VLOG(2) << "Different assignment, posting";
        postUpc(
            state, upct.first, u.second, enemies.at(assignment_.at(u.first)));
      }
    }
    oldAssignment_ = assignment_;
  }
} // namespace fairrsh

void TargetingModule::reset() {
  assignment_.clear();
  oldAssignment_.clear();
  lastFrame = -1000;
}

void TargetingModule::evenSplit_heuristic(
    const std::unordered_map<int, Unit*>& allies,
    const std::unordered_map<int, Unit*>& enemies) {
  assignment_.clear();
  std::unordered_map<int, int> attackCount;
  // if all allies can keep their target, we keep them
  bool canKeep = true;
  for (const auto& a : oldAssignment_) {
    if (enemies.count(a.second) > 0) {
      if (allies.count(a.first) > 0) {
        assignment_[a.first] = a.second;
        attackCount[a.second]++;
      }
    } else {
      canKeep = false;
      assignment_.clear();
    }
  }

  if (canKeep && assignment_.size() == allies.size())
    return;
  assignment_.clear();

  int assigned = 0;
  while (assigned < allies.size()) {
    // we select the enemy with the least attackers
    int best_enemy = (*(enemies.begin())).first;
    for (const auto& e : enemies) {
      if (attackCount[e.first] < attackCount[best_enemy]) {
        best_enemy = e.first;
      }
    }

    // select the closest non-assigned ally
    int best_ally = -1;
    float best_dist = 1e10;
    for (const auto& a : allies) {
      if (assignment_.count(a.first) > 0) {
        // already assigned
        continue;
      }
      float cur_dist = utils::distance(a.second, enemies.at(best_enemy));
      if (cur_dist < best_dist) {
        best_ally = a.first;
        best_dist = cur_dist;
      }
    }
    if (best_ally < 0) {
      LOG(FATAL) << "didn't manage to find attacker :(";
    }
    assigned++;
    assignment_[best_ally] = best_enemy;
    attackCount[best_enemy]++;
  }
}

static int find_wc_target(
    const std::unordered_map<int, Unit*>& allies,
    const std::unordered_map<int, Unit*>& enemies,
    bool forceGround,
    bool forceAir,
    const std::unordered_set<int>& taboo) {
  int meanPosX = 0, meanPosY = 0;
  for (const auto& u : allies) {
    meanPosX += u.second->x;
    meanPosY += u.second->y;
  }
  meanPosX /= allies.size();
  meanPosY /= allies.size();
  float minDistance = 1e9;
  int minHp = 1e7;
  int chosenId = -1;
  for (const auto& u : enemies) {
    if (taboo.count(u.first) > 0 || (forceGround && u.second->type->isFlyer) ||
        (forceAir && !u.second->type->isFlyer)) {
      continue;
    }
    int totalHp = u.second->unit.health + u.second->unit.shield;
    if (totalHp == minHp) {
      float distance =
          utils::distance(meanPosX, meanPosY, u.second->x, u.second->y);
      if (distance < minDistance) {
        minDistance = distance;
        minHp = totalHp;
        chosenId = u.first;
      }
    } else if (totalHp < minHp) {
      minHp = totalHp;
      minDistance =
          utils::distance(meanPosX, meanPosY, u.second->x, u.second->y);
      chosenId = u.first;
    }
  }
  return chosenId;
}

void TargetingModule::wc_heuristic(
    const std::unordered_map<int, Unit*>& allies,
    const std::unordered_map<int, Unit*>& enemies) {
  assignment_.clear();
  if (enemies.size() * allies.size() == 0) {
    return;
  }
  std::unordered_map<int, Unit*> unAssigned = allies;
  std::unordered_set<int> taboo;
  bool first = true;
  // weakest_closest heuristic may find a target that is not suitable for all
  // units (not all units may be able to attack it) To solve this, we do
  // several pass, sometimes forcing the heuristic to pick Air or Ground
  // targets We also maintain the list of units picked by previous passes as
  // taboo. One the list of taboos equals the list of enemies, we stop. It
  // means that there must be a problem with the attacking unit (maybe can't
  // see targets)
  while (!unAssigned.empty() && taboo.size() < enemies.size()) {
    bool forceAir = false, forceGround = false;
    if (!first) {
      //
      if ((*unAssigned.begin()).second->type->hasAirWeapon) {
        forceAir = true;
      } else {
        forceGround = true;
      }
    }
    auto chosenId =
        find_wc_target(unAssigned, enemies, forceGround, forceAir, taboo);
    taboo.insert(chosenId);
    if (chosenId == -1) {
      return;
    }
    std::unordered_map<int, Unit*> newUnAssigned;
    for (const auto& u : unAssigned) {
      if (u.second->canAttack(enemies.at(chosenId))) {
        assignment_[u.first] = chosenId;
      } else {
        if (u.second->type->hasGroundWeapon || u.second->type->hasAirWeapon) {
          newUnAssigned.insert(u);
        }
      }
    }
    std::swap(unAssigned, newUnAssigned);
    first = false;
  }
}

void TargetingModule::wcnok_heuristic(
    const std::unordered_map<int, Unit*>& allies,
    const std::unordered_map<int, Unit*>& enemies,
    bool nochange,
    bool smart) {
  assignment_.clear();
  if (enemies.size() * allies.size() == 0) {
    return;
  }
  std::unordered_map<int, Unit*> unAssigned = allies;
  std::unordered_set<int> taboo;
  std::unordered_map<int, int> damage_sum;
  for (const auto& e : enemies) {
    damage_sum[e.first] = 0;
  }

  auto compute_damage = [&](int a, int e) {
    int hpDmg = 0, shieldDmg = 0;
    if (allies.at(a)->canAttack(enemies.at(e))) {
      allies.at(a)->computeDamageTo(enemies.at(e), &hpDmg, &shieldDmg);
    }
    return hpDmg + shieldDmg;
  };

  // First step, if an ally has a previous target, and it's still valid, try to
  // keep it
  if (nochange) {
    for (const auto& a : oldAssignment_) {
      if (enemies.count(a.second) > 0 && allies.count(a.first) > 0) {
        VLOG(1) << "NOK: " << utils::unitString(allies.at(a.first))
                << " is considering keeping same target "
                << utils::unitString(enemies.at(a.second))
                << " current dammage " << damage_sum[a.second];
        int curHp = enemies.at(a.second)->unit.health +
            enemies.at(a.second)->unit.shield;
        if (!smart || damage_sum[a.second] < curHp) {
          int dmg = compute_damage(a.first, a.second);
          assignment_[a.first] = a.second;
          unAssigned.erase(a.first);
          damage_sum[a.second] += dmg;
          VLOG(1) << "NOK: " << utils::unitString(allies.at(a.first))
                  << " is actually keeping same target "
                  << utils::unitString(enemies.at(a.second));
        }
        if (damage_sum[a.second] >= curHp) {
          VLOG(1) << "NOK: " << utils::unitString(allies.at(a.first))
                  << " can't keep target. Tabooing "
                  << utils::unitString(enemies.at(a.second));
          taboo.insert(a.second);
        }
      }
    }
  }

  bool enforceNOK = true;
  // if we have too much power, forget about NOK
  if (taboo.size() == enemies.size()) {
    taboo.clear();
    enforceNOK = false;
  }
  bool first = true;
  // weakest_closest heuristic may find a target that is not suitable for all
  // units (not all units may be able to attack it) To solve this, we do
  // several pass, sometimes forcing the heuristic to pick Air or Ground
  // targets We also maintain the list of units picked by previous passes as
  // taboo. One the list of taboos equals the list of enemies, we stop. It
  // means that there must be a problem with the attacking unit (maybe can't
  // see targets)
  while (!unAssigned.empty() && taboo.size() < enemies.size()) {
    bool forceAir = false, forceGround = false;
    if (!first) {
      //
      if ((*unAssigned.begin()).second->type->hasAirWeapon) {
        forceAir = true;
      } else {
        forceGround = true;
      }
    }
    first = false;
    auto chosenId =
        find_wc_target(unAssigned, enemies, forceGround, forceAir, taboo);
    taboo.insert(chosenId);
    VLOG(1) << "NOK: Considered target is "
            << utils::unitString(enemies.at(chosenId));
    if (chosenId == -1) {
      return;
    }
    int curHp =
        enemies.at(chosenId)->unit.health + enemies.at(chosenId)->unit.shield;
    std::unordered_map<int, Unit*> newUnAssigned;
    for (const auto& u : unAssigned) {
      if ((!enforceNOK || damage_sum[chosenId] < curHp) &&
          u.second->canAttack(enemies.at(chosenId))) {
        VLOG(1) << "NOK: target " << utils::unitString(enemies.at(chosenId))
                << " affected to " << utils::unitString(u.second);
        assignment_[u.first] = chosenId;

        int dmg = compute_damage(u.first, chosenId);
        damage_sum[chosenId] += dmg;

      } else {
        VLOG(1) << "NOK: target " << utils::unitString(enemies.at(chosenId))
                << " is full";
        if (u.second->type->hasGroundWeapon || u.second->type->hasAirWeapon) {
          newUnAssigned.insert(u);
        }
      }
    }
    std::swap(unAssigned, newUnAssigned);
    if (taboo.size() == enemies.size()) {
      VLOG(1) << "NOK: resetting taboo and switching to OK";
      taboo.clear();
      enforceNOK = false;
      first = true;
    }
  }
}
void TargetingModule::closest_heuristic(
    const std::unordered_map<int, Unit*>& allies,
    const std::unordered_map<int, Unit*>& enemies) {
  assignment_.clear();
  if (enemies.size() * allies.size() == 0) {
    return;
  }
  for (const auto& u : allies) {
    Unit* closest = nullptr;
    float dist = 1e9;
    for (const auto& v : enemies) {
      if (!u.second->canAttack(v.second)) {
        continue;
      }
      float cur_dist = utils::distance(u.second, v.second);
      if (cur_dist < dist) {
        dist = cur_dist;
        closest = v.second;
      }
    }
    if (closest) {
      assignment_[u.first] = closest->id;
    }
  }
}

void TargetingModule::random_heuristic(
    const std::unordered_map<int, Unit*>& allies,
    const std::unordered_map<int, Unit*>& enemies) {
  std::uniform_int_distribution<int> distrib(0, enemies.size() - 1);
  assignment_.clear();
  for (const auto& u : allies) {
    int retries = 100;
    while (retries--) {
      int sample = Rand::sample(distrib);
      auto it = enemies.begin();
      std::advance(it, sample);
      if (u.second->canAttack(it->second)) {
        assignment_[u.first] = it->first;
        break;
      }
    }
  }
}

void TargetingModule::random_nochange_heuristic(
    const std::unordered_map<int, Unit*>& allies,
    const std::unordered_map<int, Unit*>& enemies) {
  // clean old assignment
  std::vector<UnitId> to_delete;
  for (const auto& a : assignment_) {
    if (allies.count(a.first) == 0 || enemies.count(a.second) == 0) {
      to_delete.push_back(a.first);
    }
  }
  for (const auto& id : to_delete) {
    assignment_.erase(id);
  }
  std::uniform_int_distribution<int> distrib(0, enemies.size() - 1);
  for (const auto& u : allies) {
    if (assignment_.count(u.first) != 0 &&
        u.second->canAttack(enemies.at(assignment_.at(u.first)))) {
      // already has a valid target
      continue;
    }
    int retries = 100;
    while (retries--) {
      int sample = Rand::sample(distrib);
      auto it = enemies.begin();
      std::advance(it, sample);
      if (u.second->canAttack(it->second)) {
        assignment_[u.first] = it->first;
        break;
      }
    }
  }
}

void TargetingModule::postUpc(
    State* state,
    int srcUpcId,
    Unit* source,
    Unit* target) {
  auto upc = std::make_shared<UPCTuple>();
  upc->unit[source] = 1.;
  UPCTuple::UnitMap map;
  map[target] = 1.;
  upc->position = std::move(map);
  upc->command[Command::Delete] = 1.; //.5;
  // upc->command[Command::Move] = .5;
  state->board()->postUPC(std::move(upc), srcUpcId, this);
}

void TargetingModule::postUpc(
    State* state,
    int srcUpcId,
    Unit* source,
    int x,
    int y) {
  auto upc = std::make_shared<UPCTuple>();
  upc->unit[source] = 1.;
  upc->position = Position{x, y};
  upc->command[Command::Delete] = 1.;
  state->board()->postUPC(std::move(upc), srcUpcId, this);
}

void TargetingModule::play_with_model(
    State* botstate,
    const std::unordered_map<int, Unit*>& allies,
    const std::unordered_map<int, Unit*>& enemies) {
  auto device = trainer_->model()->options().device();
  auto traceDumper = botstate->board()->getTraceDumper();
  if (first_state_sent_) {
    // we need to make a frame and pass it to the trainer
    auto frame = trainer_->makeFrame(
        std::move(last_model_out_), std::move(last_state_), aggregatedReward_);
    if (traceDumper) {
      traceDumper->dumpGameValue(botstate, "reward", aggregatedReward_);
    }
    aggregatedReward_ = 0.;
    trainer_->step(myHandle_, frame, false);
    first_state_sent_ = false;
  }

  // We find the center of the box

  // This function takes as input some sorted items given as a position on a
  // line, and returns the returns the pair that is at has a distance of at most
  // maxSpan that contains the emost items
  auto find_best_window = [](const std::vector<int>& items, int maxSpan) {
    int first = 0, last = 0;
    int best_span = 1;
    int cur_first = 0, cur_last = 0;
    while (cur_last < (int)items.size()) {
      while (cur_last < (int)items.size() &&
             items[cur_last] - items[cur_first] < maxSpan) {
        int cur_span = cur_last - cur_first + 1;
        if (cur_span > best_span) {
          first = cur_first, last = cur_last, best_span = cur_span;
        }
        cur_last++;
      }
      if (cur_last >= (int)items.size()) {
        break;
      }
      while (items[cur_last] - items[cur_first] >= maxSpan) {
        cur_first++;
      }
    }
    return std::pair<int, int>{items[first], items[last]};
  };
  std::vector<int> allX, allY;
  UnitsInfo::Units allies_vec, enemies_vec;
  for (const auto& u : allies) {
    allX.push_back(u.second->x);
    allY.push_back(u.second->y);
    allies_vec.push_back(u.second);
  }
  for (const auto& u : enemies) {
    allX.push_back(u.second->x);
    allY.push_back(u.second->y);
    enemies_vec.push_back(u.second);
  }
  auto sort_by_id = [](Unit* a, Unit* b) { return a->id < b->id; };
  std::sort(allies_vec.begin(), allies_vec.end(), sort_by_id);
  std::sort(enemies_vec.begin(), enemies_vec.end(), sort_by_id);

  std::sort(allX.begin(), allX.end());
  std::sort(allY.begin(), allY.end());

  std::pair<int, int> spanX = find_best_window(allX, FLAGS_map_dim);
  std::pair<int, int> spanY = find_best_window(allY, FLAGS_map_dim);

  Position center(0, 0);
  center.x = spanX.first + (spanX.second - spanX.first) / 2;
  center.y = spanY.first + (spanY.second - spanY.first) / 2;

  Rect box = Rect::centeredWithSize(center, FLAGS_map_dim, FLAGS_map_dim);
  box.x = std::max(0, box.x);
  box.y = std::max(0, box.y);
  VLOG(4) << "BOX " << box.x << " " << box.y << " " << box.w << " " << box.h
          << std::endl;
  // utils::drawBox(botstate, box);

  auto jitter = std::make_shared<Jitter>(botstate, box, false);

  auto feat = std::make_shared<SimpleUnitFeaturizer>();
  feat->jitter = jitter;

  auto allyFeat = feat->extract(botstate, allies_vec, box);
  auto enemyFeat = feat->extract(botstate, enemies_vec, box);

  auto allySpatial = feat->toSpatialFeature(allyFeat, SubsampleMethod::Sum);
  auto enemySpatial = feat->toSpatialFeature(enemyFeat, SubsampleMethod::Sum);

  torch::Tensor state =
      torch::cat({allySpatial.tensor, enemySpatial.tensor}, 0);

  // we filter units that were not featurized
  std::unordered_map<int, Unit*> removed_allies;
  for (Unit* u : allies_vec) {
    if ((*jitter.get())(u) == kInvalidPosition) {
      removed_allies[u->id] = u;
    }
  }

  auto it = std::remove_if(allies_vec.begin(), allies_vec.end(), [&](auto& u) {
    return (*jitter.get())(u) == kInvalidPosition;
  });
  allies_vec.erase(it, allies_vec.end());

  it = std::remove_if(enemies_vec.begin(), enemies_vec.end(), [&](auto& u) {
    return (*jitter.get())(u) == kInvalidPosition;
  });
  enemies_vec.erase(it, enemies_vec.end());

  // if we don't have any allies or enemies left, it means that the two
  // groups are too far apart.
  if (enemies_vec.size() == 0) {
    // default to closest
    LOG(INFO) << "No enemy, defaulting to closest";
    return closest_heuristic(allies, enemies);
  }
  if (removed_allies.size() > 0) {
    // pick target for allies outside our featurization box
    closest_heuristic(removed_allies, enemies);
  }
  if (allies_vec.size() == 0) {
    return;
  }

  // we need to compute the sampling history for all pairs
  torch::Tensor hist_linear =
      torch::zeros({(int)allies_vec.size(), (int)enemies_vec.size()});
  auto hist = hist_linear.accessor<float, 2>();
  for (size_t i = 0; i < allies_vec.size(); ++i) {
    int ida = allies_vec[i]->id;
    for (size_t j = 0; j < enemies_vec.size(); ++j) {
      int ide = enemies_vec[j]->id;
      if (sampling_hist_linear_.count(ida) > 0) {
        if (sampling_hist_linear_[ida].count(ide) > 0) {
          auto& curhist = sampling_hist_linear_[ida][ide];
          hist[i][j] = std::accumulate(curhist.begin(), curhist.end(), 0.);
          // LOG(INFO) << "found history of size " << curhist.size() << "
          // "
          //           << hist[i][j];
        }
      }
    }
  }
  hist_linear = hist_linear.view({-1});
  torch::Tensor sampling_hist = hist_linear.to(device);

  if (isModelQuad(model_type_)) {
    torch::Tensor hist_quad =
        torch::zeros({(int)enemies_vec.size(), (int)enemies_vec.size()});
    auto hist = hist_quad.accessor<float, 2>();
    for (size_t i = 0; i < enemies_vec.size(); ++i) {
      int ide1 = enemies_vec[i]->id;
      for (size_t j = 0; j < enemies_vec.size(); ++j) {
        int ide2 = enemies_vec[j]->id;
        if (sampling_hist_quad_.count(ide1) > 0) {
          if (sampling_hist_quad_[ide1].count(ide2) > 0) {
            auto& curhist = sampling_hist_quad_[ide1][ide2];
            hist[i][j] = std::accumulate(curhist.begin(), curhist.end(), 0.);
          }
        }
      }
    }
    hist_quad = hist_quad.to(device).view({-1});
    sampling_hist = at::cat({sampling_hist, hist_quad});
  }

  last_state_ = ag::VariantDict{
      {keys::kAllyData, ag::Variant(allyFeat.data.to(device))},
      {keys::kAllyPos, ag::Variant(allyFeat.positions.to(device))},
      {keys::kEnemyData, ag::Variant(enemyFeat.data.to(device))},
      {keys::kEnemyPos, ag::Variant(enemyFeat.positions.to(device))},
      {keys::kState, ag::Variant(state.to(device))},
      {keys::kSamplingHist, ag::Variant(sampling_hist)}};

  if (FLAGS_use_pairwise_feats) {
    torch::Tensor pair_feats = torch::zeros(
        {(int)allies_vec.size(), (int)enemies_vec.size(), kNumPairFeatures});
    for (size_t i = 0; i < allies_vec.size(); ++i) {
      int ida = allies_vec[i]->id;
      int old_target = -1;
      if (oldAssignment_.count(ida) > 0) {
        old_target = oldAssignment_[ida];
      }
      for (size_t j = 0; j < enemies_vec.size(); ++j) {
        int ide = enemies_vec[j]->id;
        pair_feats[i][j][0] = utils::distance(allies_vec[i], enemies_vec[j]);
        if (FLAGS_normalize_dist) {
          pair_feats[i][j][0] /= 25.;
        }
        if (ide == old_target) {
          pair_feats[i][j][1] = 1.;
        }
      }
    }
    last_state_[keys::kPairsData] =
        pair_feats.to(device).view({-1, kNumPairFeatures});
  }
  first_state_sent_ = true;
  last_model_out_ = trainer_->forward(last_state_, myHandle_);
  last_model_out_ = trainer_->sample(last_model_out_);

  torch::Tensor value = last_model_out_[keys::kValueKey];
  if (traceDumper) {
    traceDumper->dumpGameValue(botstate, "V", value.item<float>());
  }
  assignment_.clear();

  torch::Tensor actions = last_model_out_[keys::kActionKey].view(-1);
  torch::Tensor pi = last_model_out_[keys::kPiPlayKey].view(-1);

  torch::Tensor pi_lin =
      pi.slice(0, 0, long(allies_vec.size() * enemies_vec.size()));
  pi_lin = pi_lin.view({long(allies_vec.size()), long(enemies_vec.size())});

  torch::Tensor actions_lin =
      actions.slice(0, 0, long(allies_vec.size() * enemies_vec.size()));
  actions_lin =
      actions_lin.view({long(allies_vec.size()), long(enemies_vec.size())});
  // LOG(INFO) << "policy" << pi;
  torch::Tensor alpha_lin = (actions_lin - pi_lin);

  actions_lin = actions_lin.to(at::kCPU);

  alpha_lin = alpha_lin.to(at::kCPU).view(
      {(long)allies_vec.size(), (long)enemies_vec.size()});

  if (traceDumper) {
    auto tmp_pi = actions_lin.to(torch::kCPU);

    std::unordered_map<std::string, ag::Variant> maps;
    auto acc = tmp_pi.accessor<float, 2>();
    for (size_t i = 0; i < allies_vec.size(); ++i) {
      torch::Tensor hm = torch::zeros(
          {botstate->map()->WalkSize().y, botstate->map()->WalkSize().x});
      auto hm_acc = hm.accessor<float, 2>();
      for (size_t j = 0; j < enemies_vec.size(); ++j) {
        hm_acc[enemies_vec[j]->y][enemies_vec[j]->x] = acc[i][j];
      }
      maps[std::to_string(allies_vec[i]->id)] = hm;
    }
    traceDumper->dumpTerrainHeatmaps(
        botstate,
        maps,
        /* topLeftPixel */
        {0, 0},
        /* scalingToPixels */
        {tc::BW::XYPixelsPerWalktile, tc::BW::XYPixelsPerWalktile});
  }

  // we need to update the sampling histories
  auto alphaA = alpha_lin.accessor<float, 2>();
  for (size_t i = 0; i < allies_vec.size(); ++i) {
    int ida = allies_vec[i]->id;
    for (size_t j = 0; j < enemies_vec.size(); ++j) {
      int ide = enemies_vec[j]->id;
      sampling_hist_linear_[ida][ide].push_back(alphaA[i][j]);
      if (sampling_hist_linear_[ida][ide].size() >
          Parameters::get_int("correlated_steps")) {
        sampling_hist_linear_[ida][ide].pop_front();
      }
    }
  }
  torch::Tensor actions_quad;
  if (isModelQuad(model_type_)) {
    torch::Tensor pi_quad =
        pi.view(-1)
            .slice(
                0,
                long(allies_vec.size() * enemies_vec.size()),
                long(
                    allies_vec.size() * enemies_vec.size() +
                    enemies_vec.size() * enemies_vec.size()))
            .view({(long)enemies_vec.size(), (long)enemies_vec.size()});

    actions_quad =
        actions.view(-1)
            .slice(
                0,
                long(allies_vec.size() * enemies_vec.size()),
                long(
                    allies_vec.size() * enemies_vec.size() +
                    enemies_vec.size() * enemies_vec.size()))
            .view({(long)enemies_vec.size(), (long)enemies_vec.size()});

    torch::Tensor alpha_quad = (actions_quad - pi_quad);

    actions_quad = actions_quad.to(at::kCPU);

    alpha_quad = alpha_quad.to(at::kCPU).view(
        {(long)enemies.size(), (long)enemies.size()});

    auto alphaA = alpha_quad.accessor<float, 2>();
    for (size_t i = 0; i < enemies_vec.size(); ++i) {
      int ide1 = enemies_vec[i]->id;
      for (size_t j = 0; j < enemies_vec.size(); ++j) {
        int ide2 = enemies_vec[j]->id;
        sampling_hist_quad_[ide1][ide2].push_back(alphaA[i][j]);
        if (sampling_hist_quad_[ide1][ide2].size() >
            Parameters::get_int("correlated_steps")) {
          sampling_hist_quad_[ide1][ide2].pop_front();
        }
      }
    }
  }

  // we retrieve the actions
  switch (model_type_) {
    case ModelType::Argmax_DM:
    case ModelType::Argmax_PEM:
      play_argmax(botstate, allies_vec, enemies_vec, actions_lin);
      break;
    case ModelType::LP_DM:
    case ModelType::LP_PEM:
      play_lp(botstate, allies_vec, enemies_vec, actions_lin);
      break;
    case ModelType::Quad_DM:
    case ModelType::Quad_PEM:
      play_quad(botstate, allies_vec, enemies_vec, actions_lin, actions_quad);
      break;
    default:
      LOG(FATAL) << "Model not implemented :(";
  }
}

void TargetingModule::play_argmax(
    State* state,
    const UnitsInfo::Units& allies,
    const UnitsInfo::Units& enemies,
    torch::Tensor actions) {
  auto act = actions.accessor<float, 2>();
  for (size_t i = 0; i < allies.size(); ++i) {
    // find argmax for current ally
    double best_val = -1e8;
    int best_targ = -1;
    for (size_t j = 0; j < enemies.size(); ++j) {
      if (act[i][j] > best_val) {
        best_val = act[i][j];
        best_targ = j;
      }
    }
    if (best_targ != -1 && best_val >= 0) {
      assignment_[allies[i]->id] = enemies[best_targ]->id;
    }
  }
}

std::pair<std::vector<std::vector<double>>, std::vector<double>>
TargetingModule::computeContribAndCapa(
    State* state,
    const UnitsInfo::Units& allies,
    const UnitsInfo::Units& enemies) {
  std::vector<std::vector<double>> contribMatrix(
      allies.size(), std::vector<double>(enemies.size(), 0));

  std::vector<std::pair<double, double>> minMaxDamage(
      enemies.size(), {1e100, -1e100});
  std::unordered_map<UnitId, std::unordered_set<UnitId>> old_reverse_assignment;

  std::unordered_map<UnitId, int> ally_indices;

  std::unordered_set<UnitId> all_enemies;
  for (size_t i = 0; i < enemies.size(); ++i) {
    all_enemies.insert(enemies[i]->id);
  }

  for (size_t i = 0; i < allies.size(); ++i) {
    ally_indices[allies[i]->id] = i;
    for (size_t j = 0; j < enemies.size(); ++j) {
      int hpDmg = 0, shieldDmg = 0;
      if (allies[i]->canAttack(enemies[j])) {
        allies[i]->computeDamageTo(enemies[j], &hpDmg, &shieldDmg);
      }
      contribMatrix[i][j] = hpDmg + shieldDmg;
      minMaxDamage[j].first =
          std::min(minMaxDamage[j].first, contribMatrix[i][j]);
      minMaxDamage[j].second =
          std::max(minMaxDamage[j].second, contribMatrix[i][j]);
    }
    if (oldAssignment_.count(allies[i]->id) > 0) {
      UnitId old_target = oldAssignment_.at(allies[i]->id);
      if (all_enemies.count(old_target) > 0) {
        old_reverse_assignment[old_target].insert(allies[i]->id);
      }
    }
  }

  std::vector<double> capacities(enemies.size(), 0);
  for (size_t j = 0; j < enemies.size(); ++j) {
    int curHp = enemies[j]->unit.health + enemies[j]->unit.shield;
    double cur_damage = 0;
    if (old_reverse_assignment.count(enemies[j]->id) > 0) {
      for (const UnitId& ally : old_reverse_assignment.at(enemies[j]->id)) {
        cur_damage += contribMatrix[ally_indices[ally]][j];
        if (cur_damage >= curHp) {
          break;
        }
      }
    }
    if (cur_damage >= curHp) {
      // amongst the ally targeting the enemy, we already have enough fire
      // power to kill it
      // TODO: possibly, we could select the optimal subset of the current
      // allies that we want to keep, based on CD (we want to keep the lowest
      // CD in priority) and damage (we want to minimize overkill)
      VLOG(2) << "CAPA: Old attackers of " << utils::unitString(enemies[j])
              << " can deal at least " << cur_damage
              << ", this is greater than the current HP of " << curHp;
      capacities[j] = cur_damage;
    } else {
      // very simple heuristic: we assume that each unit is going to deal the
      // minimum amount of unit, and compute the minimal amount of damage that
      // needs to be dealt to kill the enemy. This works well when the allies
      // that can target the enemy are homogenous, but can backfire if they
      // deal different amount of damage.
      // TODO: to improve this, we could compute the assignment as a packing
      // that minimizes the overkill, while still killing the target (if
      // possible). The pitfalls are 1) it must be fast and 2) we might want
      // to take distance into account to avoid finding a packing that is
      // unlikely because it involves a very distant ally, and that disallow
      // more likely packing in terms of range
      int nbHits = ceil(double(curHp) / double(minMaxDamage[j].first));
      VLOG(2) << "CAPA: enemy " << utils::unitString(enemies[j]) << " has "
              << curHp << " HP, assigning " << nbHits << " hits of "
              << minMaxDamage[j].first << " dmg";
      capacities[j] = nbHits * minMaxDamage[j].first;
    }
    /*
    LOG(INFO) << "HP of " << utils::unitString(enemies[j]) << " " << curHp
              << " capacity " << capacities[j];
    */
  }
  return {std::move(contribMatrix), std::move(capacities)};
}

void TargetingModule::applyAssignment(
    State* state,
    const UnitsInfo::Units& allies,
    const UnitsInfo::Units& enemies,
    const std::vector<std::vector<double>>& contribMatrix,
    std::vector<double> remaining_capa,
    const Assign& assign) {
  std::unordered_set<size_t> unassigned;
  for (size_t i = 0; i < allies.size(); ++i) {
    if (assign[i].second < 0.1) {
      unassigned.insert(i);
    } else {
      int target = assign[i].first;
      assignment_[allies[i]->id] = enemies[target]->id;
      remaining_capa[target] -= contribMatrix[i][target];
    }
  }
  for (size_t i : unassigned) {
    // this is a non assigned target. If this is because we are
    // overpowered, the contraints prevented it from being affected,
    // hence, it makes sense to pick a target anyway rather than letting it go
    // to waste. We check whether there exists a target it could have been
    // affected to.
    bool canAttack = false;
    for (size_t j = 0; j < enemies.size(); ++j) {
      if (contribMatrix[i][j] > 0 && remaining_capa[j] >= contribMatrix[i][j]) {
        canAttack = true;
        break;
      }
    }

    if (canAttack) {
      // the unit could have attack, so it's the model choice to have it do
      // nothing. We skip it.
      continue;
    }

    VLOG(2) << "ASSIGN: ally " << utils::unitString(allies[i])
            << " doesn't have a target (score was " << assign[i].second
            << ") Going to pick one";

    if (oldAssignment_.count(allies[i]->id) > 0) {
      UnitId old_target = oldAssignment_[allies[i]->id];
      bool found = false;
      for (size_t j = 0; j < enemies.size() && !found; ++j) {
        if (enemies[j]->id == old_target) {
          found = true;
          VLOG(2) << "ASSIGN: assigning to old target "
                  << utils::unitString(enemies[j]);
          assignment_[allies[i]->id] = old_target;
        }
      }
      if (found) {
        continue;
      }
    }
    UnitId choosen_id = -1;
    int j_best = -1;
    double best_dist = 1e9;
    for (size_t j = 0; j < enemies.size(); ++j) {
      float cur_dist = utils::distance(allies[i], enemies[j]);
      if (allies[i]->canAttack(enemies[j]) && cur_dist < best_dist) {
        best_dist = cur_dist;
        choosen_id = enemies[j]->id;
        j_best = j;
      }
    }
    if (choosen_id != -1) {
      VLOG(2) << "ASSIGN: assigning to closest attackable target "
              << utils::unitString(enemies[j_best]);
      assignment_[allies[i]->id] = choosen_id;
    } else {
      // nothing found
      VLOG(2) << "ASSIGN: No target found, defaulting to model's decision ";
      assignment_[allies[i]->id] = enemies[assign[i].first]->id;
    }
  }
}

void TargetingModule::play_discrete(
    State* state,
    const UnitsInfo::Units& allies,
    const UnitsInfo::Units& enemies,
    torch::Tensor actions) {
  if (enemies.size() == 0) {
    return;
  }

  assignment_.clear();
  actions = actions.to(torch::kLong).view({-1});
  auto act = actions.accessor<long, 1>();
  for (size_t i = 0; i < allies.size(); ++i) {
    if (act[i] < enemies.size()) {
      assignment_[allies[i]->id] = enemies[act[i]]->id;
    }
  }
}

void TargetingModule::play_lp(
    State* state,
    const UnitsInfo::Units& allies,
    const UnitsInfo::Units& enemies,
    torch::Tensor actions) {
  // LOG(INFO) << "playing actions " << actions;
  if (enemies.size() == 1) {
    for (size_t i = 0; i < allies.size(); ++i) {
      assignment_[allies[i]->id] = enemies[0]->id;
    }
    return;
  }
  if (enemies.size() == 0) {
    return;
  }
  std::vector<std::vector<double>> affinityMatrix(
      allies.size(), std::vector<double>(enemies.size(), 0));
  auto act = actions.accessor<float, 2>();
  for (size_t i = 0; i < allies.size(); ++i) {
    for (size_t j = 0; j < enemies.size(); ++j) {
      affinityMatrix[i][j] = act[i][j];
      // affinityMatrix[i][j] = 1000 - utils::distance(allies[i], enemies[j]);
    }
  }
  std::vector<std::vector<double>> contribMatrix;
  std::vector<double> capacities;
  std::tie(contribMatrix, capacities) =
      computeContribAndCapa(state, allies, enemies);
  auto res =
      solveLinearWithLP(affinityMatrix, contribMatrix, capacities).second;

  applyAssignment(state, allies, enemies, contribMatrix, capacities, res);
}

void TargetingModule::play_quad(
    State* state,
    const UnitsInfo::Units& allies,
    const UnitsInfo::Units& enemies,
    torch::Tensor actions_lin,
    torch::Tensor actions_quad) {
  if (enemies.size() == 1) {
    for (size_t i = 0; i < allies.size(); ++i) {
      assignment_[allies[i]->id] = enemies[0]->id;
    }
    return;
  }
  if (enemies.size() == 0) {
    return;
  }
  std::vector<std::vector<double>> affinityMatrix(
      allies.size(), std::vector<double>(enemies.size(), 0));
  std::vector<std::vector<double>> crossCost(
      enemies.size(), std::vector<double>(enemies.size(), 0));
  auto act = actions_lin.accessor<float, 2>();
  for (size_t i = 0; i < allies.size(); ++i) {
    for (size_t j = 0; j < enemies.size(); ++j) {
      affinityMatrix[i][j] = act[i][j];
      // affinityMatrix[i][j] = 1000 - utils::distance(allies[i], enemies[j]);
    }
  }
  auto quad = actions_quad.accessor<float, 2>();
  for (size_t i = 0; i < enemies.size(); ++i) {
    for (size_t j = 0; j < enemies.size(); ++j) {
      crossCost[i][j] = quad[i][j];
      /*if (i == j) {
        crossCost[i][j] =
            10000. * (enemies[i]->unit.shield + enemies[i]->unit.health - 50);
      } else {
        crossCost[i][j] = 0;
      }
      */
    }
  }
  std::vector<std::vector<double>> contribMatrix;
  std::vector<double> capacities;
  std::tie(contribMatrix, capacities) =
      computeContribAndCapa(state, allies, enemies);
  auto res = solveQuad(affinityMatrix, crossCost, contribMatrix, capacities);
  applyAssignment(state, allies, enemies, contribMatrix, capacities, res);
}

void TargetingModule::sendLastFrame(State* state) {
  if (trainer_) {
    if (first_state_sent_) {
      // TODO: better select allies and enemies
      double deltaHP = 0;
      for (const auto& u : state->unitsInfo().myUnits()) {
        deltaHP += u->unit.health + u->unit.shield;
      }
      for (const auto& u : state->unitsInfo().enemyUnits()) {
        deltaHP -= u->unit.health + u->unit.shield;
      }
      deltaHP /= double(std::max(1, total_HP_begining_));

      auto frame = trainer_->makeFrame(
          std::move(last_model_out_),
          std::move(last_state_),
          aggregatedReward_ + deltaHP);
      aggregatedReward_ = 0.;
      trainer_->step(myHandle_, frame, true);

    } else {
      LOG(ERROR) << "ERROR: trying to send last frame but no forward has been "
                    "done so far";
    }
  }
}

float TargetingModule::computeReward(
    const std::unordered_map<int, Unit*>& allies,
    const std::unordered_map<int, Unit*>& enemies) {
  int prevAllyHpTotal = 0;
  int currAllyHpTotal = 0;
  for (auto const& ally : allies) {
    auto hp = ally.second->unit.health + ally.second->unit.shield;
    int id = ally.second->id;

    // Check whether unit existed before in same form, if so include in reward
    // calc
    if (prevAllyHp_.count(id) > 0 &&
        prevAllyHp_[id].first == utils::unitString(ally.second)) {
      prevAllyHpTotal += prevAllyHp_[id].second;
      currAllyHpTotal += hp;
    }

    // Update unit hp and add to current enemy list
    prevAllyHp_[id] = std::make_pair(utils::unitString(ally.second), hp);
  }

  // Don't want to include newly created units in reward calculation
  int prevEnemyHpTotal = 0;
  int currEnemyHpTotal = 0;
  for (auto const& enemy : enemies) {
    auto hp = enemy.second->unit.health + enemy.second->unit.shield;
    int id = enemy.second->id;

    // Check whether unit existed before in same form, if so include in reward
    // calc
    if (prevEnemyHp_.count(id) > 0 &&
        prevEnemyHp_[id].first == utils::unitString(enemy.second)) {
      prevEnemyHpTotal += prevEnemyHp_[id].second;
      currEnemyHpTotal += hp;
    }

    // Update unit hp and add to current enemy list
    prevEnemyHp_[id] = std::make_pair(utils::unitString(enemy.second), hp);
  }

  if (total_HP_begining_ == -1) {
    total_HP_begining_ = 0;
    for (auto const& ally : allies) {
      auto hp = ally.second->unit.health + ally.second->unit.shield;
      total_HP_begining_ += hp;
    }
    for (auto const& enemy : allies) {
      auto hp = enemy.second->unit.health + enemy.second->unit.shield;
      total_HP_begining_ += hp;
    }
  }

  std::vector<int> to_delete;
  for (const auto& prevEnemyIt : prevEnemyHp_) {
    int id = prevEnemyIt.first;
    if (enemies.count(id) == 0) {
      // If enemy isn't in current state, remove from prevEnemy list
      to_delete.push_back(id);
      prevEnemyHpTotal += prevEnemyIt.second.second;
    }
  }
  for (int id : to_delete) {
    prevEnemyHp_.erase(id);
  }
  to_delete.clear();

  for (const auto& prevAllyIt : prevAllyHp_) {
    int id = prevAllyIt.first;
    if (allies.count(id) == 0) {
      // If ally isn't in current state, remove from prevAlly list
      to_delete.push_back(id);
      prevAllyHpTotal += prevAllyIt.second.second;
    }
  }
  for (int id : to_delete) {
    prevAllyHp_.erase(id);
  }

  float reward = std::min(currAllyHpTotal - prevAllyHpTotal, 0) +
      std::max(prevEnemyHpTotal - currEnemyHpTotal, 0);
  // float scaling = std::max(1, prevAllyHpTotal + prevEnemyHpTotal);
  float scaling = total_HP_begining_;
  reward /= scaling;

  return reward;
}

} // namespace cherrypi
