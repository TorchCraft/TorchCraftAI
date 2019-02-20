/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "gameutils/scenario.h"
#include "test.h"

#include "buildorders/base.h"
#include "modules.h"
#include "player.h"
#include "utils.h"

using namespace cherrypi;
DECLARE_double(rtfactor);
DEFINE_string(choose_map, "maps/(4)Fortress.scx", "SC map to load");
DEFINE_bool(test_gas, false, "test with a BO doing gas");

namespace {

// Build a bunch of drones
class BuildDronesModule : public AutoBuildModule {
  virtual std::shared_ptr<AutoBuildTask> createTask(
      State* state,
      int srcUpcId,
      std::shared_ptr<UPCTuple> srcUpc) override {
    if (!srcUpc->state.is<std::string>()) {
      return nullptr;
    }
    std::vector<DefaultAutoBuildTask::Target> targets;
    targets.emplace_back(buildtypes::Zerg_Drone, 24);
    return std::make_shared<DefaultAutoBuildTask>(
        srcUpcId, state, this, std::move(targets));
  }
};

// Build a bunch of drones with 2 expansions
class ABBO3BasePool : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  virtual void buildStep2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    st.autoBuildHatcheries = false;
    buildN(Zerg_Drone, 50);

    if (countPlusProduction(st, Zerg_Hatchery) == 2) {
      build(Zerg_Hatchery, nextBase);
      buildN(Zerg_Drone, 14);
    }
    if (countPlusProduction(st, Zerg_Hatchery) == 1) {
      build(Zerg_Hatchery, nextBase);
      buildN(Zerg_Drone, 12);
    }
  }
};

class ABBO3BaseGas : public ABBOBase { // TODO properly with RTTR from real BOs
 public:
  using ABBOBase::ABBOBase;
  bool buildExtractor = false;
  bool hasBuiltExtractor = false;
  int hurtSunkens = 0;
  bool hasSunken = false;
  bool wasAllinRushed = false;
  bool builtSecondExpansion = false;

  virtual void preBuild2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    if (!hasBuiltExtractor && countPlusProduction(st, Zerg_Drone) == 9 &&
        countPlusProduction(st, Zerg_Overlord) == 1) {
      buildExtractor = true;
      hasBuiltExtractor = cancelGas();
    } else {
      buildExtractor = false;
    }

    hasSunken = !state_->unitsInfo().myUnitsOfType(Zerg_Sunken_Colony).empty();
  }

  virtual void buildStep2(autobuild::BuildState& st) override {
    using namespace buildtypes;
    using namespace autobuild;

    st.autoBuildRefineries = countPlusProduction(st, Zerg_Extractor) == 0 ||
        st.frame >= 15 * 60 * 11;

    auto buildSunkens = [&](int n) {
      if (hasOrInProduction(st, Zerg_Creep_Colony)) {
        build(Zerg_Sunken_Colony);
      } else {
        if (myCompletedHatchCount >= 2 && nextStaticDefencePos != Position()) {
          if (countPlusProduction(st, Zerg_Sunken_Colony) < n &&
              !isInProduction(st, Zerg_Creep_Colony)) {
            build(Zerg_Creep_Colony, nextStaticDefencePos);
          }
        }
      }
    };

    if (st.frame < 15 * 60 * 4 + 15 * 50) {
      if (myCompletedHatchCount >= 2 && nextStaticDefencePos != Position()) {
        if (!hasSunken) {
          buildSunkens(2);
          return;
        }
      }
    }

    if (st.usedSupply[tc::BW::Race::Zerg] < 185.0 ||
        countPlusProduction(st, Zerg_Mutalisk) >= 20) {
      build(Zerg_Zergling);
      build(Zerg_Hydralisk);
    } else {
      build(Zerg_Mutalisk);
    }

    if (countPlusProduction(st, Zerg_Hydralisk) >= 40.0 &&
        (armySupply > enemyArmySupply || armySupply >= 80.0)) {
      buildN(Zerg_Mutalisk, 6);
      buildN(Zerg_Scourge, std::min((int)enemyAirArmySupply, 10));
    }

    if (countPlusProduction(st, Zerg_Zergling) >= 10) {
      upgrade(Metabolic_Boost);
    }

    if (armySupply > enemyArmySupply) {
      if (countProduction(st, Zerg_Drone) == 0) {
        buildN(Zerg_Drone, 66);
      }
      if (armySupply > enemyArmySupply + enemyAttackingArmySupply &&
          countProduction(st, Zerg_Drone) < 3) {
        buildN(Zerg_Drone, 45);
      }
    }

    if (armySupply > enemyArmySupply + 8.0 || armySupply >= 20.0) {
      if (st.workers >= 45) {
        buildN(Zerg_Evolution_Chamber, 2);
      }
      upgrade(Zerg_Missile_Attacks_1) && upgrade(Zerg_Missile_Attacks_2) &&
          upgrade(Zerg_Missile_Attacks_3);
      upgrade(Zerg_Carapace_1) && upgrade(Zerg_Carapace_2) &&
          upgrade(Zerg_Carapace_3);
    }

    if (bases < (armySupply >= 20.0 && armySupply > enemyArmySupply + 8.0
                     ? 4
                     : 3) &&
        !st.isExpanding && canExpand &&
        armySupply >= std::min(enemyArmySupply, 12.0)) {
      builtSecondExpansion = true;
      build(Zerg_Hatchery, nextBase);
    }
    if (armySupply > enemyArmySupply) {
      buildN(Zerg_Drone, 24 + std::max(enemyStaticDefenceCount - 3, 0) * 4);
    } else {
      buildN(Zerg_Drone, 24 + enemyStaticDefenceCount * 4);
    }

    upgrade(Muscular_Augments) && upgrade(Grooved_Spines);
    if (enemyStaticDefenceCount == 0 && !enemyHasExpanded) {
      buildN(Zerg_Hydralisk, 12);
    }
    buildN(Zerg_Hydralisk, 2);
    buildN(Zerg_Drone, 18);

    buildN(Zerg_Hatchery, 3);
    buildN(Zerg_Drone, 14);
    buildN(Zerg_Zergling, 4);
    buildSunkens((enemyZealotCount ? 2 : 1) + hurtSunkens);
    buildN(Zerg_Overlord, 2);
    buildN(Zerg_Spawning_Pool, 1);
    if (countPlusProduction(st, Zerg_Hatchery) == 1) {
      build(Zerg_Hatchery, nextBase);
      if (!hasBuiltExtractor && buildExtractor) {
        buildN(Zerg_Extractor, 1);
      }
      buildN(Zerg_Drone, 9);
    }
  }
};

class Build3BaseDronesModule : public AutoBuildModule {
  virtual std::shared_ptr<AutoBuildTask> createTask(
      State* state,
      int srcUpcId,
      std::shared_ptr<UPCTuple> srcUpc) override {
    if (srcUpc->state.is<std::string>()) {
      return std::make_shared<ABBO3BasePool>(srcUpcId, state, this);
    }
    return nullptr;
  }
};

class Build3BaseGasModule : public AutoBuildModule {
  virtual std::shared_ptr<AutoBuildTask> createTask(
      State* state,
      int srcUpcId,
      std::shared_ptr<UPCTuple> srcUpc) override {
    if (srcUpc->state.is<std::string>()) {
      return std::make_shared<ABBO3BaseGas>(srcUpcId, state, this);
    }
    return nullptr;
  }
};

// Baseline gatherer: send drone to closest mineral patch and let SC handle the
// rest
class BuiltinGathererModule : public Module {
 public:
  using Module::Module;

  void step(State* state) override {
    auto board = state->board();

    // Select mineral location for all gatherer UPCs
    for (auto& v : board->upcsWithSharpCommand(Command::Gather)) {
      UpcId upcId = v.first;
      auto& upc = v.second;
      if (upc->unit.empty()) {
        continue;
      }

      // Send all units to closest mineral
      auto base = state->areaInfo().myStartLocation();
      Unit* closest = nullptr;
      float closestDistance = kfMax;
      for (Unit* resource : state->unitsInfo().resourceUnits()) {
        if (resource->type->isMinerals) {
          auto distance = utils::distance(resource, base);
          if (distance < closestDistance) {
            closest = resource;
            closestDistance = distance;
          }
        }
      }
      if (closest == nullptr) {
        continue;
      }

      board->consumeUPC(upcId, this);
      for (auto uit : upc->unit) {
        if (uit.second > 0) {
          auto id = board->postUPC(
              utils::makeSharpUPC(uit.first, closest, Command::Gather),
              upcId,
              this);
          std::unordered_set<Unit*> units = {uit.first};

          // Post task to prevent this unit from popping up in future UPCs
          auto task = std::make_shared<Task>(id, units);
          task->setStatus(TaskStatus::Ongoing);
          board->postTask(task, this, true);
        }
      }
    }
  }
};

std::pair<int, int> gathered(State* state) {
  auto tcs = state->tcstate();
  auto res = tcs->frame->resources[tcs->player_id];
  auto ore = res.ore;
  auto gas = res.gas;
  auto& ui = state->unitsInfo();

  ore -= 650; // Available at start of game

  // Count our units
  for (Unit* u : ui.myUnits()) {
    gas += u->type->gasCost;
    ore += u->type->mineralCost;
  }

  return std::make_pair(ore, gas);
}

} // namespace

void runBenchmark(
    std::shared_ptr<Module> build,
    std::shared_ptr<Module> gatherer,
    size_t maxDrones = 24,
    bool upRightOnly = true,
    std::string scmap = "maps/(4)Fighting Spirit.scx") {
  std::unique_ptr<MeleeScenario> scenario;
  std::unique_ptr<Player> bot;

  do {
    scenario = std::make_unique<MeleeScenario>(scmap, "Zerg", "Terran");
    bot = std::make_unique<Player>(scenario->makeClient());
    bot->setWarnIfSlow(false);
    bot->setRealtimeFactor(FLAGS_rtfactor);

    bot->addModule(Module::make<CreateGatherAttackModule>());
    bot->addModule(build);
    bot->addModule(Module::make<BuildingPlacerModule>());
    bot->addModule(Module::make<BuilderModule>());
    bot->addModule(gatherer);
    bot->addModule(Module::make<UPCToCommandModule>());

    bot->init();
    bot->step();

    // Check location
    auto myStartLocation = bot->state()->areaInfo().myStartLocation();
    if (!upRightOnly ||
        (myStartLocation.x > bot->state()->mapWidth() / 2 &&
         myStartLocation.y < bot->state()->mapHeight() / 2)) {
      VLOG(0) << "Starting at " << myStartLocation.x << "," << myStartLocation.y
              << ", ok";
      break;
    }
    VLOG(0) << "Starting at " << myStartLocation.x << "," << myStartLocation.y
            << ", retrying";
  } while (true);

  auto state = bot->state();
  int const kMaxFrames = 15000;
  int nextT = 1000;
  bool past17 = false;

  while (!state->gameEnded() && state->currentFrame() < kMaxFrames) {
    bot->step();

    auto tmp = gathered(state);
    auto ore = tmp.first;
    auto gas = tmp.second;

    auto ndrones = state->unitsInfo()
                       .myCompletedUnitsOfType(buildtypes::Zerg_Drone)
                       .size();
    if (!past17 && ndrones >= 17) {
      VLOG(0) << gatherer->name() << ": "
              << " ore mined: " << ore << ", gas mined: " << gas
              << ", frames (>= 17 drones built): " << state->currentFrame();
      past17 = true;
    } else if (ndrones >= maxDrones) {
      VLOG(0) << gatherer->name() << ": "
              << " ore mined: " << ore << ", gas mined: " << gas
              << ", frames: " << state->currentFrame() << ", drones > "
              << maxDrones;
      break;
    }

    if (state->currentFrame() >= nextT) {
      VLOG(0) << gatherer->name() << ": "
              << " ore mined: " << ore << ", gas mined: " << gas
              << ", frames: " << state->currentFrame()
              << ", drones: " << ndrones;
      nextT += 1000;
    }
  }
  VLOG(0) << gatherer->name() << ": done in " << state->currentFrame()
          << " frames";
}

void runWithBuild(std::shared_ptr<Module> gatherer) {
  std::unique_ptr<MeleeScenario> scenario;
  std::unique_ptr<Player> bot;

  scenario = std::make_unique<MeleeScenario>(
      "maps/(4)Fighting Spirit.scx", "Zerg", "Terran");
  bot = std::make_unique<Player>(scenario->makeClient());
  bot->setWarnIfSlow(false);
  bot->setRealtimeFactor(FLAGS_rtfactor);

  bot->addModule(Module::make<CreateGatherAttackModule>());
  bot->addModule(Module::make<StrategyModule>());
  bot->addModule(Module::make<GenericAutoBuildModule>());
  bot->addModule(Module::make<BuilderModule>());
  bot->addModule(gatherer);
  bot->addModule(Module::make<UPCToCommandModule>());

  bot->init();

  auto state = bot->state();
  int const kMaxFrames = 15000;
  int nextT = 1000;

  while (!state->gameEnded() && state->currentFrame() < kMaxFrames) {
    bot->step();

    auto tmp = gathered(state);
    auto ore = tmp.first;
    auto gas = tmp.second;
    auto ndrones = state->unitsInfo()
                       .myCompletedUnitsOfType(buildtypes::Zerg_Drone)
                       .size();

    if (state->currentFrame() >= nextT) {
      VLOG(0) << gatherer->name() << ": "
              << " ore mined: " << ore << ", gas mined: " << gas
              << ", frames: " << state->currentFrame()
              << ", drones: " << ndrones;
      nextT += 1000;
    }
  }

  VLOG(0) << gatherer->name() << ": done in " << state->currentFrame()
          << " frames";
}

SCENARIO("gatherer/efficiency/our/mining") {
  runBenchmark(
      Module::make<BuildDronesModule>(), Module::make<GathererModule>());
}

SCENARIO("gatherer/efficiency/our/3base") {
  runBenchmark(
      Module::make<Build3BaseDronesModule>(),
      Module::make<GathererModule>(),
      50);
}

SCENARIO("gatherer/efficiency/our/fortress") {
  if (FLAGS_test_gas)
    runBenchmark(
        Module::make<Build3BaseGasModule>(),
        Module::make<GathererModule>(),
        64,
        false,
        FLAGS_choose_map);
  else
    runBenchmark(
        Module::make<Build3BaseDronesModule>(),
        Module::make<GathererModule>(),
        64,
        false,
        FLAGS_choose_map);
}

SCENARIO("gatherer/efficiency/baseline/mining") {
  runBenchmark(
      Module::make<BuildDronesModule>(), Module::make<BuiltinGathererModule>());
}

SCENARIO("gatherer/efficiency/baseline/3base") {
  runBenchmark(
      Module::make<Build3BaseDronesModule>(),
      Module::make<BuiltinGathererModule>(),
      50);
}

SCENARIO("gatherer/default[hide]") {
  runWithBuild(Module::make<GathererModule>());
}
