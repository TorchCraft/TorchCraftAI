/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "featurize.h"
#include "flags.h"
#include "utils.h"
#include <glog/logging.h>

using namespace cherrypi;
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
namespace cherrypi {

SimpleUnitFeaturizer::SimpleUnitFeaturizer() {
  type = CustomFeatureType::Other;
  name = "SimpleUnitFeat";
  numChannels = kNumChannels;
  //std::tie(typemap_, itypemap_) = getDefoggerTypeMap();
}

int SimpleUnitFeaturizer::kNumChannels = 8;
void SimpleUnitFeaturizer::init() {
  if (FLAGS_scenario == "dragzeal" || FLAGS_scenario == "zerghydra") {
    kNumChannels += 2;
  }
}
void SimpleUnitFeaturizer::extractUnit(TensorDest acc, Unit* u) {
  auto ind = 0;
  acc[ind++] = u->isEnemy;
  // TODO compute better center
  acc[ind++] = u->unit.pixel_x / 64. - 15.;
  acc[ind++] = u->unit.pixel_y / 64. - 15.;
  acc[ind++] = u->unit.velocityX / 5.;
  acc[ind++] = u->unit.velocityY / 5.;
  acc[ind++] = (u->unit.shield + u->unit.health) /
      float(u->type->maxHp + u->type->maxShields);
  acc[ind++] = std::max(u->unit.groundCD, u->unit.airCD) /
      float(std::max(
          u->type->airWeaponCooldown, u->type->groundWeaponCooldown));
  acc[ind++] = std::min(u->unit.groundRange, u->unit.airRange) / 10.;

  if (FLAGS_scenario == "dragzeal" || FLAGS_scenario == "zerghydra") {
    // int typeId = mapType(u->type->unit);
    int typeId = 0;
    if (u->type->unit == tc::BW::UnitType::Zerg_Zergling ||
        u->type->unit == tc::BW::UnitType::Protoss_Zealot) {
      typeId = 1;
    }

    acc[ind + typeId] = 1.;
    // LOG(INFO) << typeId;
  }
}

} // namespace cherrypi
