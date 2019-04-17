/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "modules/autobuild.h"
#include "modules/builder.h"
#include "modules/buildingplacer.h"
#include "modules/combat.h"
#include "modules/combatmicro.h"
#include "modules/creategatherattack.h"
#include "modules/gatherer.h"
#include "modules/genericautobuild.h"
#include "modules/harass.h"
#include "modules/once.h"
#include "modules/scouting.h"
#include "modules/squadcombat.h"
#include "modules/staticdefencefocusfire.h"
#include "modules/strategy.h"
#include "modules/tactics.h"
#include "modules/top.h"
#include "modules/upctocommand.h"

namespace cherrypi {
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
} // namespace cherrypi
