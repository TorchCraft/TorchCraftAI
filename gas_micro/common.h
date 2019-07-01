/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <tuple>

#include "cpid/metrics.h"
#include "player.h"
#include "state.h"

const std::string kStateKey = "state";
const std::string kActionKey = "action";
const std::string kPActionKey = "pAction";
const std::string kPastActionKey = "past_action";
const std::string kActionQKey = "actionQ";
const std::string kActionIdsKey = "action_ids";
const std::string kNumActionsKey = "num_actions";
const std::string kQKey = "Q";
const std::string kVKey = "V";
const std::string kHiddenKey = "hidden";
const std::string kOccupancyKey = "occupancy";
const std::string kLodKey = "lod";
const std::string kLodProbKey = "pLod";
const std::string kAllQKey = "allQ";
const std::string kAllDeltaKey = "allDelta";
const std::string kMapFeatsKey = "mapFeats";
const std::string kOurLocsKey = "ourLocs";
const std::string kOurTypeLocsKey = "ourTypeLoc";
const std::string kOurFeatsKey = "ourFeats";
const std::string kNmyLocsKey = "nmyLocs";
const std::string kNmyTypeLocsKey = "nmyTypeLocs";
const std::string kNmyFeatsKey = "nmyFeats";
const std::string kGrpAssignments = "grpAssignments";

namespace microbattles {

constexpr int kMapHeight = 256; // Note: hard-coded - maps should be this size
constexpr int kMapWidth = 256;

std::tuple<float, float, float, float> getUnitCountsHealth(
    cherrypi::State* state);
double getMovementRadius(cherrypi::Unit* u);
at::Device defaultDevice();
void research(
    cherrypi::BasePlayer* bot,
    int pid,
    torchcraft::BW::TechType tech,
    bool r = true);
} // namespace microbattles
