/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <array>
#include <vector>

namespace cherrypi {

struct BuildType;
struct Unit;

/**
 * Predicts the outcome of a hypothetical fight by simulating unit
 * movements/attacks.
 *
 * Uses a high speed/low precision approximation of Brood War mechanics,
 * ignoring elements like collisions, splash damage, accleration, turn rates,
 * attack animations, and spells.
 */
class CombatSim {
 public:
  struct SimUnit {
    int x = 0;
    int y = 0;
    double hp = 0.0;
    double shields = 0.0;
    int armor = 0;
    int maxSpeed = 0;
    bool flying = false;
    bool underDarkSwarm = false;
    const BuildType* type = nullptr;
    SimUnit* target = nullptr;
    bool targetInRange = false;

    int cooldownUntil = 0;
    int groundDamage = 0;
    int airDamage = 0;
    int groundDamageType = 0;
    int airDamageType = 0;
    int groundRange = 0;
    int airRange = 0;
  };

  struct Team {
    double startHp = 0.0;
    double endHp = 0.0;
    std::vector<SimUnit> units;
  };

  double speedMult = 1.0;
  std::array<Team, 2> teams;

  bool addUnit(Unit* u);

  void run(int frames);
};
} // namespace cherrypi
