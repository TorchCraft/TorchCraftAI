/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "features/unitsfeatures.h"
#include "state.h"
#include "utils.h"

#include <algorithm>
#include <cassert>

namespace cherrypi {

namespace {

std::pair<std::array<int, 234>*, std::array<int, 234>*> getDefoggerTypeMap() {
  static std::array<int, 234> map;
  static std::array<int, 234> imap;
  auto init = [&] {
    map.fill(117);
    int i = 0;
    for (auto t : tc::BW::UnitType::_values()) {
      map.at(t._to_integral()) = i;
      imap.at(i) = t._to_integral();
      i++;
    }
    if (i > 118) {
      throw std::runtime_error(
          "Unexpected total number of unit types: " + std::to_string(i));
    }
  };

  static std::once_flag flag;
  std::call_once(flag, init);
  return std::make_pair(&map, &imap);
}

} // namespace

int constexpr UnitTypeFeaturizer::kNumUnitTypes;
int constexpr UnitTypeDefoggerFeaturizer::kNumUnitTypes;
int constexpr UnitFlagsFeaturizer::kNumUnitFlags;
int constexpr UnitStatFeaturizer::kNumChannels;

UnitAttributeFeaturizer::Data UnitAttributeFeaturizer::extract(
    State* state,
    UnitsInfo::Units const& units,
    Rect const& boundingBox) {
  Data data;
  if (boundingBox.empty()) {
    data.boundingBox = state->mapRect();
  } else {
    data.boundingBox = boundingBox;
  }

  data.positions = torch::zeros({int(units.size()), 2}, torch::kI32);
  data.data = torch::zeros({int(units.size()), numChannels});
  if (units.empty()) {
    return data;
  }

  FeaturePositionMapper mapper(data.boundingBox, state->mapRect());
  auto& jr = *jitter.get();
  auto ap = data.positions.accessor<int, 2>();
  auto ad = data.data.accessor<float, 2>();
  int n = 0;
  for (auto* unit : units) {
    // Determine resulting position by jittering and mapping to desired bounding
    // box.
    auto pos = mapper(jr(unit));
    if (pos.x >= 0) {
      ap[n][0] = pos.y;
      ap[n][1] = pos.x;
      extractUnit(ad[n], unit);
      n++;
    }
  }

  if (n > 0) {
    data.positions.resize_({n, data.positions.size(1)});
    //data.postype.resize_({n, data.postype.size(1)});
    data.data.resize_({n, data.data.size(1)});
  } else {
    // undefined means empty
    data.positions = torch::Tensor();
    data.data = torch::Tensor();
    //data.postype = torch::Tensor();
  }
  return data;
}

UnitAttributeFeaturizer::Data UnitAttributeFeaturizer::extract(
    State* state,
    Rect const& boundingBox) {
  return extract(state, state->unitsInfo().liveUnits(), boundingBox);
}

UnitAttributeFeaturizer::Data UnitAttributeFeaturizer::extract(
    State* state,
    UnitFilter filter,
    Rect const& boundingBox) {
  auto const& src = state->unitsInfo().liveUnits();
  UnitsInfo::Units units(src.size());
  auto it = std::copy_if(src.begin(), src.end(), units.begin(), filter);
  units.resize(std::distance(units.begin(), it));
  return extract(state, units, boundingBox);
}

FeatureData UnitAttributeFeaturizer::toSpatialFeature(
    Data const& data,
    SubsampleMethod pooling) const {
  FeatureData ret;
  toSpatialFeature(&ret, data, pooling);
  return ret;
}

void UnitAttributeFeaturizer::toSpatialFeature(
    FeatureData* dest,
    Data const& data,
    SubsampleMethod pooling) const {
  if (data.data.defined() && int(data.data.size(1)) != numChannels) {
    throw std::runtime_error(
        "Found wrong number of channels. Wrong data instance?");
  }

  if (!dest->tensor.defined()) {
    dest->tensor = torch::zeros(
        {numChannels, data.boundingBox.height(), data.boundingBox.width()});
  } else {
    dest->tensor = dest->tensor.toType(at::kFloat);
    dest->tensor.resize_(
        {numChannels, data.boundingBox.height(), data.boundingBox.width()});
    dest->tensor.zero_();
  }
  dest->desc.clear();
  dest->desc.emplace_back(type, name, numChannels);
  dest->scale = 1;
  dest->offset.x = data.boundingBox.left();
  dest->offset.y = data.boundingBox.top();

  if (!data.positions.defined() || !data.data.defined()) {
    return;
  }

  auto numEntries = int(data.data.size(0));
  auto racc = dest->tensor.accessor<float, 3>();
  auto pacc = const_cast<torch::Tensor&>(data.positions).accessor<int, 2>();
  auto dacc = const_cast<torch::Tensor&>(data.data).accessor<float, 2>();
  for (auto i = 0; i < numEntries; i++) {
    auto y = pacc[i][0];
    auto x = pacc[i][1];
    auto srcit = dacc[i];
    if (pooling == SubsampleMethod::Sum) {
      for (auto j = 0; j < numChannels; j++) {
        racc[j][y][x] += srcit[j];
      }
    } else if (pooling == SubsampleMethod::Max) {
      for (auto j = 0; j < numChannels; j++) {
        racc[j][y][x] = std::max(racc[j][y][x], srcit[j]);
      }
    } else {
      throw std::runtime_error("Unsupported subsample method");
    }
  }
}

FeatureData UnitTypeFeaturizer::toOneHotSpatialFeature(
    Data const& data,
    int unitValueOffset,
    std::unordered_map<int, int> const& channelValues) const {
  if (data.data.defined() && int(data.data.size(1)) != 1) {
    throw std::runtime_error(
        "toOneHotSpatialFeature only works with single channel features.");
  }

  // The number of specified one-hot values +1 for 'other'
  auto numOneHotChannels = int(channelValues.size() + 1);

#ifndef NDEBUG
  // Verify that the assigned channel numbers match feature tensor size
  for (auto const& channelVal : channelValues) {
    assert(channelVal.second < numOneHotChannels);
  }
#endif // NDEBUG

  FeatureData dest;
  dest.tensor = torch::zeros(
      {numOneHotChannels, data.boundingBox.height(), data.boundingBox.width()});
  dest.desc.emplace_back(type, name, numOneHotChannels);
  dest.scale = 1;
  dest.offset.x = data.boundingBox.left();
  dest.offset.y = data.boundingBox.top();

  if (!data.positions.defined() || !data.data.defined()) {
    return dest;
  }

  auto numEntries = int(data.data.size(0));
  auto racc = dest.tensor.accessor<float, 3>();
  auto pacc = const_cast<torch::Tensor&>(data.positions).accessor<int, 2>();
  auto dacc = const_cast<torch::Tensor&>(data.data).accessor<float, 2>()[0];
  for (auto i = 0; i < numEntries; i++) {
    auto y = pacc[i][0];
    auto x = pacc[i][1];

    // The unit type is modified based on
    auto val = dacc[i] - unitValueOffset;
    auto channel = channelValues.find(val);
    if (channel == channelValues.end()) {
      // If value not in channel map, add to other
      racc[numOneHotChannels - 1][y][x] += 1.0;
    } else {
      racc[channel->second][y][x] += 1.0;
    }
  }

  return dest;
}

void UnitStatFeaturizer::extractUnit(TensorDest acc, cherrypi::Unit* u) {
  auto ind = 0;
  acc[ind++] = u->unit.pixel_x / 512.;
  acc[ind++] = u->unit.pixel_y / 512.;
  acc[ind++] = u->unit.velocityX / 5.;
  acc[ind++] = u->unit.velocityY / 5.;
  acc[ind++] = u->unit.health / 100.;
  acc[ind++] = u->unit.shield / 100.;
  acc[ind++] = u->unit.energy / 100.;
  acc[ind++] = u->unit.groundCD / 15.;
  acc[ind++] = u->unit.airCD / 15.;
  acc[ind++] = u->unit.armor / 10.;
  acc[ind++] = u->unit.shieldArmor / 10.;
  acc[ind++] = u->unit.groundATK / 10.;
  acc[ind++] = u->unit.airATK / 10.;
  acc[ind++] = u->unit.groundRange / 10.;
  acc[ind++] = u->unit.airRange / 10.;

  auto armorType = u->unit.size == cherrypi::tc::BW::UnitSize::Small
      ? 0
      : u->unit.size == cherrypi::tc::BW::UnitSize::Medium ? 1 : 2;
  acc[ind + armorType] = 1;
  ind += 3;

  auto gDmgType =
      u->unit.groundDmgType == cherrypi::tc::BW::DamageType::Concussive
      ? 0
      : u->unit.groundDmgType == cherrypi::tc::BW::DamageType::Explosive ? 1
                                                                         : 2;
  acc[ind + gDmgType] = 1;
  ind += 3;

  auto aDmgType = u->unit.airDmgType == cherrypi::tc::BW::DamageType::Concussive
      ? 0
      : u->unit.airDmgType == cherrypi::tc::BW::DamageType::Explosive ? 1 : 2;
  acc[ind + aDmgType] = 1;
  ind += 3;

  for (auto flag = 0; flag < cherrypi::UnitFlagsFeaturizer::kNumUnitFlags;
       flag++) {
    acc[ind++] = (u->unit.flags & (1 << flag)) ? 1 : 0;
  }
}

UnitTypeDefoggerFeaturizer::UnitTypeDefoggerFeaturizer() {
  type = CustomFeatureType::UnitTypeDefogger;
  name = "UnitTypDefogger";
  numChannels = 1;
  std::tie(typemap_, itypemap_) = getDefoggerTypeMap();
}

FeatureData UnitTypeDefoggerFeaturizer::toDefoggerFeature(
    Data const& data,
    int res,
    int stride) const {
  if (data.data.defined() && int(data.data.size(1)) != numChannels) {
    throw std::runtime_error(
        "Found wrong number of channels. Wrong data instance?");
  }

  auto nBinX = int32_t((double)(data.boundingBox.width() - res) / stride + 1);
  auto nBinY = int32_t((double)(data.boundingBox.height() - res) / stride + 1);

  FeatureData dest;
  dest.tensor = torch::zeros({kNumUnitTypes, nBinY, nBinX});
  dest.desc.emplace_back(type, name, kNumUnitTypes);
  dest.scale = res;
  dest.offset.x = data.boundingBox.left();
  dest.offset.y = data.boundingBox.top();
  if (!data.positions.defined() || !data.data.defined()) {
    return dest;
  }

  auto numEntries = int(data.data.size(0));
  auto racc = dest.tensor.accessor<float, 3>();
  auto pacc = const_cast<torch::Tensor&>(data.positions).accessor<int, 2>();
  auto dacc = const_cast<torch::Tensor&>(data.data).accessor<float, 2>();
  for (auto i = 0; i < numEntries; i++) {
    auto y = pacc[i][0];
    auto x = pacc[i][1];
    auto tp = dacc[i][0];
    if (tp < 0) {
      // Units to be ignored are mapped to type -1
      continue;
    }

    // Determine bins for this position; see defoggerfeatures.cpp for an
    // explanation
    int32_t maxbX = std::min(x / stride, nBinX - 1) + 1;
    int32_t maxbY = std::min(y / stride, nBinY - 1) + 1;
    int32_t minbX = std::max(
        0, maxbX - (int32_t(res) - (x % stride) + stride - 1) / stride);
    int32_t minbY = std::max(
        0, maxbY - (int32_t(res) - (y % stride) + stride - 1) / stride);

    for (int32_t by = minbY; by < maxbY; by++) {
      for (int32_t bx = minbX; bx < maxbX; bx++) {
        racc[tp][by][bx] += 1;
      }
    }
  }

  return dest;
}

UnitTypeMDefoggerFeaturizer::UnitTypeMDefoggerFeaturizer() {
  type = CustomFeatureType::UnitTypeMDefogger;
  name = "UnitTypMDefogger";
  numChannels = 1;
  std::tie(typemap_, itypemap_) = getDefoggerTypeMap();
}

UnitAttributeFeaturizer::Data UnitTypeMDefoggerFeaturizer::extract(
    State* state,
    UnitsInfo::Units const& units,
    Rect const& boundingBox) {
  /// XXX copy-pasta from UnitAttributeFeaturizer...
  Data data;
  if (boundingBox.empty()) {
    data.boundingBox = state->mapRect();
  } else {
    data.boundingBox = boundingBox;
  }
  if (units.empty()) {
    return data;
  }

  // Take care to featurize morphing Zerglings as two units
  int numUnits = 0;
  for (auto* u : units) {
    numUnits++;
    if (u->isMine && u->morphing() && u->constructingType &&
        u->constructingType->isTwoUnitsInOneEgg) {
      numUnits++;
    }
  }
  data.positions = torch::zeros({numUnits, 2}, torch::kI32);
  data.data = torch::zeros({numUnits}); // small optim, add channel dim later

  FeaturePositionMapper mapper(data.boundingBox, state->mapRect());
  auto& jr = *jitter.get();
  auto ap = data.positions.accessor<int, 2>();
  auto ad = data.data.accessor<float, 1>();
  int n = 0;
  for (auto* unit : units) {
    // Determine resulting position by jittering and mapping to desired bounding
    // box.
    auto pos = mapper(jr(unit));
    if (pos.x >= 0) {
      ap[n][0] = pos.y;
      ap[n][1] = pos.x;

      if (unit->isMine) {
        if (unit->morphing() && unit->constructingType) {
          ad[n++] = mapType(unit->constructingType->unit) + 118 * 0;
          if (unit->constructingType->isTwoUnitsInOneEgg) {
            ad[n++] = mapType(unit->constructingType->unit) + 118 * 0;
          }
        } else {
          ad[n++] = mapType(unit->type->unit) + 118 * 0;
        }
      } else if (unit->isEnemy) {
        ad[n++] = mapType(unit->type->unit) + 118 * 1;
      } else {
        ad[n++] = -1;
      }
    }
  }

  if (n > 0) {
    data.positions.resize_({n, data.positions.size(1)});
    data.data.resize_({n}).unsqueeze_(1); // NxC expected
  } else {
    // undefined means empty
    data.positions = torch::Tensor();
    data.data = torch::Tensor();
  }
  return data;
}

UnitTypeGasFeaturizer::UnitTypeGasFeaturizer() {
  type = CustomFeatureType::UnitTypeGas;
  name = "UnitTypeGas";
  numChannels = UnitStatFeaturizer::kNumChannels;
}


UnitAttributeFeaturizer::Data UnitTypeGasFeaturizer::extract(
    State* state,
    UnitsInfo::Units const& units,
    Rect const& boundingBox) {
  /// XXX copy-pasta from UnitAttributeFeaturizer...
  Data data;
  if (boundingBox.empty()) {
    data.boundingBox = state->mapRect();
  } else {
    data.boundingBox = boundingBox;
  }
  if (units.empty()) {
    return data;
  }
  auto numUnitTypes = unittypemap_.size();
  if (numUnitTypes == 0 ) { // we need to assign unit types 1 hot values
    // needs to be deterministic for consistency over all episodes
    std::set<int> unitTypes = std::set<int>();
    for (auto* u : units) {
      unitTypes.insert(u->type->unit);
    }
    int i = 0;
    for (auto it = unitTypes.begin(); it != unitTypes.end(); ++it) {
      unittypemap_[*it] = i;
      i ++;
    }
    numUnitTypes = unittypemap_.size();
  }
  data.positions = torch::zeros({int(units.size()), 2}, torch::kI32);
  //data.postype = torch::zeros({numUnits, 2 + numUnitTypes}, torch::kI32);
  data.data = torch::zeros({int(units.size()), numUnitTypes}, torch::kI32); // small optim, add channel dim later

  FeaturePositionMapper mapper(data.boundingBox, state->mapRect());
  auto& jr = *jitter.get();
  auto ap = data.positions.accessor<int, 2>();
  //auto apt = data.postype.accessor<int, 2 + numUnitTypes>();
  auto ad = data.data.accessor<int, 2>();
  int n = 0;
  for (auto* unit : units) {
    // Determine resulting position by jittering and mapping to desired bounding
    // box.
    auto pos = mapper(jr(unit));
    if (pos.x >= 0) {
      ap[n][0] = pos.y;
      ap[n][1] = pos.x;
      //ap[n][unittypemap_[2+ unit->type->unit]] = 1;
      ad[n][unittypemap_[unit->type->unit]] = 1;
      n++;
    }
  }

  if (n > 0) {
    data.positions.resize_({n, data.positions.size(1)});
    data.data.resize_({n, data.data.size(1)}); // NxC expected
  } else {
    // undefined means empty
    data.positions = torch::Tensor();
    data.data = torch::Tensor();
  }
  return data;
}

} // namespace cherrypi
