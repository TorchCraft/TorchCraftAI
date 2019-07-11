/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "staticdefencefocusfire.h"

#include "state.h"
#include "unitsinfo.h"
#include "utils.h"

#include <unordered_map>

namespace cherrypi {

REGISTER_SUBCLASS_0(Module, StaticDefenceFocusFireModule);

void StaticDefenceFocusFireModule::step(State* state) {
  std::unordered_map<Unit*, double> targetDamage;

  auto run = [&](Unit* u) {
    Unit* target = nullptr;
    double lowestHp = kdInfty;
    Unit* targetWithMedics = nullptr;
    double lowestHpWithMedics = kdInfty;
    bool anyMedics = false;
    for (Unit* e : state->unitsInfo().visibleEnemyUnits()) {
      if (utils::distance(u, e) < 4 * 12) {
        anyMedics |= e->type == buildtypes::Terran_Medic;
        if (e->inRangeOf(u)) {
          double hp = e->unit.shield + e->unit.health;
          hp -= targetDamage[e];
          if (hp > 0.0 && hp < lowestHp) {
            lowestHp = hp;
            target = e;
          }
          if (hp > -20.0 && hp < lowestHpWithMedics) {
            lowestHpWithMedics = hp;
            targetWithMedics = e;
          }
        }
      }
    }
    if (anyMedics) {
      target = targetWithMedics;
    }
    if (target) {
      targetDamage[target] += u->computeHPDamage(target);
      if (!u->unit.orders.empty()) {
        auto& o = u->unit.orders.front();
        if (o.type == tc::BW::Order::AttackUnit && o.targetId == target->id) {
          return;
        }
      }
      if (VLOG_IS_ON(2)) {
        utils::drawLine(
            state, Position(u), Position(target), tc::BW::Color::Red);
      }
      auto cmd = tc::Client::Command(
          tc::BW::Command::CommandUnit,
          u->id,
          tc::BW::UnitCommandType::Attack_Unit,
          target->id);
      state->board()->postCommand(cmd, kRootUpcId);
    }
  };

  for (Unit* u : state->unitsInfo().myCompletedUnitsOfType(
           buildtypes::Zerg_Sunken_Colony)) {
    run(u);
  }
  for (Unit* u : state->unitsInfo().myCompletedUnitsOfType(
           buildtypes::Zerg_Spore_Colony)) {
    run(u);
  }
  for (Unit* u : state->unitsInfo().myCompletedUnitsOfType(
           buildtypes::Protoss_Photon_Cannon)) {
    run(u);
  }
  for (Unit* u : state->unitsInfo().myCompletedUnitsOfType(
           buildtypes::Terran_Missile_Turret)) {
    run(u);
  }
}

} // namespace cherrypi
