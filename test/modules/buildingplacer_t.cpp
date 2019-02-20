/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "gameutils/scenario.h"
#include "test.h"

#include <glog/logging.h>

#include "fivepool.h"
#include "modules.h"
#include "player.h"
#include "upcfilter.h"
#include "utils.h"

using namespace cherrypi;
DECLARE_double(rtfactor);

namespace {

void postSpawningPoolOnGeyser(State* state, Module* self) {
  assert(state->areaInfo().foundMyStartLocation());
  auto loc = state->areaInfo().myStartLocation();

  // Get closest geyser
  Unit* closest = nullptr;
  float mind = kfMax;
  for (Unit* unit : state->unitsInfo().resourceUnits()) {
    if (unit->type == buildtypes::Resource_Vespene_Geyser) {
      auto d = utils::distance(unit, loc);
      if (d < mind) {
        mind = d;
        closest = unit;
      }
    }
  }

  if (closest) {
    auto upc = std::make_shared<UPCTuple>();
    upc->command[Command::Create] = 1;
    upc->state = UPCTuple::BuildTypeMap{{buildtypes::Zerg_Spawning_Pool, 1}};
    upc->position = Position(closest);
    state->board()->postUPC(std::move(upc), kRootUpcId, self);
  }
}

// ATen is required for position masks
#ifdef HAVE_ATEN

// We'll restrict building locations with a custom UPC filter that replaces the
// position distribution for build order UPCs with a given mask.
class RestrictLocationFilter : public UPCFilter {
 public:
  RestrictLocationFilter(Module* boModule, torch::Tensor mask)
      : boModule_(boModule), mask_(std::move(mask)) {}

  bool filter(State* state, std::shared_ptr<UPCTuple> upc, Module* origin)
      override {
    if (origin != boModule_) {
      return true;
    }

    // We'll still allow the extractor to be placed anywhere
    if (upc->state.is<UPCTuple::BuildTypeMap>()) {
      auto& map = upc->state.get_unchecked<UPCTuple::BuildTypeMap>();
      auto it = map.find(buildtypes::Zerg_Extractor);
      if (it != map.end()) {
        return true;
      }
    }

    upc->position = mask_;
    return true;
  }

 private:
  Module* boModule_;
  torch::Tensor mask_;
};

#endif // HAVE_ATEN

// Set location for second base
class SecondBaseFilter : public UPCFilter {
 public:
  SecondBaseFilter(Module* boModule) : boModule_(boModule) {}

  bool filter(State* state, std::shared_ptr<UPCTuple> upc, Module* origin)
      override {
    if (origin != boModule_) {
      return true;
    }
    if (upc->state.is<UPCTuple::BuildTypeMap>()) {
      auto& map = upc->state.get_unchecked<UPCTuple::BuildTypeMap>();
      auto it = map.find(buildtypes::Zerg_Hatchery);
      if (it == map.end()) {
        return true;
      }
    } else {
      return true;
    }
    if (state->unitsInfo()
            .myCompletedUnitsOfType(buildtypes::Zerg_Hatchery)
            .size() != 1) {
      // This is not the second base
      return true;
    }

    // Find a good location for the second base
    if (!foundSecondBase) {
      // Select closest base in nearby areas
      std::vector<Position> locs;
      auto& areaInfo = state->areaInfo();
      auto& myBaseArea = areaInfo.getArea(areaInfo.myStartLocation());
      for (auto* next : myBaseArea.neighbors) {
        for (auto& loc : next->baseLocations) {
          locs.push_back(loc);
        }
      }

      auto myBase = state->areaInfo().myStartLocation();
      secondBase = utils::getBestScoreCopy(
          locs,
          [&](Position const& pos) {
            return utils::distance(pos.x, pos.y, myBase.x, myBase.y);
          },
          kdInfty);
      secondBase.x /= tc::BW::XYWalktilesPerBuildtile;
      secondBase.y /= tc::BW::XYWalktilesPerBuildtile;
      // Base position is center, but builder uses UPC position as top-left.
      secondBase.x -= buildtypes::Zerg_Hatchery->tileWidth / 2;
      secondBase.y -= buildtypes::Zerg_Hatchery->tileHeight / 2;
    }

    upc->position = secondBase;
    upc->scale = tc::BW::XYWalktilesPerBuildtile;
    VLOG(1) << "Place next base at " << secondBase.x << ", " << secondBase.y;
    return true;
  }

  bool foundSecondBase = false;
  Position secondBase;

 private:
  Module* boModule_;
};

class SecondBaseModule : public FivePoolModule {
 public:
  SecondBaseModule() : FivePoolModule() {
    builds_.clear();
    builds_.emplace_back(buildtypes::Zerg_Drone);
    builds_.emplace_back(buildtypes::Zerg_Drone);
    builds_.emplace_back(buildtypes::Zerg_Hatchery);
    builds_.emplace_back(buildtypes::Zerg_Spawning_Pool);
  }
};

// ATen is required for position masks
#ifdef HAVE_ATEN
std::unique_ptr<Player> setupPlayerWithMask(
    Scenario const& sc,
    torch::Tensor mask) {
  auto player = std::make_unique<Player>(sc.makeClient());

  player->addModule(Module::make<CreateGatherAttackModule>());
  auto buildOrder = Module::make<FivePoolModule>();
  player->addModule(buildOrder);
  player->addModule(Module::make<BuildingPlacerModule>());
  player->addModule(Module::make<BuilderModule>());
  player->addModule(Module::make<GathererModule>());
  player->addModule(Module::make<UPCToCommandModule>());

  player->state()->board()->addUPCFilter(
      std::make_shared<RestrictLocationFilter>(
          buildOrder.get(), std::move(mask)));

  player->init();
  return player;
}
#endif // HAVE_ATEN

} // namespace

// ATen is required for position masks
#ifdef HAVE_ATEN
SCENARIO("buildingplacer/fivepool_restrict_v") {
  auto mask = torch::zeros({256, 256});
  int const minX = 52;
  int const maxX = 66;
  auto acc = mask.accessor<float, 2>();
  auto const val = 1.0f / (float(maxX - minX) * acc.size(0));
  for (int y = 0; y < acc.size(0); y++) {
    for (int x = minX; x <= maxX; x++) {
      acc[y][x] = val;
    }
  }

  auto scenario = Scenario("test/maps/eco-base-zerg.scm", "Zerg");
  auto player = setupPlayerWithMask(scenario, mask);
  auto state = player->state();

  int const kMaxFrames = 6000;
  do {
    player->step();
    if (state->currentFrame() > kMaxFrames) {
      break;
    }
    if (state->unitsInfo()
            .myCompletedUnitsOfType(buildtypes::Zerg_Zergling)
            .size() == 10) {
      break;
    }
  } while (!state->gameEnded());
  VLOG(0) << "Done after " << state->currentFrame() << " frames";

  // Check that we have all the units that we wanted
  auto& ui = state->unitsInfo();
  EXPECT(ui.myCompletedUnitsOfType(buildtypes::Zerg_Zergling).size() == 10u);
  EXPECT(ui.myCompletedUnitsOfType(buildtypes::Zerg_Drone).size() == 6u);
  EXPECT(
      ui.myCompletedUnitsOfType(buildtypes::Zerg_Spawning_Pool).size() == 1u);
  EXPECT(ui.myCompletedUnitsOfType(buildtypes::Zerg_Overlord).size() == 2u);

  // Check that we stayed within the restricted location
  for (auto* bldg : state->unitsInfo().myBuildings()) {
    EXPECT(acc[bldg->buildY][bldg->buildX] > 0);
  }
}

SCENARIO("buildingplacer/fivepool_restrict_h") {
  auto mask = torch::zeros({256, 256});
  int const minY = 48;
  int const maxY = 60;
  auto acc = mask.accessor<float, 2>();
  auto const val = 1.0f / (float(maxY - minY) * acc.size(1));
  for (int y = minY; y <= maxY; y++) {
    for (int x = 0; x < acc.size(1); x++) {
      acc[y][x] = val;
    }
  }

  auto scenario = Scenario("test/maps/eco-base-zerg.scm", "Zerg");
  auto player = setupPlayerWithMask(scenario, mask);
  auto state = player->state();

  int const kMaxFrames = 6000;
  do {
    player->step();
    if (state->currentFrame() > kMaxFrames) {
      break;
    }
    if (state->unitsInfo()
            .myCompletedUnitsOfType(buildtypes::Zerg_Zergling)
            .size() == 10) {
      break;
    }
  } while (!state->gameEnded());
  VLOG(0) << "Done after " << state->currentFrame() << " frames";

  // Check that we have all the units that we wanted
  auto& ui = state->unitsInfo();
  EXPECT(ui.myCompletedUnitsOfType(buildtypes::Zerg_Zergling).size() == 10u);
  EXPECT(ui.myCompletedUnitsOfType(buildtypes::Zerg_Drone).size() == 6u);
  EXPECT(
      ui.myCompletedUnitsOfType(buildtypes::Zerg_Spawning_Pool).size() == 1u);
  EXPECT(ui.myCompletedUnitsOfType(buildtypes::Zerg_Overlord).size() == 2u);

  // Check that we stayed within the restricted location
  for (auto* bldg : state->unitsInfo().myBuildings()) {
    EXPECT(acc[bldg->buildY][bldg->buildX] > 0);
  }
}
#endif // HAVE_ATEN

SCENARIO("buildingplacer/second_base") {
  auto scenario = MeleeScenario("maps/(2)Heartbreak Ridge.scx", "Zerg");
  auto player = std::make_unique<Player>(scenario.makeClient());

  player->addModule(Module::make<CreateGatherAttackModule>());
  auto buildOrder = Module::make<SecondBaseModule>();
  player->addModule(buildOrder);
  player->addModule(Module::make<BuildingPlacerModule>());
  player->addModule(Module::make<BuilderModule>());
  player->addModule(Module::make<GathererModule>());
  player->addModule(Module::make<UPCToCommandModule>());

  player->state()->board()->addUPCFilter(
      std::make_shared<SecondBaseFilter>(buildOrder.get()));

  player->init();
  auto state = player->state();

  int const kMaxFrames = 8000;
  do {
    player->step();
    if (state->currentFrame() > kMaxFrames) {
      break;
    }
    if (state->unitsInfo()
            .myCompletedUnitsOfType(buildtypes::Zerg_Hatchery)
            .size() == 2) {
      break;
    }
  } while (!state->gameEnded());
  VLOG(0) << "Done after " << state->currentFrame() << " frames";

  // Check that we have all the units that we wanted
  auto& ui = state->unitsInfo();
  auto hatcheries = ui.myCompletedUnitsOfType(buildtypes::Zerg_Hatchery);
  EXPECT(hatcheries.size() == size_t(2));
  if (hatcheries.size() < 2) {
    return;
  }

  // Check that hatcheries are sufficiently far away from each other.
  // XXX This is a pretty lousy condition.
  EXPECT(utils::distance(hatcheries[0], hatcheries[1]) > 60);
}

SCENARIO("buildingplacer/invalid_dirac") {
  Scenario scenario("test/maps/eco-base-zerg.scm", "Zerg");
  Player player(scenario.makeClient());
  player.setRealtimeFactor(FLAGS_rtfactor);

  player.addModule(Module::make<CreateGatherAttackModule>());
  player.addModule(Module::make<OnceModule>(
      postSpawningPoolOnGeyser, "PostSpawningPoolOnGeyser"));
  player.addModule(Module::make<BuildingPlacerModule>());
  player.addModule(Module::make<BuilderModule>());
  player.addModule(Module::make<GathererModule>());
  player.addModule(Module::make<UPCToCommandModule>());

  int const kMaxFrames = 2500;
  player.init();
  auto state = player.state();
  do {
    player.step();
    if (state->currentFrame() > kMaxFrames) {
      break;
    }
    if (state->unitsInfo()
            .myCompletedUnitsOfType(buildtypes::Zerg_Spawning_Pool)
            .size() > 0) {
      break;
    }
  } while (!state->gameEnded());
  VLOG(0) << "Done after " << state->currentFrame() << " frames";

  // Check that we have all the units that we wanted
  auto& ui = state->unitsInfo();
  EXPECT(
      ui.myCompletedUnitsOfType(buildtypes::Zerg_Spawning_Pool).size() == 1u);
}
