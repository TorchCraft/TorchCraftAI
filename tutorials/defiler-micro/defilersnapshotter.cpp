/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <glog/logging.h>

#include "defilersnapshotter.h"

#include "buildtype.h"

namespace microbattles {

bool DefilerSnapshotter::isCameraReady(torchcraft::State* state) {
  // We don't expect Defilers this early
  if (state->frame_from_bwapi < 7 * 60 * 24) {
    return false;
  }
  auto defilerDistanceMaxSquared = defilerDistanceMax_ * defilerDistanceMax_;

  auto defilerIsCloseEnough = [&]() {
    for (const auto& playerUnits : state->units) {
      for (const auto& unit : playerUnits.second) {
        // Is there a Defiler?
        if (unit.type == torchcraft::BW::UnitType::Zerg_Defiler &&
            unit.flags & torchcraft::replayer::Unit::Flags::Completed) {
          for (const auto& otherPlayerUnits : state->units) {
            // Look for enemy units near the Defiler
            if (playerUnits.first != otherPlayerUnits.first) {
              for (const auto& otherUnit : otherPlayerUnits.second) {
                int dx = unit.x - otherUnit.x;
                int dy = unit.y - otherUnit.y;
                int distanceSquared = dx * dx + dy * dy;
                bool isCombatUnit =
                    otherUnit.groundATK > 0 || otherUnit.airATK > 0;
                if (isCombatUnit &&
                    distanceSquared <= defilerDistanceMaxSquared) {
                  return true;
                }
              }
            }
          }
        }
      }
    }
    return false;
  };

  auto armiesAreEvenEnough = [&]() {
    auto armyValue = [](const auto& playerUnits) {
      double output = 0;
      for (const auto& unit : playerUnits) {
        auto type = cherrypi::getUnitBuildType(unit.type);
        bool isCombatUnit =
            !type->isWorker && (type->numGroundAttacks || type->numAirAttacks);
        if (isCombatUnit) {
          output += type->subjectiveValue;
        }
      }
      return output;
    };

    std::vector<double> armyValues = {};
    for (const auto& playerUnits : state->units) {
      auto playerId = playerUnits.first;
      if (playerId < 0) {
        continue;
      }
      while (armyValues.size() < size_t(playerId + 1)) {
        armyValues.emplace_back(0);
      }
      armyValues[playerId] = armyValue(playerUnits.second);
    }
    std::sort(armyValues.begin(), armyValues.end());
    std::reverse(armyValues.begin(), armyValues.end());
    if (armyValues.size() < 2) {
      return false;
    }
    if (armyValues[0] == 0 || armyValues[1] == 0) {
      return false;
    }

    double armyValueRatio = armyValues[0] / armyValues[1];
    armyValueRatio = std::max(armyValueRatio, 1.0 / armyValueRatio);
    return armyValueRatio <= armyValueRatioMax_;
  };

  return defilerIsCloseEnough() && armiesAreEvenEnough();
};

} // namespace microbattles