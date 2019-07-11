/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "buildtype.h"
#include "replayer.h"
#include "snapshotter.h"
#include "test.h"

#include <glog/logging.h>
#include <range/v3/all.hpp>

using namespace cherrypi;
using namespace buildtypes;

namespace {

Snapshot getSnapshot(int skipFrames = 0) {
  Replayer replay("test/maps/replays/TL_TvZ_IC420273.rep");
  replay.setPerspective(0);
  replay.init();
  for (auto i = -1; i < skipFrames; ++i) {
    replay.step();
  }
  return stateToSnapshot(replay.tcstate());
};

constexpr int kExampleSteps = 24 * (18 * 60 + 24) / 3;
} // namespace

CASE("snapshot/stateToSnapshot") {
  cherrypi::init();

  Snapshot snapshot = getSnapshot();
  EXPECT(snapshot.players.size() == 2);
  EXPECT(snapshot.mapBuildTileWidth == 128);
  EXPECT(snapshot.mapBuildTileHeight == 128);
  EXPECT(snapshot.mapTitle == "| iCCup | Medusa 1.0");
  EXPECT(snapshot.replay == "TL_TvZ_IC420273.rep");

  // Test unit snapshotting

  auto& units0 = snapshot.players[0].units;
  auto& units1 = snapshot.players[1].units;
  EXPECT(units0.size() == 9);
  EXPECT(units1.size() == 5);

  auto countUnits = [](std::vector<SnapshotUnit>& units,
                       const BuildType* unitType) {
    return ranges::count_if(
        units, [&](auto& unit) { return unit.type == unitType->unit; });
  };

  int drones = countUnits(units0, buildtypes::Zerg_Drone);
  int larva = countUnits(units0, buildtypes::Zerg_Larva);
  int overlords = countUnits(units0, buildtypes::Zerg_Overlord);
  int hatcheries = countUnits(units0, buildtypes::Zerg_Hatchery);
  int scvs = countUnits(units1, buildtypes::Terran_SCV);
  int commandCenters = countUnits(units1, buildtypes::Terran_Command_Center);

  EXPECT(drones == 4);
  EXPECT(larva == 3);
  EXPECT(overlords == 1);
  EXPECT(hatcheries == 1);
  EXPECT(scvs == 4);
  EXPECT(commandCenters == 1);

  auto hatchery = ranges::find_if(units0, [](auto& unit) {
    return unit.type == buildtypes::Zerg_Hatchery->unit;
  });
  auto overlord = ranges::find_if(units0, [](auto& unit) {
    return unit.type == buildtypes::Zerg_Overlord->unit;
  });
  auto allDrones = ranges::view::filter(units0, [](auto& unit) {
    return unit.type == buildtypes::Zerg_Drone->unit;
  });
  auto allLarva = ranges::view::filter(units0, [](auto& unit) {
    return unit.type == buildtypes::Zerg_Larva->unit;
  });
  EXPECT(hatchery->health == 1250);
  EXPECT(hatchery->shields == 0);
  EXPECT(hatchery->energy == 0);
  EXPECT(overlord->health == 200);
  EXPECT(overlord->shields == 0);
  EXPECT(overlord->energy == 0);
  auto dronesRight = ranges::count_if(
      allDrones, [&](auto& drone) { return drone.x > hatchery->x; });
  auto larvaBelow = ranges::count_if(
      allLarva, [&](auto& larva) { return larva.y > hatchery->y; });
  EXPECT(dronesRight == 3); // The Drones are lined up below the Hatchery, and
                            // three are right of its center
  EXPECT(larvaBelow == 3);

  // Test upgrade/tech snapshotting

  // The Zerg player finishes Level 2 range attacks, ground carapace, and air
  // carapace at a bit before 18:24
  snapshot = getSnapshot(kExampleSteps);
  EXPECT(
      snapshot.players[0].getUpgradeLevel(
          buildtypes::Charon_Boosters->upgrade) == 1);
  EXPECT(
      snapshot.players[1].getUpgradeLevel(
          buildtypes::Charon_Boosters->upgrade) == 0);
  EXPECT(
      snapshot.players[1].getUpgradeLevel(
          buildtypes::Metabolic_Boost->upgrade) == 1);
  EXPECT(
      snapshot.players[1].getUpgradeLevel(
          buildtypes::Zerg_Carapace_1->upgrade) == 2);
  EXPECT(
      snapshot.players[1].hasTech(buildtypes::Tank_Siege_Mode->tech) == false);
  EXPECT(
      snapshot.players[0].hasTech(buildtypes::Tank_Siege_Mode->tech) == true);
}

CASE("snapshot/snapshotToScenario") {
  // Populate the snapshot from the same replay (mostly so we can get properly
  // formatted upgrades/tech)
  Snapshot snapshot = getSnapshot(kExampleSteps);
  snapshot.players[0].units = {
      {buildtypes::Zerg_Zergling->unit, 1, 2, 3, 4, 5},
      {buildtypes::Zerg_Drone->unit, 6, 7, 8, 9, 10},
  };
  snapshot.players[1].units = {
      {buildtypes::Terran_SCV->unit, 10, 20, 30, 40, 50},
      {buildtypes::Terran_Marine->unit, 60, 70, 80, 90, 100},
  };

  auto scenario = snapshotToScenario(snapshot);
  EXPECT(
      scenario.players[0].getUpgradeLevel(
          buildtypes::Charon_Boosters->upgrade) == 1);
  EXPECT(
      scenario.players[1].getUpgradeLevel(
          buildtypes::Charon_Boosters->upgrade) == 0);
  EXPECT(
      scenario.players[0].hasTech(buildtypes::Tank_Siege_Mode->tech) == true);
  EXPECT(
      scenario.players[1].hasTech(buildtypes::Tank_Siege_Mode->tech) == false);
  EXPECT(scenario.allies()[0].count == 1);
  EXPECT(scenario.allies()[0].type == buildtypes::Zerg_Zergling->unit);
  EXPECT(scenario.allies()[0].x == 1);
  EXPECT(scenario.allies()[0].y == 2);
  EXPECT(scenario.allies()[0].health == 3);
  EXPECT(scenario.allies()[0].shields == 4);
  EXPECT(scenario.allies()[0].energy == 5);
  EXPECT(scenario.allies()[1].type == buildtypes::Zerg_Drone->unit);
  EXPECT(scenario.allies()[1].x == 6);
  EXPECT(scenario.allies()[1].y == 7);
  EXPECT(scenario.allies()[1].health == 8);
  EXPECT(scenario.allies()[1].shields == 9);
  EXPECT(scenario.allies()[1].energy == 10);
  EXPECT(scenario.allies()[0].count == 1);
  EXPECT(scenario.enemies()[0].type == buildtypes::Terran_SCV->unit);
  EXPECT(scenario.enemies()[0].x == 10);
  EXPECT(scenario.enemies()[0].y == 20);
  EXPECT(scenario.enemies()[0].health == 30);
  EXPECT(scenario.enemies()[0].shields == 40);
  EXPECT(scenario.enemies()[0].energy == 50);
  EXPECT(scenario.enemies()[1].type == buildtypes::Terran_Marine->unit);
  EXPECT(scenario.enemies()[1].x == 60);
  EXPECT(scenario.enemies()[1].y == 70);
  EXPECT(scenario.enemies()[1].health == 80);
  EXPECT(scenario.enemies()[1].shields == 90);
  EXPECT(scenario.enemies()[1].energy == 100);
}