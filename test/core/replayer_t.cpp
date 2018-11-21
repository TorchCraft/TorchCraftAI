/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "replayer.h"
#include "state.h"

#include <glog/logging.h>
#include <thread>

using namespace cherrypi;
using namespace buildtypes;

CASE("replayer/zerg") {
  Replayer replay("test/maps/replays/TL_TvZ_IC420273.rep");
  auto* state = replay.state();
  auto& uinfo = state->unitsInfo();
  auto& ainfo = state->areaInfo();

  EXPECT(state->mapWidth() == 512);
  EXPECT(state->mapHeight() == 512);

  replay.setPerspective(0);
  EXPECT(state->playerId() == 0);
  EXPECT(state->myRace() == +tc::BW::Race::Zerg);

  replay.init();

  while (!state->gameEnded() && state->currentFrame() < 100) {
    replay.step();
  }
  EXPECT(!state->gameEnded());
  EXPECT(uinfo.myCompletedUnitsOfType(Zerg_Hatchery).size() == 1u);
  EXPECT(uinfo.enemyUnits().size() == 0u);
  EXPECT(ainfo.foundEnemyStartLocation() == false);

  while (!state->gameEnded() && state->currentFrame() < 2500) {
    replay.step();
  }
  EXPECT(!state->gameEnded());
  EXPECT(uinfo.myCompletedUnitsOfType(Zerg_Drone).size() == 12u);
  EXPECT(ainfo.foundEnemyStartLocation() == true); // found by exclusion

  while (!state->gameEnded() && state->currentFrame() < 4350) {
    replay.step();
  }
  EXPECT(!state->gameEnded());
  EXPECT(
      uinfo.myCompletedUnitsOfType(Zerg_Hatchery).size() ==
      2u); // still in construction
  EXPECT(uinfo.enemyUnits().size() == 3u);
  EXPECT(ainfo.foundEnemyStartLocation() == true);
  EXPECT(ainfo.numMyBases() == 2);
}

CASE("replayer/terran") {
  Replayer replay("test/maps/replays/TL_TvZ_IC420273.rep");
  auto* state = replay.state();
  auto& uinfo = state->unitsInfo();
  auto& ainfo = state->areaInfo();

  replay.setPerspective(1);
  EXPECT(state->playerId() == 1);
  EXPECT(state->myRace() == +tc::BW::Race::Terran);

  replay.init();

  while (!state->gameEnded() && state->currentFrame() < 100) {
    replay.step();
  }
  EXPECT(!state->gameEnded());
  EXPECT(uinfo.myCompletedUnitsOfType(Terran_Command_Center).size() == 1u);
  EXPECT(uinfo.enemyUnits().size() == 0u);
  EXPECT(ainfo.foundEnemyStartLocation() == false);

  while (!state->gameEnded() && state->currentFrame() < 2500) {
    replay.step();
  }
  EXPECT(uinfo.myUnitsOfType(Terran_Refinery).size() == 1u);
  EXPECT(
      uinfo.myCompletedUnitsOfType(Terran_Refinery).size() ==
      0u); // still in construction

  while (!state->gameEnded() && state->currentFrame() < 4300) {
    replay.step();
  }
  EXPECT(!state->gameEnded());
  EXPECT(ainfo.foundEnemyStartLocation() == true);
  EXPECT(uinfo.myCompletedUnitsOfType(Terran_Refinery).size() == 1u);
  EXPECT(uinfo.myCompletedUnitsOfType(Terran_Barracks).size() == 1u);
  EXPECT(uinfo.myCompletedUnitsOfType(Terran_SCV).size() == 16u);
  EXPECT(ainfo.numEnemyBases() == 1);
}

CASE("replayer/lose[hide]") {
  Replayer replay("test/maps/replays/TL_TvZ_IC420273.rep");
  auto* state = replay.state();
  replay.setPerspective(0);
  replay.run();
  EXPECT(state->gameEnded() == true);
  EXPECT(state->won() == false);
}

CASE("replayer/win[hide]") {
  Replayer replay("test/maps/replays/TL_TvZ_IC420273.rep");
  auto* state = replay.state();
  replay.setPerspective(1);
  replay.run();
  EXPECT(state->areaInfo().numMyBases() == 4);
  EXPECT(state->gameEnded() == true);
  EXPECT(state->won() == true);
}
