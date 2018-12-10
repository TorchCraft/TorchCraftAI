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

#include <bwem/map.h>

using namespace cherrypi;

CASE("core/areaInfo/cache[hide]") {
  const std::string scmap = "maps/(4)Circuit Breaker.scx";

  std::unique_ptr<MeleeScenario> scenario;
  std::unique_ptr<Player> bot;

  scenario = std::make_unique<MeleeScenario>(scmap, "Zerg", "Terran");
  bot = std::make_unique<Player>(scenario->makeClient());
  bot->setWarnIfSlow(false);

  bot->addModule(Module::make<CreateGatherAttackModule>());
  bot->addModule(Module::make<UPCToCommandModule>());

  bot->init();
  bot->step();
  auto& areaInfo = bot->state()->areaInfo();
  auto* map = bot->state()->map();

  int mismatch = 0;
  // uncomment if you want to dump and inspect differences
  // std::ofstream f1("groundtruth.txt"), f2("ours.txt");
  for (int x = 0; x < map->WalkSize().x; ++x) {
    for (int y = 0; y < map->WalkSize().y; ++y) {
      auto curArea = map->GetArea(BWAPI::WalkPosition(x, y));
      if (curArea == nullptr) {
        curArea = map->GetNearestArea(BWAPI::WalkPosition(x, y));
      }

      auto ourArea = areaInfo.getArea(Position(x, y));
      // f1 << curArea->Id() << std::endl;
      // f2 << ourArea.id << std::endl;
      if (curArea->Id() != ourArea.id) {
        mismatch++;
      }
    }
  }
  double mismatchRate =
      double(mismatch) / double(map->WalkSize().x * map->WalkSize().y);

  // we can have a few mismatch, since tie-breaking is not canonical. We expect
  // the mismatch to be very low though.
  EXPECT(mismatchRate < 0.5 / 100.);
}
