/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "features/features.h"
#include "features/unitsfeatures.h"
#include "gameutils/selfplayscenario.h"
#include "modules.h"
#include "modules/cherryvisdumper.h"
#include "player.h"

#include <common/fsutils.h>

#include <glog/logging.h>

DEFINE_string(
    cvisdumper_t_store_replays,
    "",
    "A directory where to store the replay files generated in cvisdumper_t.cpp "
    "tests");

using namespace cherrypi;
using namespace common;

namespace {
void addDefaultModules(std::shared_ptr<Player> bot) {
  bot->addModule(Module::make<TopModule>());
  bot->addModule(Module::make<CreateGatherAttackModule>());
  bot->addModule(Module::make<StrategyModule>());
  bot->addModule(Module::make<GenericAutoBuildModule>());
  bot->addModule(Module::make<BuildingPlacerModule>());
  bot->addModule(Module::make<BuilderModule>());
  bot->addModule(Module::make<TacticsModule>());
  bot->addModule(Module::make<SquadCombatModule>());
  bot->addModule(Module::make<ScoutingModule>());
  bot->addModule(Module::make<GathererModule>());
  bot->addModule(Module::make<HarassModule>());
  bot->addModule(Module::make<StaticDefenceFocusFireModule>());
  bot->addModule(Module::make<UPCToCommandModule>());
}

std::shared_ptr<Player> createMyPlayer(SelfPlayScenario* scenario) {
  auto bot = std::make_shared<Player>(scenario->makeClient1());
  addDefaultModules(bot);
  return bot;
}

std::shared_ptr<Player> createEnemyPlayer(SelfPlayScenario* scenario) {
  auto bot = std::make_shared<Player>(scenario->makeClient2());
  addDefaultModules(bot);
  bot->init();
  return bot;
}

SelfPlayScenario createScenario(std::string const& replayPath) {
  return SelfPlayScenario(
      "maps/(4)Fighting Spirit.scx",
      tc::BW::Race::Zerg,
      tc::BW::Race::Zerg,
      GameType::Melee,
      replayPath);
}
} // namespace

SCENARIO("cherryvisdumper") {
  GIVEN("example_use_case") {
    // This test case is an example of how to use CherryVisDumper module
    auto directory = FLAGS_cvisdumper_t_store_replays.empty()
        ? fsutils::mktempd()
        : FLAGS_cvisdumper_t_store_replays;
    auto replayPath = directory + "/example_use_case.rep";
    auto scenario = createScenario(replayPath);
    auto ourBot = createMyPlayer(&scenario);
    // Add the tracer module before 'init' call
    ourBot->dumpTraceAlongReplay(replayPath);
    ourBot->init();

    // Now let's play with the tracer functions
    auto state = ourBot->state();
    auto traceDumper = state->board()->getTraceDumper();

    auto demoLogging = [&]() {
      // Dump log messages using VLOG/LOG functions
      VLOG(1) << "This log message will be included in the trace dump if "
                 "loglevel >= 1";
      // ... or by calling them directly to the tracer
      CVIS_LOG(state) << "This log message was logged using handleLog";
    };

    auto demoTree = [&]() {
      // Create a tree
      struct Node {
        Node(std::string const& n, std::vector<std::shared_ptr<Node>> c = {})
            : name(n), children(c) {}
        std::string name;
        std::vector<std::shared_ptr<Node>> children;
      };
      auto root = std::make_shared<Node>("root");
      root->name = "root";
      root->children.push_back(std::make_shared<Node>("c1"));
      root->children.push_back(std::make_shared<Node>("c2"));
      root->children.push_back(std::make_shared<Node>(
          "c3",
          std::vector<std::shared_ptr<Node>>{std::make_shared<Node>("c3.1"),
                                             std::make_shared<Node>("c3.2")}));

      // Dump it to the trace
      traceDumper->addTree(
          state,
          /* treeName */ "demo",
          /* dumpNode */
          [](std::shared_ptr<Node> from,
             std::shared_ptr<CherryVisDumperModule::TreeNode> to) {
            to->setModule("DemoModule");
            to->setFrame(rand() % 100);
            (*to) << from->name;
            // You can also add a probability distribution over units:
            //  to->addUnitWithProb(unit, proba)
            // You can also set an ID - it has to be unique.
            // Otherwise, an ID will be generated
            //  to->setId(5, "node");
          },
          /* getChildren */
          [](std::shared_ptr<Node> parent) { return parent->children; },
          root);
    };

    auto demoDumpTensorSummary = [&]() {
      auto unitFeaturizer = cherrypi::UnitStatFeaturizer();
      auto myUnitFeatures =
          unitFeaturizer.extract(state, state->unitsInfo().myUnits());
      traceDumper->dumpTensorsSummary(
          state,
          {
              {"myUnitFeatures.data", myUnitFeatures.data},
              {"myUnitFeatures.positions", myUnitFeatures.positions},
              {"torch::random", torch::rand({10, 10})},
          });
    };

    auto demoDumpHeatmap = [&]() {
      auto mapFeatures = featurizePlain(
          state,
          {cherrypi::PlainFeatureType::Walkability,
           cherrypi::PlainFeatureType::Buildability,
           cherrypi::PlainFeatureType::CandidateEnemyStartLocations,
           cherrypi::PlainFeatureType::FogOfWar,
           cherrypi::PlainFeatureType::GroundHeight},
          cherrypi::Rect(
              {10, 5}, // Offsetting is supported
              {state->mapWidth(), state->mapHeight()}));
      mapFeatures =
          subsampleFeature(mapFeatures, SubsampleMethod::Average, 2, 0);
      // In a single call to 'dumpTerrainHeatmaps', all tensors must share
      // the same scaling and offsetting, but may have different shapes
      traceDumper->dumpTerrainHeatmaps(
          state,
          {{"Walkability", mapFeatures.tensor[0]},
           {"Walkability_Xslice", mapFeatures.tensor[0].slice(1, 0, 10)},
           {"Walkability_Yslice", mapFeatures.tensor[0].slice(0, 0, 10)},
           {"Buildability", mapFeatures.tensor[1]},
           {"CandidateEnemyStartLocations", mapFeatures.tensor[2]},
           {"FogOfWar", mapFeatures.tensor[3]},
           {"GroundHeight", mapFeatures.tensor[4]}},
          /* topLeftPixel */
          {mapFeatures.offset.x * tc::BW::XYPixelsPerWalktile,
           mapFeatures.offset.y * tc::BW::XYPixelsPerWalktile},
          /* scalingToPixels */
          {float(mapFeatures.scale) * tc::BW::XYPixelsPerWalktile,
           float(mapFeatures.scale) * tc::BW::XYPixelsPerWalktile});
    };

    auto demoFrameValue = [&]() {
      auto& units = state->unitsInfo();
      traceDumper->dumpGameValue(state, "units.mine", units.myUnits().size());
      traceDumper->dumpGameValue(
          state, "units.enemy", units.enemyUnits().size());
      traceDumper->dumpGameValue(
          state, "units.allever", units.allUnitsEver().size());
      // Values can be dumped at different frequencies
      if (state->currentFrame() % 200 == 0) {
        traceDumper->dumpGameValue(
            state, "buildings.mine", units.myBuildings().size());
      }
    };

    auto demoUnitLogs = [&]() {
      auto& units = state->unitsInfo().visibleUnits();
      for (auto& u : units) {
        CVIS_LOG_UNIT(state, u) << "Hi I'm visible with " << u->unit.health
                                << " hp at pos " << u->pos();
      }
    };

    auto demoLogDistribution = [&]() {
      // Let's start with a simple plot
      std::unordered_map<float, float> xSq;
      for (int x = -10; x < 10; ++x) {
        xSq[x] = x * x;
      }
      xSq[15.0f] = 15.0f * 15.0f;
      CVIS_LOG(state) << "x^2" << xSq;

      // Possible to have units and positions in the same proba distr
      std::unordered_map<CherryVisDumperModule::Dumpable, float> unitsHealth;
      for (auto& u : state->unitsInfo().myUnits()) {
        unitsHealth[u] = u->unit.health;
      }
      for (auto& u : state->unitsInfo().enemyUnits()) {
        unitsHealth[u->pos()] = u->unit.health;
      }
      CVIS_LOG(state) << "unit | pos -> health" << unitsHealth;

      // It can be a distribution on integers
      std::unordered_map<std::string, float> unitsByType;
      for (auto& u : state->unitsInfo().myUnits()) {
        unitsByType[u->type->name] += 1;
      }
      CVIS_LOG(state) << "type -> count" << unitsByType;

      // We can map units to units
      std::unordered_map<Unit*, Unit*> unitsAttackedBy;
      for (auto& u : state->unitsInfo().myUnits()) {
        if (!u->beingAttackedByEnemies.empty())
          unitsAttackedBy[u] = u->beingAttackedByEnemies[0];
      }
      if (!unitsAttackedBy.empty()) {
        CVIS_LOG(state) << "first attacker" << unitsAttackedBy;
      }

      // It's possible to log multiple maps on the same message
      CVIS_LOG(state) << "multiple attachments" << unitsByType << xSq;
    };

    demoLogging();
    demoTree();

    auto p2 = createEnemyPlayer(&scenario);
    int i = 0;
    constexpr int kDumpTensorsEvery = 1000;
    constexpr int kUnitLogsEvery = 200;
    while (!state->gameEnded() && state->currentFrame() < 6000) {
      ourBot->step();
      p2->step();
      demoFrameValue();
      if ((i % kUnitLogsEvery) == 0) {
        demoUnitLogs();
        demoLogDistribution();
      }
      if (((i++) % kDumpTensorsEvery) == 0) {
        demoDumpTensorSummary();
        demoDumpHeatmap();
      }
    }
    EXPECT((kDumpTensorsEvery < i));

    p2->leave();
    while (!state->gameEnded()) {
      ourBot->step();
      p2->step();
    }
  }
}
