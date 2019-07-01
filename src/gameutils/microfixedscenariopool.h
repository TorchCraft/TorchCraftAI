/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#pragma once

#include "initialconditions.h"
#include "state.h"
#include "utils.h"

namespace cherrypi {

/////////////////////////////
// Scenario-building tools //
/////////////////////////////
// We have a numbers advantage -- for GAS
ScenarioInfo customAdvantageScenario(const std::string& unit,
    const std::string& enemyUnit, int base, int advantage, bool varyStart, bool varyAngle,
    int separation = 0);
ScenarioInfo customHeterogenousScenario(int base, bool varyStart);
ScenarioInfo customGroupedScenario(const std::string& unit,
    int base, int additional, int advantage, bool varyStart);
ScenarioInfo customGroupedScenarioFar(const std::string& unit,
    int base, int additional, int advantage, bool varyStart);
ScenarioInfo customCheckerboardScenario(const std::string& unit, int num_units, int num_groups);
} // namespace cherrypi
