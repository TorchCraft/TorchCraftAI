/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "gameutils/game.h"
#include "test.h"

#include <glog/logging.h>

#include "modules.h"
#include "player.h"
#include "utils.h"

using namespace cherrypi;
DECLARE_double(rtfactor);

namespace {

class BuildAllUnitsModule : public AutoBuildModule {
 public:
  static std::vector<DefaultAutoBuildTask::Target> targets() {
    using namespace buildtypes;
    return std::vector<DefaultAutoBuildTask::Target>{
        {Ensnare, 1},
        {Zerg_Hydralisk, 1},
        {Zerg_Lurker, 1},
        {Lurker_Aspect, 1},
        {Zerg_Ultralisk, 1},
        {Zerg_Ultralisk_Cavern, 1},
        {Zerg_Spire, 1},
        {Zerg_Devourer, 1},
        {Zerg_Mutalisk, 1},
        {Zerg_Guardian, 1},
        {Zerg_Greater_Spire, 1},
        {Zerg_Mutalisk, 1},
        {Zerg_Scourge, 2},
        {Zerg_Drone, 37},
        {Zerg_Spire, 1},
        {Zerg_Defiler, 1},
        {Zerg_Drone, 36},
        {Zerg_Hatchery, 1},
        {Zerg_Lair, 1},
        {Zerg_Drone, 35},
        {Zerg_Defiler_Mound, 1},
        {Zerg_Drone, 34},
        {Zerg_Nydus_Canal, 1},
        {Zerg_Hive, 1},
        {Zerg_Drone, 32},
        {Zerg_Queen, 1},
        {Zerg_Drone, 26},
        {Zerg_Queens_Nest, 1},
        {Zerg_Hatchery, 1},
        {Zerg_Lair, 1},
        {Zerg_Drone, 23},
        {Zerg_Hydralisk, 1},
        {Zerg_Hydralisk_Den, 1},
        {Zerg_Creep_Colony, 1},
        {Zerg_Zergling, 2},
        {Zerg_Drone, 22},
        {Zerg_Sunken_Colony, 1},
        {Zerg_Drone, 19},
        {Zerg_Spawning_Pool, 1},
        {Zerg_Drone, 18},
        {Zerg_Drone, 13},
        {Zerg_Creep_Colony, 1},
        {Zerg_Spore_Colony, 1},
        {Zerg_Drone, 12},
        {Zerg_Evolution_Chamber, 1},
        {Zerg_Drone, 11},
        {Zerg_Creep_Colony, 1},
        {Zerg_Drone, 10},
        {Zerg_Extractor, 1},
        {Zerg_Drone, 7},
    };
  }

 protected:
  virtual std::shared_ptr<AutoBuildTask> createTask(
      State* state,
      int srcUpcId,
      std::shared_ptr<UPCTuple> srcUpc) override {
    if (!srcUpc->state.is<std::string>() &&
        !srcUpc->state.is<UPCTuple::Empty>()) {
      return nullptr;
    }
    // Return early if there is already a task created
    for (auto& task : state->board()->tasksOfModule(this)) {
      if (std::dynamic_pointer_cast<AutoBuildTask>(task)) {
        return nullptr;
      }
    }

    return std::make_shared<DefaultAutoBuildTask>(
        srcUpcId, state, this, targets());
  }
};

} // namespace

SCENARIO("builder/zerg_all_units[.flaky]") {
  auto scenario = GameSinglePlayerUMS("test/maps/eco-base-zerg.scm", "Zerg");
  Player bot(scenario.makeClient());
  bot.setRealtimeFactor(FLAGS_rtfactor);
  bot.setWarnIfSlow(false);

  bot.addModule(Module::make<CreateGatherAttackModule>());
  bot.addModule(Module::make<BuildAllUnitsModule>());
  bot.addModule(Module::make<BuildingPlacerModule>());
  bot.addModule(Module::make<BuilderModule>());
  bot.addModule(Module::make<GathererModule>());
  bot.addModule(Module::make<UPCToCommandModule>());

  bot.init();
  auto state = bot.state();
  constexpr int kMaxFrames = 30000;
  do {
    bot.step();
    if (state->currentFrame() > kMaxFrames) {
      break;
    }
  } while (!state->gameEnded());
  VLOG(0) << "Done after " << state->currentFrame() << " frames";

  auto targets = BuildAllUnitsModule::targets();
  std::map<BuildType const*, size_t> expectedCounts;
  for (auto const& tgt : targets) {
    if (expectedCounts.find(tgt.type) == expectedCounts.end()) {
      expectedCounts[tgt.type] = tgt.n;
    }
  }
  expectedCounts[buildtypes::Zerg_Spire] = 0; // replaced by greater spire
  expectedCounts[buildtypes::Zerg_Hatchery] = 2; // two macro hatcheries
  expectedCounts[buildtypes::Zerg_Lair] = 0; // replaced by hive
  auto& uinfo = state->unitsInfo();
  for (auto const& it : expectedCounts) {
    if (it.first->isUnit()) {
      EXPECT(it.second == uinfo.myUnitsOfType(it.first).size());
    } else if (it.first->isTech()) {
      EXPECT(state->hasResearched(it.first) == true);
    }
  }
}
