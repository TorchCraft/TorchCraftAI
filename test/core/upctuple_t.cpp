/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "replayer.h"
#include "state.h"
#include "unitsinfo.h"
#include "upc.h"

using namespace cherrypi;

CASE("upctuple/position_argmax") {
  UPCTuple upc;

  EXPECT(upc.positionArgMax().first == kInvalidPosition);
  EXPECT(upc.positionArgMax().second == 0.0f);

  upc.position = Position(10, 10);
  EXPECT(upc.positionArgMax().first == Position(10, 10));
  EXPECT(upc.positionArgMax().second == 1.0f);
  upc.scale = 2;
  EXPECT(upc.positionArgMax().first == Position(20, 20));
  EXPECT(upc.positionArgMax().second == 1.0f);
  upc.scale = 1;

  Unit u1;
  u1.id = 1;
  u1.x = 1;
  u1.y = 1;
  Unit u2;
  u2.id = 2;
  u2.x = 2;
  u2.y = 2;
  upc.position = UPCTuple::UnitMap{{&u1, 0.4f}, {&u2, 0.6f}};
  EXPECT(upc.positionArgMax().first == Position(2, 2));
  EXPECT(upc.positionArgMax().second == 0.6f);

#ifdef HAVE_ATEN
  auto t = torch::zeros({64, 64});
  t[12][14] = 0.3f;
  t[20][30] = 0.3f;
  t[40][1] = 0.4f;
  upc.position = t;
  EXPECT(upc.positionArgMax().first == Position(1, 40));
  EXPECT(upc.positionArgMax().second == 0.4f);
  upc.scale = 4;
  EXPECT(upc.positionArgMax().first == Position(4, 160));
  EXPECT(upc.positionArgMax().second == 0.4f);
#endif // HAVE_ATEN
}

#ifdef HAVE_ATEN
CASE("upctuple/position_tensor") {
  // Load a replay so we'll have a representative bot state
  Replayer replay("test/maps/replays/TL_TvZ_IC420273.rep");
  replay.init();
  replay.step();
  State* state = replay.state();

  UPCTuple upc;
  {
    // Empty position == uniform
    auto t = upc.positionTensor(state);
    EXPECT(t.sum().item<float>() == 1.0f);
    EXPECT(t.min().item<float>() == t.max().item<float>());
  }

  upc.position = Position(10, 18);
  {
    auto t = upc.positionTensor(state);
    EXPECT(t.sum().item<float>() == 1.0f);
    EXPECT(t[18][10].item<float>() == 1.0f);
  }

  upc.scale = 4;
  {
    auto t = upc.positionTensor(state);
    EXPECT(t.sum().item<float>() == 1.0f);
    for (int i = 0; i < upc.scale; i++) {
      for (int j = 0; j < upc.scale; j++) {
        EXPECT(
            t[18 * upc.scale + i][10 * upc.scale + j].item<float>() ==
            1.0f / (upc.scale * upc.scale));
      }
    }
  }
  upc.scale = 1;

  auto& area = state->areaInfo().getArea(1);
  upc.position = &area;
  {
    auto t = upc.positionTensor(state);
    EXPECT(t.sum().item<float>() == lest::approx(1.0f));
    // area.size includes walkable tiles only, so the tensor should contain at
    // least as meany positions
    EXPECT(t.gt(0).sum().item<float>() >= area.size);
    // True for this area, at least
    auto c = Position(area.x, area.y);
    EXPECT(t[c.y][c.x].item<float>() > 0.0f);
  }

  Unit u1;
  u1.id = 1;
  u1.x = 1;
  u1.y = 1;
  Unit u2;
  u2.id = 2;
  u2.x = 2;
  u2.y = 2;
  upc.position = UPCTuple::UnitMap{{&u1, 0.4f}, {&u2, 0.6f}};
  {
    auto t = upc.positionTensor(state);
    EXPECT(t.sum().item<float>() == 1.0f);
    EXPECT(t[1][1].item<float>() == 0.4f);
    EXPECT(t[2][2].item<float>() == 0.6f);
  }
}
#endif // HAVE_ATEN
