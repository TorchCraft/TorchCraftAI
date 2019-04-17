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

using namespace cherrypi;

SCENARIO("builder/accepted_upcs") {
  GIVEN("A blank state") {
    State state(std::make_shared<tc::Client>());
    auto board = state.board();
    auto top = Module::make<TopModule>();
    auto builder = Module::make<BuilderModule>();

    WHEN("A non-{Create,SetCreatePriority} UPC is posted") {
      for (int i = 0; i < numUpcCommands(); i++) {
        Command c = static_cast<Command>(1 << i);
        if (c == Command::Create || c == Command::SetCreatePriority) {
          continue;
        }

        auto upc = std::make_shared<UPCTuple>();
        upc->command[c] = 1;
        auto id = board->postUPC(std::move(upc), kRootUpcId, top.get());

        builder->step(&state);
        // THEN it's not consumed
        EXPECT(board->upcsFrom(top.get()).size() == 1u);
        board->consumeUPC(id, top.get());
      }
    }

    WHEN("A UPC with a non-sharp create command is posted") {
      auto upc = std::make_shared<UPCTuple>();
      upc->command[Command::Create] = 0.5;
      upc->command[Command::Move] = 0.5;
      auto id = board->postUPC(std::move(upc), kRootUpcId, top.get());
      builder->step(&state);
      // THEN it's not consumed
      EXPECT(board->upcsFrom(top.get()).size() == 1u);
      board->consumeUPC(id, top.get());
      EXPECT(board->upcsFrom(top.get()).size() == 0u);
    }

    WHEN("A UPC with a non-sharp create type is posted") {
      auto upc = std::make_shared<UPCTuple>();
      upc->command[Command::Create] = 1;
      upc->state = UPCTuple::BuildTypeMap{{buildtypes::Zerg_Drone, 0.5f},
                                          {buildtypes::Zerg_Zergling, 0.5f}};
      auto id = board->postUPC(std::move(upc), kRootUpcId, top.get());
      builder->step(&state);
      // THEN it is not consumed
      EXPECT(board->upcsFrom(top.get()).size() == 1u);
      board->consumeUPC(id, top.get());
      EXPECT(board->upcsFrom(top.get()).size() == 0u);
    }

    WHEN("A create UPC for an upgrade is posted") {
      for (auto const* bt : buildtypes::allUpgradeTypes) {
        auto upc = std::make_shared<UPCTuple>();
        upc->command[Command::Create] = 1;
        upc->state = UPCTuple::BuildTypeMap{{bt, 1}};
        board->postUPC(std::move(upc), kRootUpcId, top.get());

        builder->step(&state);
        // THEN it is consumed, even though there's no builder
        EXPECT(board->upcsFrom(top.get()).size() == 0u);
      }
    }

    WHEN("A create UPC for a tech is posted") {
      for (auto const* bt : buildtypes::allTechTypes) {
        auto upc = std::make_shared<UPCTuple>();
        upc->command[Command::Create] = 1;
        upc->state = UPCTuple::BuildTypeMap{{bt, 1}};
        EXPECT(board->upcsFrom(top.get()).size() == 0u);
        auto id = board->postUPC(std::move(upc), kRootUpcId, top.get());
        EXPECT(board->upcsFrom(top.get()).size() == 1u);

        builder->step(&state);
        if (bt->builder != nullptr) {
          // THEN it is consumed when we know how to build it
          EXPECT(board->upcsFrom(top.get()).size() == 0u);
        } else {
          // THEN it is not consumed if we don't know how to build i
          EXPECT(board->upcsFrom(top.get()).size() == 1u);
          board->consumeUPC(id, top.get());
        }
      }
    }

    WHEN(
        "A create UPC for a building with a sharp position is posted that "
        "requires a worker") {
      for (auto const* bt : buildtypes::allUnitTypes) {
        if (!bt->isBuilding || bt->builder == nullptr ||
            !bt->builder->isWorker) {
          continue;
        }

        auto upc = std::make_shared<UPCTuple>();
        upc->command[Command::Create] = 1;
        upc->state = UPCTuple::BuildTypeMap{{bt, 1}};
        upc->position = Position(10, 10);
        board->postUPC(std::move(upc), kRootUpcId, top.get());

        builder->step(&state);
        // THEN it is consumed
        EXPECT(board->upcsFrom(top.get()).size() == 0u);
      }
    }

    WHEN(
        "A create UPC for a building without a sharp position is posted that "
        "requires a worker") {
      for (auto const* bt : buildtypes::allUnitTypes) {
        if (!bt->isBuilding || bt->builder == nullptr ||
            !bt->builder->isWorker) {
          continue;
        }

        auto upc = std::make_shared<UPCTuple>();
        upc->command[Command::Create] = 1;
        upc->state = UPCTuple::BuildTypeMap{{bt, 1}};
        auto id = board->postUPC(std::move(upc), kRootUpcId, top.get());

        builder->step(&state);
        // THEN it is not consumed
        EXPECT(board->upcsFrom(top.get()).size() == 1u);
        board->consumeUPC(id, top.get());
      }
    }

    WHEN("The position is dirac on a unit") {
      Unit unit;
      auto upc = std::make_shared<UPCTuple>();
      upc->command[Command::Create] = 1;
      upc->state = UPCTuple::BuildTypeMap{{buildtypes::Zerg_Extractor, 1}};
      upc->position = UPCTuple::UnitMap{{&unit, 1}};
      board->postUPC(std::move(upc), kRootUpcId, top.get());

      builder->step(&state);
      // THEN it is consumed
      EXPECT(board->upcsFrom(top.get()).size() == 0u);
    }

    WHEN("The position is an Area") {
      Area area;
      auto upc = std::make_shared<UPCTuple>();
      upc->command[Command::Create] = 1;
      upc->state = UPCTuple::BuildTypeMap{{buildtypes::Zerg_Extractor, 1}};
      upc->position = &area;
      auto id = board->postUPC(std::move(upc), kRootUpcId, top.get());

      builder->step(&state);
      // THEN it is not consumed since we don't regard areas as dirac
      EXPECT(board->upcsFrom(top.get()).size() == 1u);
      board->consumeUPC(id, top.get());
    }

#ifdef HAVE_ATEN
    WHEN("The position is a non-dirac tensor") {
      auto upc = std::make_shared<UPCTuple>();
      upc->command[Command::Create] = 1;
      upc->state = UPCTuple::BuildTypeMap{{buildtypes::Zerg_Extractor, 1}};
      upc->position = torch::Tensor(torch::zeros({10, 10}).fill_(0.01f));
      auto id = board->postUPC(std::move(upc), kRootUpcId, top.get());

      builder->step(&state);
      // THEN it is not consumed
      EXPECT(board->upcsFrom(top.get()).size() == 1u);
      board->consumeUPC(id, top.get());
    }

    WHEN("The position is a dirac tensor") {
      auto upc = std::make_shared<UPCTuple>();
      upc->command[Command::Create] = 1;
      upc->state = UPCTuple::BuildTypeMap{{buildtypes::Zerg_Extractor, 1}};
      auto t = torch::zeros({10, 10});
      t[1][1] = 1;
      upc->position = t;
      board->postUPC(std::move(upc), kRootUpcId, top.get());

      builder->step(&state);
      // THEN it is consumed
      EXPECT(board->upcsFrom(top.get()).size() == 0u);
    }
#endif // HAVE_ATEN
  }
}
