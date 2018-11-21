/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "buildtype.h"
#include "cherrypi.h"

using namespace cherrypi;

CASE("buildtypes/race_sanity_check") {
  EXPECT(buildtypes::Protoss_Probe->race == tc::BW::Race::Protoss);
  EXPECT(buildtypes::Protoss_Dragoon->race == tc::BW::Race::Protoss);
  EXPECT(buildtypes::Terran_SCV->race == tc::BW::Race::Terran);
  EXPECT(buildtypes::Zerg_Drone->race == tc::BW::Race::Zerg);
  EXPECT(buildtypes::Zerg_Infested_Command_Center->race == tc::BW::Race::Zerg);
  EXPECT(buildtypes::Resource_Mineral_Field->race == tc::BW::Race::None);
  EXPECT(buildtypes::Spell_Dark_Swarm->race == tc::BW::Race::None);
}
