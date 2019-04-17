/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "buildorders/base.h"
#include "fivepool.h"
#include "gameutils/game.h"
#include "modules.h"
#include "player.h"
#include "registry.h"
#include "test.h"
#include <glog/logging.h>

DECLARE_string(build);
DECLARE_double(rtfactor);
using namespace cherrypi;

namespace {
auto constexpr kBuildOrderFirst = "test1";
auto constexpr kBuildOrderSecond = "test2";
auto constexpr kBuildOrderChangeAtFrame = 1000;

class ABBOTest1 : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;
  virtual void preBuild2(autobuild::BuildState& st) override {}
  virtual void buildStep2(autobuild::BuildState& st) override {}
};
class ABBOTest2 : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;
  virtual void preBuild2(autobuild::BuildState& st) override {}
  virtual void buildStep2(autobuild::BuildState& st) override {}
};
REGISTER_SUBCLASS_3(ABBOBase, ABBOTest1, UpcId, State*, Module*);
REGISTER_SUBCLASS_3(ABBOBase, ABBOTest2, UpcId, State*, Module*);
} // namespace

class GenericAutobuildTestUtils {
 public:
  static auto createMyPlayer(
      GameMultiPlayer* scenario,
      std::function<void(State*)> init_fn = nullptr) {
    auto bot = std::make_shared<Player>(scenario->makeClient1());
    if (init_fn) {
      init_fn(bot->state());
    }
    bot->setRealtimeFactor(FLAGS_rtfactor);
    bot->addModule(Module::make<CreateGatherAttackModule>());
    bot->addModule(Module::make<StrategyModule>());
    bot->addModule(Module::make<GenericAutoBuildModule>());
    bot->addModule(Module::make<BuildingPlacerModule>());
    bot->addModule(Module::make<BuilderModule>());
    bot->addModule(Module::make<GathererModule>());
    bot->addModule(Module::make<UPCToCommandModule>());
    bot->init();
    return bot;
  }

  static auto createEnemyPlayer(GameMultiPlayer* scenario) {
    auto bot = std::make_shared<Player>(scenario->makeClient2());
    bot->setRealtimeFactor(FLAGS_rtfactor);
    bot->init();
    return bot;
  }

  template <typename T>
  static bool hasGenericAutoBuildSubtask(std::shared_ptr<BasePlayer> player) {
    std::shared_ptr<GenericAutoBuildModule> module =
        player->findModule<GenericAutoBuildModule>();
    for (auto task : player->state()->board()->tasksOfModule(module.get())) {
      if (std::dynamic_pointer_cast<T>(task)) {
        return true;
      }
    }
    return false;
  }
};

SCENARIO("genericautobuild") {
  GIVEN("build order initialized with kBuildOrderKey") {
    auto scenario = GameMultiPlayer(
        "maps/(4)Fighting Spirit.scx", tc::BW::Race::Zerg, tc::BW::Race::Zerg);

    auto ourBot =
        GenericAutobuildTestUtils::createMyPlayer(&scenario, [](State* s) {
          s->board()->post(
              Blackboard::kBuildOrderKey, std::string(kBuildOrderSecond));
        });
    auto theirBot = GenericAutobuildTestUtils::createEnemyPlayer(&scenario);
    auto ourBoard = ourBot->state()->board();
    ourBot->step();
    theirBot->step();
    EXPECT(ourBoard->hasKey(Blackboard::kBuildOrderKey));
    EXPECT(
        ourBoard->get<std::string>(Blackboard::kBuildOrderKey) ==
        kBuildOrderSecond);
    EXPECT(GenericAutobuildTestUtils::hasGenericAutoBuildSubtask<ABBOTest2>(
        ourBot));
  }

  GIVEN("build order initialized with kOpeningBuildOrderKey") {
    auto scenario = GameMultiPlayer(
        "maps/(4)Fighting Spirit.scx", tc::BW::Race::Zerg, tc::BW::Race::Zerg);

    auto ourBot =
        GenericAutobuildTestUtils::createMyPlayer(&scenario, [](State* s) {
          s->board()->post(
              Blackboard::kOpeningBuildOrderKey,
              std::string(kBuildOrderSecond));
        });
    auto theirBot = GenericAutobuildTestUtils::createEnemyPlayer(&scenario);
    auto ourBoard = ourBot->state()->board();
    ourBot->step();
    theirBot->step();
    EXPECT(ourBoard->hasKey(Blackboard::kBuildOrderKey));
    EXPECT(
        ourBoard->get<std::string>(Blackboard::kBuildOrderKey) ==
        kBuildOrderSecond);
    EXPECT(GenericAutobuildTestUtils::hasGenericAutoBuildSubtask<ABBOTest2>(
        ourBot));
  }

  GIVEN("A blank state") {
    auto scenario = GameMultiPlayer(
        "maps/(4)Fighting Spirit.scx", tc::BW::Race::Zerg, tc::BW::Race::Zerg);

    auto ourBot =
        GenericAutobuildTestUtils::createMyPlayer(&scenario, [](State* s) {
          s->board()->post(
              Blackboard::kBuildOrderKey, std::string(kBuildOrderFirst));
        });
    auto theirBot = GenericAutobuildTestUtils::createEnemyPlayer(&scenario);
    auto ourState = ourBot->state();
    auto ourBoard = ourState->board();
    while (!ourState->gameEnded() &&
           ourState->currentFrame() < kBuildOrderChangeAtFrame) {
      ourBot->step();
      theirBot->step();
    }
    WHEN("the game has been initialized with a first build order") {
      EXPECT(!ourState->gameEnded());
      EXPECT(ourBoard->hasKey(Blackboard::kBuildOrderKey));
      EXPECT(
          ourBoard->get<std::string>(Blackboard::kBuildOrderKey) ==
          kBuildOrderFirst);
      EXPECT(GenericAutobuildTestUtils::hasGenericAutoBuildSubtask<ABBOTest1>(
          ourBot));
    }

    WHEN("the build order has changed") {
      ourBoard->post(
          Blackboard::kBuildOrderKey, std::string(kBuildOrderSecond));
      while (!ourState->gameEnded() &&
             ourState->currentFrame() < (kBuildOrderChangeAtFrame * 2)) {
        ourBot->step();
        theirBot->step();
      }

      EXPECT(!ourState->gameEnded());
      EXPECT(ourBoard->hasKey(Blackboard::kBuildOrderKey));
      EXPECT(
          ourBoard->get<std::string>(Blackboard::kBuildOrderKey) ==
          kBuildOrderSecond);
      EXPECT(GenericAutobuildTestUtils::hasGenericAutoBuildSubtask<ABBOTest2>(
          ourBot));
      EXPECT(!GenericAutobuildTestUtils::hasGenericAutoBuildSubtask<ABBOTest1>(
          ourBot));
    }
  }
}
