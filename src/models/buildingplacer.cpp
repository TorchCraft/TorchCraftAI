/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "buildingplacer.h"

#include "modules/builderhelper.h"
#include "state.h"
#include "upc.h"

#include <common/autograd.h>

namespace cherrypi {

int BuildingPlacerSample::kNumMapChannels = 8;

namespace {

auto constexpr kMapSizeInBuildTiles =
    BuildingPlacerSample::kMapSize / tc::BW::XYWalktilesPerBuildtile;

// Compute valid build locations for the requested type in build tile scale.
// NOTE: positionTensor is expected to be in *build tile* scale already and
// correct feature size.
torch::Tensor validBuildLocations(
    State* state,
    BuildType const* type,
    torch::Tensor position) {
  unsigned constexpr scale = tc::BW::XYWalktilesPerBuildtile;
  auto const mapHeightS = state->mapHeight() / scale;
  auto const mapWidthS = state->mapWidth() / scale;
  auto const yoff = (position.size(0) - mapHeightS) / 2;
  auto const xoff = (position.size(1) - mapWidthS) / 2;

  auto valid = torch::CPU(torch::kFloat).zeros_like(position);
  auto vacc = valid.accessor<float, 2>();
  auto pacc = position.accessor<float, 2>();
  if (vacc.size(0) != pacc.size(0) || vacc.size(1) != pacc.size(1)) {
    throw std::runtime_error("Tensor sizes do not match");
  }
  std::vector<uint8_t> const& bdata = state->tcstate()->buildable_data;
  auto bdstride = state->mapWidth();

  // This is not very fast since we check all tiles for a building, for every
  // tile. A faster implementation is doable but requires reorganization of code
  // in builderhelper.
  for (auto i = 0U; i < mapHeightS; i++) {
    auto vrow = vacc[i + yoff];
    auto prow = pacc[i + yoff];
    auto* bptr = bdata.data() + i * scale * bdstride;
    for (auto j = 0U; j < mapWidthS; j++) {
      if (prow[j + xoff] <= 0.0f || bptr[j * scale] == 0) {
        continue;
      }
      if (builderhelpers::canBuildAt(
              state, type, Position(j * scale, i * scale))) {
        vrow[j + xoff] = 1.0f;
      }
    }
  }

  return valid;
}

} // namespace

BuildingPlacerSample::StaticData::StaticData(State* state) {
  // Make sure we always extract a kMapSize x kMapSize feature tensor. The
  // actual map will be at the center of the tensor.
  auto bbox =
      Rect::centeredWithSize(state->mapRect().center(), kMapSize, kMapSize);
  unsigned constexpr scale = tc::BW::XYWalktilesPerBuildtile;

  // UserFeature1 is just a place-holder for a contiguous tensor. It will be
  // filled in the actual BuildingPlacerSample ctor.
  smap = subsampleFeature(
      featurizePlain(
          state,
          {PlainFeatureType::UserFeature1,
           PlainFeatureType::GroundHeight,
           PlainFeatureType::TallDoodad,
           PlainFeatureType::Walkability,
           PlainFeatureType::Buildability},
          bbox),
      SubsampleMethod::Average,
      scale);
  smap.desc[0].name = "Position (UPC)";
}

BuildingPlacerSample::BuildingPlacerSample(
    State* state,
    std::shared_ptr<UPCTuple> upc,
    StaticData* staticData)
    : frame(state->currentFrame()),
      mapName(state->tcstate()->map_name),
      playerName(state->tcstate()->player_info[state->playerId()].name) {
  // Make sure we always extract a kMapSize x kMapSize feature tensor. The
  // actual map will be at the center of the tensor.
  auto bbox =
      Rect::centeredWithSize(state->mapRect().center(), kMapSize, kMapSize);
  unsigned constexpr scale = tc::BW::XYWalktilesPerBuildtile;

  if (staticData != nullptr) {
    auto curf = subsampleFeature(
        featurizePlain(
            state,
            {PlainFeatureType::FogOfWar,
             PlainFeatureType::Creep,
             PlainFeatureType::CandidateEnemyStartLocations},
            bbox),
        SubsampleMethod::Average,
        scale);
    features.map = combineFeatures({staticData->smap, std::move(curf)});
  } else {
    // UserFeature1 is just a place-holder for a contiguous tensor. It will be
    // filled below with position data from the UPC.
    features.map = subsampleFeature(
        featurizePlain(
            state,
            {PlainFeatureType::UserFeature1,
             PlainFeatureType::GroundHeight,
             PlainFeatureType::TallDoodad,
             PlainFeatureType::Walkability,
             PlainFeatureType::Buildability,
             PlainFeatureType::FogOfWar,
             PlainFeatureType::Creep,
             PlainFeatureType::CandidateEnemyStartLocations},
            bbox),
        SubsampleMethod::Average,
        scale);
    features.map.desc[0].name = "Position (UPC)";
  }

  // Subsample and binarize probabilities
  auto upcP = torch::avg_pool2d(
      upc->positionTensor(state).unsqueeze(0), {scale, scale});
  upcP.gt_(0);

  // Center UPC in map feature
  auto yoff = (features.map.tensor.size(1) - upcP.size(1)) / 2;
  auto xoff = (features.map.tensor.size(2) - upcP.size(2)) / 2;
  assert(xoff >= 0 && yoff >= 0);
  features.map.tensor[0]
      .slice(0, yoff, yoff + upcP.size(1))
      .slice(1, xoff, xoff + upcP.size(2))
      .copy_(upcP[0]);

  features.type = upc->createTypeArgMax().first->unit;
  features.validLocations = validBuildLocations(
      state, upc->createTypeArgMax().first, features.map.tensor[0]);

  assert(features.map.numChannels() == kNumMapChannels);
  features.units = unitFeaturizer.extract(state, bbox);
}

BuildingPlacerSample::BuildingPlacerSample(
    State* state,
    Position action,
    std::shared_ptr<UPCTuple> upc)
    : BuildingPlacerSample(state, std::move(upc)) {
  this->action = action;
  areaId = state->areaInfo().getArea(action).id;
}

std::vector<torch::Tensor> BuildingPlacerSample::networkInput() const {
  // TODO: use a scalar?
  auto typeT = torch::empty({1}, torch::kI64);
  typeT.fill_(features.type);

  return {features.map.tensor,
          features.units.positions.div(features.map.scale),
          features.units.data.toType(torch::kLong), // for embeddings
          typeT,
          features.validLocations};
}

int64_t BuildingPlacerSample::actionToOffset(Position pos, int scale) const {
  auto rscale = features.map.scale * scale;
  auto planeDim = (BuildingPlacerSample::kMapSize / rscale);
  int64_t offset = (pos.y - features.map.offset.y) / rscale * planeDim +
      ((pos.x - features.map.offset.x) / rscale);
  return offset;
}

Position BuildingPlacerSample::offsetToAction(int64_t offset, int scale) const {
  auto rscale = features.map.scale * scale;
  auto planeDim = (BuildingPlacerSample::kMapSize / rscale);
  Position pos(
      (offset % planeDim + features.map.offset.x / rscale) * rscale,
      (offset / planeDim + features.map.offset.y / rscale) * rscale);
  return pos;
}

void BuildingPlacerModel::reset() {
  auto ntypes = (+tc::BW::UnitType::MAX)._to_integral();

  auto dimU = 12;
  auto dimT = 4;
  embedU = add(
      ag::Embedding(UnitTypeFeaturizer::kNumUnitTypes, dimU).make(), "embedU");
  embedT = add(ag::Embedding(ntypes, dimT).make(), "embedT");

  // Map features are stacked with embeddings projected to 2D
  auto numInputChannels = BuildingPlacerSample::kNumMapChannels + dimU + dimT;
  int constexpr kw = 5;
  conv1 =
      add(ag::Conv2d(numInputChannels, num_top_channels_ / 2, kw)
              .padding(kw / 2)
              .make(),
          "conv1"); // on 128x128
  conv2 =
      add(ag::Conv2d(num_top_channels_ / 2, num_top_channels_, kw)
              .padding(kw / 2)
              .stride(2)
              .make(),
          "conv2"); // on 128x128 with stride=2
  conv3 =
      add(ag::Conv2d(num_top_channels_, num_top_channels_, kw)
              .padding(kw / 2)
              .stride(2)
              .make(),
          "conv3"); // on 64x64 with stride=2

  // Top-level convolutions, operating on 32x32 inputs
  if (num_top_convs_ < 0) {
    num_top_convs_ = std::ceil(float(32 - 1) / (kw - 1));
  }
  for (int i = 0; i < num_top_convs_; i++) {
    convS.push_back(
        add(ag::Conv2d(num_top_channels_, num_top_channels_, kw)
                .padding(kw / 2)
                .make(),
            "convS" + std::to_string(i)));
  }

  skip2 =
      add(ag::Conv2d(num_top_channels_, num_top_channels_, 1).make(), "skip2");
  dconv2 =
      add(ag::Conv2d(num_top_channels_, num_top_channels_, kw)
              .padding(kw / 2)
              .make(),
          "dconv2");
  postskip2 =
      add(ag::Conv2d(num_top_channels_, num_top_channels_, kw)
              .padding(kw / 2)
              .make(),
          "postskip2");
  skip1 =
      add(ag::Conv2d(num_top_channels_ / 2, num_top_channels_ / 2, 1).make(),
          "skip1");
  dconv1 =
      add(ag::Conv2d(num_top_channels_, num_top_channels_ / 2, kw)
              .padding(kw / 2)
              .make(),
          "dconv1");
  postskip1 =
      add(ag::Conv2d(num_top_channels_ / 2, num_top_channels_ / 2, kw)
              .padding(kw / 2)
              .make(),
          "postskip1");

  // Output layer
  out = add(ag::Conv2d(num_top_channels_ / 2, 1, 1).make(), "out");
}

ag::Variant BuildingPlacerModel::makeInputBatch(
    std::vector<BuildingPlacerSample> const& samples,
    torch::Device device) const {
  if (samples.empty()) {
    return ag::VariantDict();
  }

  ag::tensor_list maps;
  ag::tensor_list unitsPs;
  ag::tensor_list unitsDs;
  ag::tensor_list types;
  ag::tensor_list valids;

  for (auto const& sample : samples) {
    auto inp = sample.networkInput();
    maps.push_back(inp[0]);
    unitsPs.push_back(inp[1]);
    unitsDs.push_back(inp[2]);
    types.push_back(inp[3]);
    valids.push_back(inp[4]);
  }

  // Pad positions with -1; they'll be ignored in scatterSum()
  return ag::VariantDict{
      {"map", torch::stack(maps).to(device)},
      {"units_pos", common::makeBatch(unitsPs, -1).to(device)},
      {"units_data", common::makeBatch(unitsDs, 0).to(device)},
      {"type", torch::cat(types).to(device)},
      {"valid_mask", torch::stack(valids).to(device)}};
}

ag::Variant BuildingPlacerModel::makeInputBatch(
    std::vector<BuildingPlacerSample> const& samples) const {
  return makeInputBatch(samples, options().device());
}

std::pair<ag::Variant, ag::Variant> BuildingPlacerModel::makeBatch(
    std::vector<BuildingPlacerSample> const& samples,
    torch::Device device) const {
  if (samples.empty()) {
    return std::make_pair(ag::VariantDict(), ag::VariantDict());
  }

  int64_t numSamples = samples.size();
  auto targets = torch::empty({numSamples}, torch::kI64);
  for (auto i = 0U; i < numSamples; i++) {
    targets[i] = samples[i].actionToOffset(samples[i].action);
  }

  auto target = ag::VariantDict{{"target", targets.to(device)}};
  std::pair<ag::Variant, ag::Variant> result =
      std::make_pair(makeInputBatch(samples, device), target);
  return result;
}

std::pair<ag::Variant, ag::Variant> BuildingPlacerModel::makeBatch(
    std::vector<BuildingPlacerSample> const& samples) const {
  return makeBatch(samples, options().device());
}

ag::Variant BuildingPlacerModel::forward(ag::Variant input) {
  auto map = input["map"];
  ASSERT_SIZE(
      map,
      {-1,
       BuildingPlacerSample::kNumMapChannels,
       kMapSizeInBuildTiles,
       kMapSizeInBuildTiles});
  auto batchSize = map.size(0);
  auto unitsPos = input["units_pos"];
  ASSERT_SIZE(unitsPos, {batchSize, -1, 2});
  auto unitsData = input["units_data"];
  ASSERT_SIZE(unitsData, {batchSize, unitsPos.size(1), 1});
  auto type = input["type"];
  ASSERT_SIZE(type, {batchSize});
  auto validMask = input["valid_mask"];
  ASSERT_SIZE(
      validMask, {batchSize, kMapSizeInBuildTiles, kMapSizeInBuildTiles});

  // Embed units and requested type
  auto unitsT = embedU->forward({unitsData})[0].squeeze(2);
  auto typeT = embedT->forward({type})[0];

  // Place on 2D map. For now, handle each sample in the mini-batch individually
  auto units2d =
      common::scatterSum2d(unitsPos, unitsT, {map.size(2), map.size(3)});
  auto type2d = typeT.unsqueeze(2).unsqueeze(2).expand(
      {typeT.size(0), typeT.size(1), map.size(2), map.size(3)});

  // Prepare input to convolutions
  torch::Tensor x = torch::cat(
      {map,
       units2d.to(map.options().device()),
       type2d.to(map.options().device())},
      1);

  // Up the pyramid
  x = torch::relu(conv1->forward({x})[0]);
  torch::Tensor outC1 = x;
  x = torch::relu(conv2->forward({x})[0]);
  torch::Tensor outC2 = x;
  x = torch::relu(conv3->forward({x})[0]);
  torch::Tensor outC3 = x;

  // Through top convs
  for (auto i = 0U; i < convS.size(); i++) {
    x = torch::relu(convS[i]->forward({x})[0]);
  }

  // Back to original output resolution
  x = common::upsample(x, common::UpsampleMode::Nearest, 2);
  x = dconv2->forward({x})[0];
  x = torch::relu(x + skip2->forward({outC2})[0]);
  x = torch::relu(postskip2->forward({x})[0]);

  x = common::upsample(x, common::UpsampleMode::Nearest, 2);
  x = dconv1->forward({x})[0];
  x = torch::relu(x + skip1->forward({outC1})[0]);
  x = torch::relu(postskip1->forward({x})[0]);

  torch::Tensor y = out->forward({x})[0].view({batchSize, -1});

  // Compute softmax (if required with log) with optional masking
  torch::Tensor mask;
  if (masked_) {
    mask = validMask.view({batchSize, -1});
    y = common::maskedSoftmax(y, mask, 1, kfEpsilon);
    if (logprobs_) {
      y = y.log();
    }
  } else {
    if (logprobs_) {
      y = torch::log_softmax(y, 1);
    } else {
      y = torch::softmax(y, 1);
    }
    // Not masked_ so just return an all-ones tensor for "mask"
    mask = torch::ones_like(y);
  }
  if (!flatten_) {
    y = y.view({x.size(0), x.size(2), x.size(3)});
    mask = mask.view({x.size(0), x.size(2), x.size(3)});
  }

  return ag::VariantDict{{"output", y}, {"mask", mask}};
}

} // namespace cherrypi
