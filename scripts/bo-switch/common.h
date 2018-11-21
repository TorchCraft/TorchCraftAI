/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "botscenario.h"

#include "models/bandit.h"

#include <cpid/sampler.h>
#include <cpid/trainer.h>

namespace cherrypi {

class State;

// Top and bottom modules that are always present
char constexpr kAutoTopModule[] = "Top";
char constexpr kAutoBottomModule[] = "UPCToCommand";

/// The default set of modules.
/// This is what CherryPi uses in a normal game.
char constexpr kDefaultModules[] =
    "CreateGatherAttack,"
    "Strategy,"
    "GenericAutoBuild,"
    "BuildingPlacer,"
    "Builder,"
    "Tactics,"
    "SquadCombat,"
    "Scouting,"
    "Gatherer,"
    "Harass,"
    "StaticDefenceFocusFireModule";

std::unique_ptr<PlayScriptScenario> makeBosScenario(
    std::string const& maps,
    std::string const& opponents,
    std::string playOutputDir);

std::vector<std::string> mapPool(std::string const& mapDirOrFile);
size_t numBuilds(std::string const& builds);
std::string selectRandomBuild(
    std::string const& builds,
    std::string const& opponent);
std::string selectRandomOpponent(std::string const& opponents);

} // namespace cherrypi
