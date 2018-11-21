/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "combatsim.h"
#include "utils.h"

namespace {
double damageTypeModifier(int damageType, int unitSize) {
  if (damageType == 1) {
    if (unitSize == 1) {
      return 0.5;
    }
    if (unitSize == 2) {
      return 0.75;
    }
    if (unitSize == 3) {
      return 1.0;
    }
  }
  if (damageType == 2) {
    if (unitSize == 1) {
      return 1.0;
    }
    if (unitSize == 2) {
      return 0.5;
    }
    if (unitSize == 3) {
      return 0.25;
    }
  }
  return 1.0;
}
}

bool cherrypi::CombatSim::addUnit(Unit* u) {
  if (u->type->isNonUsable || !u->active()) {
    return false;
  }
  if (u->type->isBuilding && !u->type->hasAirWeapon &&
      !u->type->hasGroundWeapon && u->type != buildtypes::Terran_Bunker) {
    return false;
  }
  auto& team = u->isMine ? teams[0] : teams[1];
  team.units.emplace_back();
  auto& su = team.units.back();

  su.x = u->x << 8;
  su.y = u->y << 8;
  su.hp = u->unit.health;
  su.shields = u->unit.shield;
  if (u->isEnemy && u->visible && !u->detected()) {
    su.hp = u->type->maxHp;
    su.shields = u->type->maxShields;
  }
  su.armor = u->unit.armor;
  su.maxSpeed = u->topSpeed * 256 * speedMult;
  su.flying = u->flying();

  su.type = u->type;

  su.cooldownUntil = std::max(u->unit.groundCD, u->unit.airCD);
  su.groundDamage = u->unit.groundATK;
  su.airDamage = u->unit.airATK;
  su.groundDamageType = u->unit.groundDmgType;
  su.airDamageType = u->unit.airDmgType;
  su.groundRange = u->unit.groundRange;
  su.airRange = u->unit.airRange;

  if (u->type == buildtypes::Terran_Bunker) {
    su.groundDamage = 6;
    su.groundDamageType = 3;
    su.groundRange = 4 * 6;
    su.airDamage = 6;
    su.airDamageType = 3;
    su.airRange = 4 * 6;
  } else if (u->type == buildtypes::Protoss_Interceptor) {
    su.groundDamage = 6;
    su.groundDamageType = 0;
    su.groundRange = 4 * 4;
    su.airDamage = 6;
    su.airDamageType = 0;
    su.airRange = 4 * 4;
  } else if (u->type == buildtypes::Protoss_Reaver) {
    su.groundDamage = 100;
    su.groundDamageType = 0;
    su.groundRange = 4 * 8;
  }
  return true;
}

void cherrypi::CombatSim::run(int frames) {
  int frame = 0;
  const int resolution = 2;

  for (auto& t : teams) {
    double hp = 0.0;
    for (auto& u : t.units) {
      hp += u.hp + u.shields;
    }
    t.startHp = hp;
  }

  while (frame < frames) {
    bool idle = true;

    for (size_t i = 0; i != 2; ++i) {
      Team& team = teams[i];
      Team& enemyTeam = teams[i ^ 1];

      for (SimUnit& u : team.units) {
        if (u.hp <= 0.0) {
          continue;
        }
        SimUnit* target = u.target;
        if (target && target->hp <= 0.0) {
          target = nullptr;
        }
        target = utils::getBestScorePointer(
            enemyTeam.units,
            [&](SimUnit& e) {
              int damage =
                  e.hp > 0.0 ? e.flying ? u.airDamage : u.groundDamage : 0.0;
              if (damage == 0) {
                return kdInfty;
              }
              double range = e.flying ? u.airRange : u.groundRange;
              return std::max(
                         (double)utils::distance(u.x, u.y, e.x, e.y) -
                             range * 256,
                         0.0) *
                  100 +
                  (e.shields + e.hp - damage);
            },
            kdInfty);

        u.target = target;
        u.targetInRange = false;

        if (target) {
          int dx = target->x - u.x;
          int dy = target->y - u.y;
          int dxi = dx >> 8;
          int dyi = dy >> 8;
          if (!u.targetInRange) {
            int range = 4 + (target->flying ? u.airRange : u.groundRange);
            if (dxi * dxi + dyi * dyi <= range * range) {
              u.targetInRange = true;
            }
          }
          if (u.targetInRange) {
            if (frame >= u.cooldownUntil) {
              double damage = target->flying ? u.airDamage : u.groundDamage;
              if (target->shields) {
                target->shields -= damage;
                if (target->shields < 0.0) {
                  damage = -target->shields;
                  target->shields = 0.0;
                } else {
                  damage = 0.0;
                }
              }
              if (damage) {
                damage *= damageTypeModifier(
                    target->flying ? u.airDamageType : u.groundDamageType,
                    target->type->size);
                damage -= target->armor;
                if (damage < 0.5) {
                  damage = 0.5;
                }
                target->hp -= damage;
                if (target->hp < 0.0) {
                  target->hp = 0.0;
                }
              }
              int cooldown = target->flying ? u.type->airWeaponCooldown
                                            : u.type->groundWeaponCooldown;
              if (u.type == buildtypes::Terran_Bunker) {
                cooldown = 4;
              } else if (u.type == buildtypes::Protoss_Interceptor) {
                cooldown = 45;
              } else if (u.type == buildtypes::Protoss_Reaver) {
                cooldown = 60;
              } else if (u.type == buildtypes::Zerg_Scourge) {
                u.hp = 0.0;
              }
              u.cooldownUntil += cooldown;
              if (u.cooldownUntil <= frame) {
                u.cooldownUntil = frame + 1;
              }
            }
            idle = false;
          } else {
            int d = utils::pxdistance(0, 0, dxi, dyi);
            u.x += (dx * u.maxSpeed >> 8) / d * resolution;
            u.y += (dy * u.maxSpeed >> 8) / d * resolution;
            idle = false;
          }
        }
      }
    }
    frame += resolution;
    if (idle) {
      break;
    }
  }

  for (auto& t : teams) {
    double hp = 0.0;
    for (auto& u : t.units) {
      hp += u.hp + u.shields;
    }
    t.endHp = hp;
  }
}
