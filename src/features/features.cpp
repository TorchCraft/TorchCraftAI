/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "features.h"

#include "state.h"

#include <glog/logging.h>

namespace cherrypi {

// Feature extractor proto-types
namespace featureimpl {

// for user features
void noop(torch::Tensor, State*, Rect const&) {}
// mapfeatures.cpp
void extractGroundHeight(torch::Tensor, State*, Rect const&);
void extractWalkability(torch::Tensor, State*, Rect const&);
void extractOneHotGroundHeight(torch::Tensor, State*, Rect const&);
void extractBuildability(torch::Tensor, State*, Rect const&);
void extractTallDoodad(torch::Tensor, State*, Rect const&);
void extractStartLocations(torch::Tensor, State*, Rect const&);
// tilesfeatures.cpp
void extractFogOfWar(torch::Tensor, State*, Rect const&);
void extractCreep(torch::Tensor, State*, Rect const&);
void extractReservedAsUnbuildable(torch::Tensor, State*, Rect const&);
// areafeatures.cpp
void extractCandidateEnemyStartLocations(torch::Tensor, State*, Rect const&);

} // namespace featureimpl

namespace {

struct PlainFeatureInfo {
  using Function = std::function<void(torch::Tensor, State*, Rect const&)>;

  char const* name;
  Function fn;
  int numChannels;
};

std::unordered_map<int, PlainFeatureInfo>& featureRegistry() {
  static std::unordered_map<int, PlainFeatureInfo> reg;
  return reg;
}

int featureTypeValue(AnyFeatureType type) {
  int value;
  type.match(
      [&](PlainFeatureType t) { value = static_cast<int>(t); },
      [&](CustomFeatureType t) { value = static_cast<int>(t); });
  return value;
}

} // namespace

namespace features {

void initialize() {
#define ADD_PLAIN_FEATURE(NAME, FN, NUM_CHANNELS)                             \
  do {                                                                        \
    featureRegistry()[static_cast<int>(PlainFeatureType::NAME)].name = #NAME; \
    featureRegistry()[static_cast<int>(PlainFeatureType::NAME)].fn = FN;      \
    featureRegistry()[static_cast<int>(PlainFeatureType::NAME)].numChannels = \
        NUM_CHANNELS;                                                         \
  } while (0)

  ADD_PLAIN_FEATURE(GroundHeight, &featureimpl::extractGroundHeight, 1);
  ADD_PLAIN_FEATURE(
      OneHotGroundHeight, &featureimpl::extractOneHotGroundHeight, 4);
  ADD_PLAIN_FEATURE(Walkability, &featureimpl::extractWalkability, 1);
  ADD_PLAIN_FEATURE(Buildability, &featureimpl::extractBuildability, 1);
  ADD_PLAIN_FEATURE(FogOfWar, &featureimpl::extractFogOfWar, 1);
  ADD_PLAIN_FEATURE(Creep, &featureimpl::extractCreep, 1);
  ADD_PLAIN_FEATURE(
      CandidateEnemyStartLocations,
      &featureimpl::extractCandidateEnemyStartLocations,
      1);
  ADD_PLAIN_FEATURE(StartLocations, &featureimpl::extractStartLocations, 1);
  ADD_PLAIN_FEATURE(
      ReservedAsUnbuildable, &featureimpl::extractReservedAsUnbuildable, 1);
  ADD_PLAIN_FEATURE(TallDoodad, &featureimpl::extractTallDoodad, 1);
  ADD_PLAIN_FEATURE(UserFeature1, &featureimpl::noop, 1);
  ADD_PLAIN_FEATURE(UserFeature2, &featureimpl::noop, 2);

#undef ADD_PLAIN_FEATURE
}

} // namespace features

bool FeatureDescriptor::operator==(FeatureDescriptor const& other) const {
  int value1 = -1;
  type.match(
      [&](PlainFeatureType t) { value1 = static_cast<int>(t); },
      [&](CustomFeatureType t) { value1 = static_cast<int>(t); });
  int value2 = -1;
  other.type.match(
      [&](PlainFeatureType t) { value2 = static_cast<int>(t); },
      [&](CustomFeatureType t) { value2 = static_cast<int>(t); });

  return value1 == value2 && name == other.name &&
      numChannels == other.numChannels;
}

int FeatureData::numChannels() const {
  if (tensor.defined()) {
    return tensor.size(0);
  }
  return 0;
}

Rect FeatureData::boundingBox() const {
  return Rect(offset, tensor.size(2) * scale, tensor.size(1) * scale);
}

Rect FeatureData::boundingBoxAtScale() const {
  return Rect(offset / scale, tensor.size(2), tensor.size(1));
}

FeatureData featurizePlain(
    State* state,
    std::vector<PlainFeatureType> types,
    Rect boundingBox) {
  if (boundingBox.empty()) {
    boundingBox = state->mapRect();
  }

  auto& reg = featureRegistry();

  Rect crop = boundingBox;
  int nchannels = 0;
  for (auto& type : types) {
    auto it = reg.find(static_cast<int>(type));
    if (it == reg.end()) {
      throw std::runtime_error(
          "Unknown feature with ID " + std::to_string(static_cast<int>(type)));
    }
    auto& info = it->second;

    int nchan = info.numChannels;
    if (nchan < 0) {
      throw std::runtime_error("Cannot extract variable-length feature");
    }
    nchannels += nchan;
  }

  FeatureData ret;
  ret.tensor = torch::zeros({nchannels, crop.height(), crop.width()});
  ret.scale = 1;
  ret.offset.x = crop.left();
  ret.offset.y = crop.top();
  int chan = 0;
  for (auto& type : types) {
    auto it = reg.find(static_cast<int>(type));
    assert(it != reg.end()); // ensured in loop above
    auto& info = it->second;

    int nchan = info.numChannels;
    info.fn(ret.tensor.slice(0, chan, chan + nchan), state, crop);
    ret.desc.emplace_back(type, info.name, nchan);
    chan += nchan;
  }
  return ret;
}

FeatureData combineFeatures(std::vector<FeatureData> const& feats) {
  if (feats.empty()) {
    return FeatureData();
  }

  Rect rect;
  int scale = -1;
  int nchannels = 0;
  for (auto const& feat : feats) {
    if (scale < 0) {
      scale = feat.scale;
    } else if (feat.scale != scale) {
      throw std::runtime_error("Cannot combine features with varying scales");
    }
    rect = rect.united(
        Rect(feat.offset, feat.tensor.size(2), feat.tensor.size(1)));
    nchannels += feat.numChannels();
  }

  FeatureData ret;
  ret.scale = scale;
  ret.offset.x = rect.left();
  ret.offset.y = rect.top();
  ret.tensor = torch::zeros({nchannels, rect.height(), rect.width()});
  int channelOffset = 0;
  for (auto const& feat : feats) {
    int nchan = feat.numChannels();
    if (nchan == 0) {
      continue;
    }
    int xOffset = feat.offset.x - rect.x;
    int yOffset = feat.offset.y - rect.y;
    auto dest = ret.tensor.slice(0, channelOffset, channelOffset + nchan)
                    .slice(1, yOffset, yOffset + feat.tensor.size(1))
                    .slice(2, xOffset, xOffset + feat.tensor.size(2));
    dest.copy_(feat.tensor);

    ret.desc.insert(ret.desc.end(), feat.desc.begin(), feat.desc.end());
    channelOffset += nchan;
  }

  return ret;
}

FeatureData selectFeatures(
    FeatureData const& feat,
    std::vector<AnyFeatureType> types) {
  FeatureData ret;
  ret.scale = feat.scale;
  ret.offset = feat.offset;

  std::vector<int> offsets; // of channels in feat
  offsets.push_back(0);
  for (auto const& desc : feat.desc) {
    offsets.push_back(offsets.back() + desc.numChannels);
  }

  // Select channels
  auto indices = torch::zeros({feat.tensor.size(0)}, torch::kI64);
  auto acc = indices.accessor<int64_t, 1>();
  int64_t nextIndex = 0;
  for (auto& type : types) {
    int offset = offsets[0];
    for (auto i = 0U; i < feat.desc.size(); i++) {
      if (featureTypeValue(feat.desc[i].type) == featureTypeValue(type)) {
        for (int j = 0; j < feat.desc[i].numChannels; j++) {
          acc[nextIndex++] = offset + j;
        }
        ret.desc.push_back(feat.desc[i]);
      }
      offset = offsets[i + 1];
    }
  }
  if (nextIndex <= 0) {
    return ret;
  }

  indices.resize_({nextIndex});
  ret.tensor = feat.tensor.index_select(0, indices);
  return ret;
}

FeatureData subsampleFeature(
    FeatureData const& feat,
    SubsampleMethod method,
    int factor,
    int stride) {
  FeatureData ret;
  ret.scale = feat.scale * factor;
  ret.offset = feat.offset;
  ret.desc = feat.desc;

  std::vector<int64_t> strides({factor, factor});
  if (stride > 0) {
    strides = {stride, stride};
  }
  switch (method) {
    case SubsampleMethod::Sum:
      // There's no sum pooling in ATen
      ret.tensor =
          at::avg_pool2d(feat.tensor, {factor, factor}, strides, {0, 0});
      ret.tensor.mul_(factor * factor);
      break;
    case SubsampleMethod::Average:
      ret.tensor =
          at::avg_pool2d(feat.tensor, {factor, factor}, strides, {0, 0});
      break;
    case SubsampleMethod::Max:
      ret.tensor = at::max_pool2d(
          feat.tensor, {factor, factor}, strides, {0, 0}, {1, 1}, false);
      break;
    default:
      throw std::runtime_error("Subsample method not implemented");
      break;
  }
  return ret;
}

} // namespace cherrypi
