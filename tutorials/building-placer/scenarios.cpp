/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "scenarios.h"
#include "rlbuildingplacer.h"

#include "modules.h"
#include "player.h"
#include "utils.h"

#include <common/fsutils.h>
#include <common/rand.h>

#include <gflags/gflags.h>
#include <glog/logging.h>

DECLARE_string(build);

namespace fsutils = common::fsutils;

namespace cherrypi {

namespace {
// Maximum game time is 30 minutes
FrameNum constexpr kMaxFrames = 30 * 24 * 60;

// Modules added *before* building placer
char constexpr kPreModules[] =
    "Top,"
    "CreateGatherAttack,"
    "Strategy,"
    "GenericAutoBuild";

// Modules added *after* building placer
char constexpr kPostModules[] =
    "Builder,"
    "Tactics,"
    "SquadCombat,"
    "Scouting,"
    "Gatherer,"
    "Harass,"
    "StaticDefenceFocusFireModule,"
    "UPCToCommand";

std::string selectMap(std::string const& mapDirOrFile) {
  if (fsutils::isdir(mapDirOrFile) == false) {
    return mapDirOrFile;
  }

  auto maps = fsutils::findr(mapDirOrFile, "*.sc[xm]");
  return maps[common::Rand::rand() % maps.size()];
}

std::string selectBuild(std::string const& builds) {
  auto buildv = utils::stringSplit(builds, '_');
  return buildv[common::Rand::rand() % buildv.size()];
}

void setupLearningPlayer(BasePlayer* player) {
  for (auto name : utils::stringSplit(kPreModules, ',')) {
    player->addModule(Module::make(name));
  }
  player->addModule(Module::make<RLBuildingPlacerModule>());
  for (auto name : utils::stringSplit(kPostModules, ',')) {
    player->addModule(Module::make(name));
  }

  player->setLogFailedCommands(false);
  player->setCheckConsistency(false);
}

void setupRuleBasedPlayer(BasePlayer* player, bool includeOffense = true) {
  for (auto name : utils::stringSplit(kPreModules, ',')) {
    player->addModule(Module::make(name));
  }
  player->addModule(Module::make("BuildingPlacer"));
  for (auto name : utils::stringSplit(kPostModules, ',')) {
    player->addModule(Module::make(name));
  }

  // Reduce work done and output produced by purely rule-based player
  player->setDraw(false);
  player->setLogFailedCommands(false);
  player->setCheckConsistency(false);
  player->setCollectTimers(false);
  player->state()->board()->upcStorage()->setPersistent(false);
}

class VsRulesScenarioProvider : public BuildingPlacerScenarioProvider {
 public:
  VsRulesScenarioProvider(std::string mapPool)
      : BuildingPlacerScenarioProvider(std::move(mapPool)) {
    setMaxFrames(kMaxFrames);
  }

  virtual std::pair<std::shared_ptr<BasePlayer>, std::shared_ptr<BasePlayer>>
  startNewScenario(
      const std::function<void(BasePlayer*)>& setup1,
      const std::function<void(BasePlayer*)>& setup2) override {
    map_ = selectMap(mapPool_);
    loadMap<Player>(
        map_,
        tc::BW::Race::Zerg,
        tc::BW::Race::Zerg,
        GameType::Melee,
        replayPath_);

    game_ = nullptr;
    player1_ = nullptr;
    player2_ = nullptr;

    setupLearningPlayer(player1_.get());
    setupRuleBasedPlayer(player2_.get());

    // Set a fixed build for both players
    build1_ = selectBuild(FLAGS_build);
    build2_ = build1_;
    player1_->state()->board()->post(Blackboard::kBuildOrderKey, build1_);
    player2_->state()->board()->post(Blackboard::kBuildOrderKey, build2_);

    // Finish with custom setup
    setup1(player1_.get());
    setup2(player2_.get());

    std::static_pointer_cast<Player>(player1_)->init();
    std::static_pointer_cast<Player>(player2_)->init();
    return std::make_pair(player1_, player2_);
  }
};

class SunkenPlacementScenarioProvider : public BuildingPlacerScenarioProvider {
 public:
  SunkenPlacementScenarioProvider(std::string mapPool)
      : BuildingPlacerScenarioProvider(std::move(mapPool)) {
    setMaxFrames(kMaxFrames);
  }

  virtual std::pair<std::shared_ptr<BasePlayer>, std::shared_ptr<BasePlayer>>
  startNewScenario(
      const std::function<void(BasePlayer*)>& setup1,
      const std::function<void(BasePlayer*)>& setup2) override {
    game_ = nullptr;
    player1_ = nullptr;
    player2_ = nullptr;

    map_ = selectMap(mapPool_);
    loadMap<Player>(
        map_,
        tc::BW::Race::Zerg,
        tc::BW::Race::Zerg,
        GameType::Melee,
        replayPath_);

    setupLearningPlayer(player1_.get());
    setupRuleBasedPlayer(player2_.get());

    // Set a fixed build for both players
    build1_ = "9poolspeedlingmutacustom";
    build2_ = "10hatchlingcustom";
    player1_->state()->board()->post(Blackboard::kBuildOrderKey, build1_);
    player2_->state()->board()->post(Blackboard::kBuildOrderKey, build2_);

    // Finish with custom setup
    setup1(player1_.get());
    setup2(player2_.get());

    std::static_pointer_cast<Player>(player1_)->init();
    std::static_pointer_cast<Player>(player2_)->init();
    return std::make_pair(player1_, player2_);
  }
};

} // namespace

std::unique_ptr<BuildingPlacerScenarioProvider> makeBPRLScenarioProvider(
    std::string const& name,
    std::string const& maps,
    bool gui) {
  if (name == "vsrules") {
    auto scenarioProvider = std::make_unique<VsRulesScenarioProvider>(maps);
    scenarioProvider->setGui(gui);
    return scenarioProvider;
  } else if (name == "sunkenplacement") {
    auto scenarioProvider =
        std::make_unique<SunkenPlacementScenarioProvider>(maps);
    scenarioProvider->setGui(gui);
    return scenarioProvider;
  } else {
    throw std::runtime_error("Unsupported scenario " + name);
  }
}

} // namespace cherrypi
