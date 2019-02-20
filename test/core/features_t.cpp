/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "common/serialization.h"
#include "features/defoggerfeatures.h"
#include "features/features.h"
#include "features/unitsfeatures.h"
#include "replayer.h"
#include "state.h"
#include "utils.h"

#include <visdom/visdom.h>
// This has the serialization code for tensors
#include <autogradpp/autograd.h>

using namespace cherrypi;
using namespace visdom;

namespace {
std::string const kDefaultReplay = "test/maps/replays/TL_TvZ_IC420273.rep";

std::unique_ptr<Replayer> replayTo(
    FrameNum frame,
    std::string path = kDefaultReplay) {
  auto replay = std::make_unique<Replayer>(path);
  replay->setPerspective(0);
  replay->init();
  while (replay->state()->currentFrame() < frame) {
    replay->step();
  }
  return replay;
}
} // namespace

CASE("features/bounding_box") {
  auto replay = replayTo(10);
  auto* state = replay->state();

  int mapW = state->mapWidth();
  int mapH = state->mapHeight();
  auto f1 = featurizePlain(state, {PlainFeatureType::GroundHeight});
  EXPECT(f1.offset.x == 0);
  EXPECT(f1.offset.y == 0);
  EXPECT(f1.tensor.sizes().vec() == std::vector<int64_t>({1, mapH, mapW}));
  auto f2 =
      featurizePlain(state, {PlainFeatureType::GroundHeight}, state->mapRect());
  EXPECT(f2.offset.x == 0);
  EXPECT(f2.offset.y == 0);
  EXPECT(f2.tensor.sizes().vec() == std::vector<int64_t>({1, mapH, mapW}));
  auto f3 = featurizePlain(
      state, {PlainFeatureType::GroundHeight}, Rect(10, 10, 100, 98));
  EXPECT(f3.offset.x == 10);
  EXPECT(f3.offset.y == 10);
  EXPECT(f3.tensor.sizes().vec() == std::vector<int64_t>({1, 98, 100}));
  auto f4 = featurizePlain(
      state,
      {PlainFeatureType::GroundHeight},
      Rect::centeredWithSize(state->mapRect().center(), 613, 1024));
  EXPECT(f4.offset.x == -50);
  EXPECT(f4.offset.y == -256);
  EXPECT(f4.tensor.sizes().vec() == std::vector<int64_t>({1, 1024, 613}));
  auto f5 = featurizePlain(
      state, {PlainFeatureType::GroundHeight}, Rect(-10, 410, 30, 10));
  EXPECT(f5.offset.x == -10);
  EXPECT(f5.offset.y == 410);
  EXPECT(f5.tensor.sizes().vec() == std::vector<int64_t>({1, 10, 30}));

  auto f6 =
      featurizePlain(state, {PlainFeatureType::FogOfWar}, state->mapRect());
  EXPECT(f6.tensor.sizes().vec() == std::vector<int64_t>({1, 512, 512}));

  auto f7 = featurizePlain(
      state, {PlainFeatureType::FogOfWar}, Rect(100, 100, 500, 500));
  EXPECT(f7.offset.x == 100);
  EXPECT(f7.offset.y == 100);
  EXPECT(f7.tensor.sizes().vec() == std::vector<int64_t>({1, 500, 500}));

  /*
  // Uncomment to verify visually
  Visdom vs;
  vs.heatmap(f1.tensor[0], makeOpts({{"title", "f1"}}));
  vs.heatmap(f2.tensor[0], makeOpts({{"title", "f2"}}));
  vs.heatmap(f3.tensor[0], makeOpts({{"title", "f3"}}));
  vs.heatmap(f4.tensor[0], makeOpts({{"title", "f4"}}));
  vs.heatmap(f5.tensor[0], makeOpts({{"title", "f5"}}));
  vs.heatmap(f6.tensor[0], makeOpts({{"title", "f6"}}));
  vs.heatmap(f7.tensor[0], makeOpts({{"title", "f7"}}));
  */
}

CASE("features/content") {
  auto replay = replayTo(20);
  auto* state = replay->state();

  FeatureData f;
  auto* tcs = state->tcstate();

  f = featurizePlain(state, {PlainFeatureType::GroundHeight});
  EXPECT(
      std::accumulate(
          tcs->ground_height_data.begin(),
          tcs->ground_height_data.end(),
          0.0f,
          [](float a, uint8_t b) { return a + b / 2; }) ==
      f.tensor.sum().item<float>());
  EXPECT(f.tensor.min().item<float>() >= 0.0f);
  EXPECT(f.tensor.max().item<float>() <= 2.0f);

  f = featurizePlain(state, {PlainFeatureType::Walkability});
  EXPECT(
      std::accumulate(
          tcs->walkable_data.begin(), tcs->walkable_data.end(), 0.0f) ==
      f.tensor.sum().item<float>());

  f = featurizePlain(state, {PlainFeatureType::Buildability});
  EXPECT(
      std::accumulate(
          tcs->buildable_data.begin(), tcs->buildable_data.end(), 0.0f) ==
      f.tensor.sum().item<float>());

  auto& tinfo = state->tilesInfo();
  std::vector<std::reference_wrapper<Tile>> usedTiles;
  size_t stride = TilesInfo::tilesWidth - tinfo.mapTileWidth();
  Tile* ptr = tinfo.tiles.data();
  for (unsigned tileY = 0; tileY != tinfo.mapTileHeight();
       ++tileY, ptr += stride) {
    for (unsigned tileX = 0; tileX != tinfo.mapTileWidth(); ++tileX, ++ptr) {
      usedTiles.push_back(*ptr);
    }
  }

  f = featurizePlain(state, {PlainFeatureType::FogOfWar});
  EXPECT(
      std::accumulate(
          usedTiles.begin(),
          usedTiles.end(),
          0.0f,
          // 16 walk tiles per build tile
          [](float i, Tile& tile) { return i + (tile.visible ? 0 : 16); }) ==
      f.tensor.sum().item<float>());

  f = featurizePlain(state, {PlainFeatureType::Creep});
  EXPECT(
      std::accumulate(
          usedTiles.begin(),
          usedTiles.end(),
          0.0f,
          // 16 walk tiles per build tile
          [](float i, Tile& tile) { return i + (tile.hasCreep ? 16 : 0); }) ==
      f.tensor.sum().item<float>());

  f = featurizePlain(state, {PlainFeatureType::TallDoodad});
  EXPECT(
      std::accumulate(
          tcs->ground_height_data.begin(),
          tcs->ground_height_data.end(),
          0.0f,
          [](float a, uint8_t b) {
            // Uneven ground height indicates doodad locations
            return a + b % 2;
          }) == f.tensor.sum().item<float>());

  auto& uinfo = state->unitsInfo();
  auto uaf = UnitPresenceFeaturizer();
  f = uaf.toSpatialFeature(uaf.extract(state));
  // Default pooling is sum-pooling
  EXPECT(float(uinfo.liveUnits().size()) == f.tensor.sum().item<float>());
  EXPECT(f.desc[0].type == CustomFeatureType::UnitPresence);

  f = uaf.toSpatialFeature(uaf.extract(state), SubsampleMethod::Max);
  std::set<std::pair<int, int>> lives;
  for (auto* u : uinfo.liveUnits()) {
    lives.insert(std::make_pair(u->x, u->y));
  }
  EXPECT(float(lives.size()) == f.tensor.sum().item<float>());

  f = uaf.toSpatialFeature(uaf.extract(state, uinfo.myUnits()));
  EXPECT(float(uinfo.myUnits().size()) == f.tensor.sum().item<float>());

  auto filter = [](Unit* u) -> bool {
    return u->visible && u->isMine && u->powered();
  };
  f = uaf.toSpatialFeature(uaf.extract(state, filter));
  EXPECT(float(uinfo.myUnits().size()) == f.tensor.sum().item<float>());

  f = uaf.toSpatialFeature(uaf.extract(state, uinfo.enemyUnits()));
  EXPECT(float(uinfo.enemyUnits().size()) == f.tensor.sum().item<float>());

  // "EnemyUnits" above was already empty, but let's be sure that it works
  f = uaf.toSpatialFeature(uaf.extract(state, UnitsInfo::Units()));
  EXPECT(float(0) == f.tensor.sum().item<float>());

  // With existing feature
  uaf.toSpatialFeature(&f, uaf.extract(state, uinfo.myUnits()));
  EXPECT(float(uinfo.myUnits().size()) == f.tensor.sum().item<float>());
  uaf.toSpatialFeature(&f, uaf.extract(state, UnitsInfo::Units()));
  EXPECT(float(0) == f.tensor.sum().item<float>());

  f = uaf.toSpatialFeature(
      uaf.extract(state, uinfo.neutralUnits()), SubsampleMethod::Max);
  // Here, we have some neutrals stacked on the same walk tile
  std::set<std::pair<int, int>> neutrals;
  for (auto* u : uinfo.neutralUnits()) {
    neutrals.insert(std::make_pair(u->x, u->y));
  }
  EXPECT(float(neutrals.size()) == f.tensor.sum().item<float>());
}

CASE("features/content_nonsquare") {
  auto replay = replayTo(20, "test/maps/replays/bwrep_gyvu8.rep");
  auto* state = replay->state();

  FeatureData f;
  auto* tcs = state->tcstate();

  f = featurizePlain(state, {PlainFeatureType::GroundHeight});
  EXPECT(
      std::accumulate(
          tcs->ground_height_data.begin(),
          tcs->ground_height_data.end(),
          0.0f,
          [](float a, uint8_t b) { return a + b / 2; }) ==
      f.tensor.sum().item<float>());

  f = featurizePlain(state, {PlainFeatureType::Walkability});
  EXPECT(
      std::accumulate(
          tcs->walkable_data.begin(), tcs->walkable_data.end(), 0.0f) ==
      f.tensor.sum().item<float>());

  f = featurizePlain(state, {PlainFeatureType::Buildability});
  EXPECT(
      std::accumulate(
          tcs->buildable_data.begin(), tcs->buildable_data.end(), 0.0f) ==
      f.tensor.sum().item<float>());

  auto& tinfo = state->tilesInfo();
  std::vector<std::reference_wrapper<Tile>> usedTiles;
  size_t stride = TilesInfo::tilesWidth - tinfo.mapTileWidth();
  Tile* ptr = tinfo.tiles.data();
  for (unsigned tileY = 0; tileY != tinfo.mapTileHeight();
       ++tileY, ptr += stride) {
    for (unsigned tileX = 0; tileX != tinfo.mapTileWidth(); ++tileX, ++ptr) {
      usedTiles.push_back(*ptr);
    }
  }

  f = featurizePlain(state, {PlainFeatureType::FogOfWar});
  EXPECT(
      std::accumulate(
          usedTiles.begin(),
          usedTiles.end(),
          0.0f,
          // 16 walk tiles per build tile
          [](float i, Tile& tile) { return i + (tile.visible ? 0 : 16); }) ==
      f.tensor.sum().item<float>());

  f = featurizePlain(state, {PlainFeatureType::Creep});
  EXPECT(
      std::accumulate(
          usedTiles.begin(),
          usedTiles.end(),
          0.0f,
          // 16 walk tiles per build tile
          [](float i, Tile& tile) { return i + (tile.hasCreep ? 16 : 0); }) ==
      f.tensor.sum().item<float>());

  f = featurizePlain(state, {PlainFeatureType::TallDoodad});
  EXPECT(
      std::accumulate(
          tcs->ground_height_data.begin(),
          tcs->ground_height_data.end(),
          0.0f,
          [](float a, uint8_t b) {
            // Uneven ground height indicates doodad locations
            return a + b % 2;
          }) == f.tensor.sum().item<float>());

  auto& uinfo = state->unitsInfo();
  auto uaf = UnitPresenceFeaturizer();
  f = uaf.toSpatialFeature(uaf.extract(state));
  // Default is sum
  EXPECT(float(uinfo.liveUnits().size()) == f.tensor.sum().item<float>());

  f = uaf.toSpatialFeature(uaf.extract(state), SubsampleMethod::Max);
  std::set<std::pair<int, int>> lives;
  for (auto* u : uinfo.liveUnits()) {
    lives.insert(std::make_pair(u->x, u->y));
  }
  EXPECT(float(lives.size()) == f.tensor.sum().item<float>());

  f = uaf.toSpatialFeature(uaf.extract(state, uinfo.myUnits()));
  EXPECT(float(uinfo.myUnits().size()) == f.tensor.sum().item<float>());

  auto filter = [](Unit* u) -> bool {
    return u->visible && u->isMine && u->powered();
  };
  f = uaf.toSpatialFeature(uaf.extract(state, filter));
  EXPECT(float(uinfo.myUnits().size()) == f.tensor.sum().item<float>());

  f = uaf.toSpatialFeature(uaf.extract(state, uinfo.enemyUnits()));
  EXPECT(float(uinfo.enemyUnits().size()) == f.tensor.sum().item<float>());

  f = uaf.toSpatialFeature(
      uaf.extract(state, uinfo.neutralUnits()), SubsampleMethod::Max);
  // Here, we have some neutrals stacked on the same walk tile
  std::set<std::pair<int, int>> neutrals;
  for (auto* u : uinfo.neutralUnits()) {
    neutrals.insert(std::make_pair(u->x, u->y));
  }
  EXPECT(float(neutrals.size()) == f.tensor.sum().item<float>());
}

CASE("features/unit_position_mapping") {
  auto replay = replayTo(20);
  auto* state = replay->state();
  auto& uinfo = state->unitsInfo();

  auto uaf = UnitPresenceFeaturizer();
  FeatureData f = uaf.toSpatialFeature(uaf.extract(state, uinfo.myUnits()));
  // Not true generally, but works here
  EXPECT(float(uinfo.myUnits().size()) == f.tensor.sum().item<float>());
  EXPECT(f.tensor.sizes().vec() == std::vector<int64_t>({1u, 512u, 512u}));
  for (auto* unit : uinfo.myUnits()) {
    EXPECT(f.tensor[0][unit->y][unit->x].item<float>() == 1.0f);
  }

  auto bbx = Rect(431, 202, 24, 9);
  f = uaf.toSpatialFeature(uaf.extract(state, uinfo.myUnits(), bbx));
  auto i = 0;
  EXPECT(f.tensor.sizes().vec() == std::vector<int64_t>({1u, 9u, 24u}));
  for (auto* unit : uinfo.myUnits()) {
    if (bbx.contains(unit->pos())) {
      EXPECT(
          f.tensor[0][unit->y - bbx.y][unit->x - bbx.x].item<float>() == 1.0f);
      i++;
    }
  }
  EXPECT(i == f.tensor.sum().item<float>());

  bbx = Rect(444, 202, 24, 9);
  f = uaf.toSpatialFeature(uaf.extract(state, uinfo.myUnits(), bbx));
  i = 0;
  EXPECT(f.tensor.sizes().vec() == std::vector<int64_t>({1u, 9u, 24u}));
  for (auto* unit : uinfo.myUnits()) {
    if (bbx.contains(unit->pos())) {
      EXPECT(
          f.tensor[0][unit->y - bbx.y][unit->x - bbx.x].item<float>() == 1.0f);
      i++;
    }
  }
  EXPECT(i == f.tensor.sum().item<float>());

  bbx = Rect::centeredWithSize(state->mapRect().center(), 1000, 1000);
  f = uaf.toSpatialFeature(uaf.extract(state, uinfo.myUnits(), bbx));
  EXPECT(float(uinfo.myUnits().size()) == f.tensor.sum().item<float>());
  EXPECT(f.tensor.sizes().vec() == std::vector<int64_t>({1u, 1000u, 1000u}));
  for (auto* unit : uinfo.myUnits()) {
    EXPECT(f.tensor[0][unit->y - bbx.y][unit->x - bbx.x].item<float>() == 1.0f);
  }

  f = uaf.toSpatialFeature(
      uaf.extract(state, uinfo.myUnits(), Rect(0, 202, 24, 9)));
  EXPECT(0.0f == f.tensor.sum().item<float>());
  EXPECT(f.tensor.sizes().vec() == std::vector<int64_t>({1u, 9u, 24u}));
}

CASE("features/extract_select") {
  auto replay = replayTo(10);
  auto* state = replay->state();

  SETUP("") {
    auto fm = featurizePlain(
        state,
        {PlainFeatureType::GroundHeight,
         PlainFeatureType::FogOfWar,
         PlainFeatureType::UserFeature2});
    EXPECT(fm.desc.size() == 3u);
    EXPECT(fm.tensor.size(0) == 4u);
    EXPECT(fm.tensor.sizes().vec() == std::vector<int64_t>({4u, 512, 512}));

    SECTION("Feature is not present") {
      auto f = selectFeatures(fm, {PlainFeatureType::Walkability});
      EXPECT(f.desc.size() == 0u);
      EXPECT(f.tensor.defined() == false);
    }

    SECTION("Feature is not present 2") {
      auto f = selectFeatures(fm, {CustomFeatureType::UnitType});
      EXPECT(f.desc.size() == 0u);
      EXPECT(f.tensor.defined() == false);
    }

    SECTION("Feature is present") {
      auto f = selectFeatures(fm, {PlainFeatureType::UserFeature2});
      EXPECT(f.desc.size() == 1u);
      EXPECT(f.tensor.sizes().vec() == std::vector<int64_t>({2u, 512, 512}));
    }

    SECTION("Select feature twice") {
      auto f = selectFeatures(
          fm, {PlainFeatureType::GroundHeight, PlainFeatureType::GroundHeight});
      EXPECT(f.desc.size() == 2u);
      EXPECT(f.tensor.sizes().vec() == std::vector<int64_t>({2, 512, 512}));
      EXPECT(f.tensor[0].equal(f.tensor[1]));
    }
  }
}

CASE("features/subsample") {
  auto replay = replayTo(20);
  auto* state = replay->state();

  SETUP("") {
    SECTION("Offset retained") {
      auto f = featurizePlain(
          state, {PlainFeatureType::GroundHeight}, Rect(10, 10, 200, 200));
      auto sub = subsampleFeature(f, SubsampleMethod::Sum, 2);
      EXPECT(sub.scale == f.scale * 2);
      EXPECT(sub.offset == f.offset);
      EXPECT(sub.boundingBox() == f.boundingBox()); // this is in walktiles
      EXPECT(sub.tensor.sizes().vec() == std::vector<int64_t>({1, 100, 100}));
    }
  }

  SETUP("") {
    auto f = featurizePlain(
        state, {PlainFeatureType::FogOfWar, PlainFeatureType::GroundHeight});

    SECTION("Sum") {
      auto factor = tc::BW::XYWalktilesPerBuildtile;
      auto sub = subsampleFeature(f, SubsampleMethod::Sum, factor);
      EXPECT(
          sub.tensor.sizes().vec() ==
          std::vector<int64_t>({f.tensor.size(0),
                                f.tensor.size(1) / factor,
                                f.tensor.size(2) / factor}));
      EXPECT(sub.tensor.sum().item<float>() == f.tensor.sum().item<float>());
      EXPECT(sub.desc == f.desc);
    }

    SECTION("Average") {
      auto factor = tc::BW::XYWalktilesPerBuildtile;
      auto sub = subsampleFeature(f, SubsampleMethod::Average, factor);
      EXPECT(
          sub.tensor.sizes().vec() ==
          std::vector<int64_t>({f.tensor.size(0),
                                f.tensor.size(1) / factor,
                                f.tensor.size(2) / factor}));
      EXPECT(
          sub.tensor.sum().item<float>() ==
          f.tensor.sum().item<float>() / (factor * factor));
      EXPECT(sub.desc == f.desc);
    }

    SECTION("Max") {
      auto factor = tc::BW::XYWalktilesPerBuildtile;
      auto sub = subsampleFeature(f, SubsampleMethod::Max, factor);
      EXPECT(
          sub.tensor.sizes().vec() ==
          std::vector<int64_t>({f.tensor.size(0),
                                f.tensor.size(1) / factor,
                                f.tensor.size(2) / factor}));
      // The original FoW data is at build tile resolution, so max-pooling will
      // be identical to average-pooling here
      EXPECT(
          sub.tensor.sum().item<float>() ==
          f.tensor.sum().item<float>() / (factor * factor));
      EXPECT(sub.desc == f.desc);
    }

    SECTION("Stride of 1") {
      auto factor = tc::BW::XYWalktilesPerBuildtile;
      auto stride = 1;
      auto sub = subsampleFeature(f, SubsampleMethod::Average, factor, stride);
      auto size = f.tensor.size(1) - factor + 1;
      EXPECT(
          sub.tensor.sizes().vec() ==
          std::vector<int64_t>({f.tensor.size(0), size, size}));
    }

    SECTION("Stride < factor") {
      auto factor = tc::BW::XYWalktilesPerBuildtile;
      auto stride = factor / 2;
      auto sub = subsampleFeature(f, SubsampleMethod::Max, factor, stride);
      auto size = int(float(f.tensor.size(1) - factor + 1) / stride + 1);
      EXPECT(
          sub.tensor.sizes().vec() ==
          std::vector<int64_t>({f.tensor.size(0), size, size}));
    }

    SECTION("Stride > factor") {
      auto factor = tc::BW::XYWalktilesPerBuildtile;
      auto stride = factor + 5;
      auto sub = subsampleFeature(f, SubsampleMethod::Sum, factor, stride);
      auto size = int(float(f.tensor.size(1) - factor + 1) / stride + 1);
      EXPECT(
          sub.tensor.sizes().vec() ==
          std::vector<int64_t>({f.tensor.size(0), size, size}));
    }

    SECTION("Whole-map kernel") {
      auto factor = f.tensor.size(1);
      auto sub = subsampleFeature(f, SubsampleMethod::Sum, factor);
      EXPECT(
          sub.tensor.sizes().vec() ==
          std::vector<int64_t>({f.tensor.size(0), 1, 1}));
      EXPECT(sub.tensor.sum().item<float>() == f.tensor.sum().item<float>());
    }
  }
}

CASE("features/combine") {
  auto replay = replayTo(20);
  auto* state = replay->state();

  SETUP("") {
    // No need to re-setup Replayer for every section

    SECTION("Simple combination") {
      auto fg = featurizePlain(state, {PlainFeatureType::GroundHeight});
      auto fw = featurizePlain(state, {PlainFeatureType::Walkability});
      auto fgw = featurizePlain(
          state,
          {PlainFeatureType::GroundHeight, PlainFeatureType::Walkability});
      auto fgw2 = combineFeatures({fg, fw});
      EXPECT(fgw2.tensor.equal(fgw.tensor));
      EXPECT(fgw2.desc == fgw.desc);
      EXPECT(fgw2.scale == fgw.scale);
      EXPECT(fgw2.offset == fgw.offset);
    }

    SECTION("Different scales") {
      auto fg = featurizePlain(state, {PlainFeatureType::GroundHeight});
      auto fw = subsampleFeature(
          featurizePlain(state, {PlainFeatureType::Walkability}),
          SubsampleMethod::Average,
          tc::BW::XYWalktilesPerBuildtile);
      EXPECT_THROWS(combineFeatures({fg, fw}));
    }

    SECTION("Padding for different bounding boxes") {
      auto fg = featurizePlain(
          state, {PlainFeatureType::GroundHeight}, Rect(-10, -10, 100, 100));
      auto fw = featurizePlain(
          state, {PlainFeatureType::Walkability}, Rect(120, 120, 20, 20));
      auto fgw = combineFeatures({fg, fw});
      EXPECT(fgw.tensor.sizes().vec() == std::vector<int64_t>({2, 150, 150}));
      EXPECT(
          fgw.tensor[0].sum().item<float>() == fg.tensor.sum().item<float>());
      EXPECT(
          fgw.tensor[1].sum().item<float>() == fw.tensor.sum().item<float>());
      EXPECT(fgw.offset.x == -10);
      EXPECT(fgw.offset.y == -10);
    }
  }
}

CASE("features/serialization") {
  auto replay = replayTo(10);
  auto* state = replay->state();

  auto f = featurizePlain(
      state, {PlainFeatureType::Walkability, PlainFeatureType::FogOfWar});
  common::OMembuf ombuf;
  std::ostream os(&ombuf);
  {
    cereal::BinaryOutputArchive archive(os);
    EXPECT((archive(f), true));
  }
  os.flush();

  common::IMembuf imbuf(ombuf.data());
  std::istream is(&imbuf);
  FeatureData re;
  cereal::BinaryInputArchive archive(is);
  EXPECT((archive(re), true));

  EXPECT(re.tensor.equal(f.tensor));
  EXPECT(re.desc == f.desc);
  EXPECT(re.scale == f.scale);
  EXPECT(re.offset == f.offset);
}

CASE("features/unit_type_defogger") {
  auto replay = std::make_unique<Replayer>("test/maps/replays/bwrep_gyvu8.rep");
  auto* state = replay->state();
  std::deque<tc::Frame> frames;
  replay->setPerspective(0);
  replay->init();
  // Let's play for a while so that we have both allied and enemy units
  while (state->currentFrame() < 24 * 60 * 2.5) {
    replay->step();
    // Record all frames so that DefoggerFeaturizer does not forget any units.
    frames.push_back(state->tcstate()->frame);
  }

  auto& uinfo = state->unitsInfo();

  // Ground truth: defogger featurizer
  for (auto res : {16, 32, 64}) {
    for (auto stride : {16, 32, 64}) {
      auto combined = DefoggerFeaturizer::combine(frames, state->playerId());
      auto dfeat = DefoggerFeaturizer(res, res, stride, stride)
                       .featurize(
                           &combined,
                           state->mapWidth(),
                           state->mapHeight(),
                           state->playerId(),
                           at::kCPU)
                       .permute({2, 0, 1});

      auto udf = UnitTypeDefoggerFeaturizer();
      FeatureData f = udf.toDefoggerFeature(
          udf.extract(state, uinfo.liveUnits()), res, stride);
      EXPECT(f.scale == res);
      EXPECT(f.tensor.sizes().vec() == dfeat.sizes().vec());
      EXPECT(f.tensor.sum().item<float>() == dfeat.sum().item<float>());
      EXPECT(f.tensor.equal(dfeat));
    }
  }

  // Paddding via bounding box works
  auto res = 32;
  auto udf = UnitTypeDefoggerFeaturizer();
  FeatureData f1 =
      udf.toDefoggerFeature(udf.extract(state, uinfo.liveUnits()), res, res);
  auto bbox = Rect::centeredWithSize(state->mapRect().center(), 1024, 1024);
  FeatureData f2 = udf.toDefoggerFeature(
      udf.extract(state, uinfo.liveUnits(), bbox), res, res);
  EXPECT(state->mapWidth() == 384);
  EXPECT(state->mapHeight() == 512);
  EXPECT(
      f1.tensor.sizes().vec() ==
      std::vector<int64_t>(
          {UnitTypeDefoggerFeaturizer::kNumUnitTypes, 512 / res, 384 / res}));
  EXPECT(
      f2.tensor.sizes().vec() ==
      std::vector<int64_t>(
          {UnitTypeDefoggerFeaturizer::kNumUnitTypes, 1024 / res, 1024 / res}));
  EXPECT(f1.tensor.sum().item<float>() == f2.tensor.sum().item<float>());

  auto offsetY = (f2.tensor.size(1) - f1.tensor.size(1)) / 2;
  auto offsetX = (f2.tensor.size(2) - f1.tensor.size(2)) / 2;
  EXPECT(f2.tensor.slice(1, offsetY, offsetY + f1.tensor.size(1))
             .slice(2, offsetX, offsetX + f1.tensor.size(2))
             .equal(f1.tensor));
  EXPECT(f2.tensor.slice(1, 0, offsetY).sum().item<float>() == 0);
  EXPECT(
      f2.tensor.slice(1, f1.tensor.size(1) + offsetY, f2.tensor.size(1))
          .sum()
          .item<float>() == 0);
  EXPECT(f2.tensor.slice(2, 0, offsetX).sum().item<float>() == 0);
  EXPECT(
      f2.tensor.slice(2, f1.tensor.size(2) + offsetX, f2.tensor.size(2))
          .sum()
          .item<float>() == 0);
}
