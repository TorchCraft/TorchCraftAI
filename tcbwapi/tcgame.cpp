/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "tcgame.h"

#include "tcunit.h"

namespace tc = torchcraft;

namespace tcbwapi {

void TCGame::setState(tc::State* s) {
  s_ = s;
  for (auto& u : s_->frame->units[s_->neutral_id]) {
    staticNeutralUnits_.emplace(new TCUnit(u));
  }
  for (auto& loc : s_->start_locations) {
    startLocations_.emplace_back(BWAPI::WalkPosition(loc.x, loc.y));
  }
}

TCGame::~TCGame() {
  for (auto& u : staticNeutralUnits_) {
    delete static_cast<TCUnit*>(u);
  }
}

bool TCGame::isWalkable(int walkX, int walkY) const {
  if (walkX < 0 || walkX >= s_->map_size[0] || walkY < 0 ||
      walkY >= s_->map_size[1]) {
    return false;
  }
  auto stride = s_->map_size[0];
  return s_->walkable_data[(walkY * stride) + walkX] > 0;
}

int TCGame::getGroundHeight(int tileX, int tileY) const {
  if (tileX < 0 || tileX >= mapWidth() || tileY < 0 || tileY >= mapHeight()) {
    return -1;
  }
  auto stride = s_->map_size[0];
  return s_
      ->ground_height_data[(unsigned(tileY) * tc::BW::XYWalktilesPerBuildtile *
                            stride) +
                           unsigned(tileX) * tc::BW::XYWalktilesPerBuildtile];
}

bool TCGame::isBuildable(int tileX, int tileY, bool includeBuildings) const {
  if (includeBuildings) {
    throwNotImplemented();
  }
  if (tileX < 0 || tileX >= mapWidth() || tileY < 0 || tileY >= mapHeight()) {
    return false;
  }
  auto stride = s_->map_size[0];
  return s_->buildable_data[(unsigned(tileY) * tc::BW::XYWalktilesPerBuildtile *
                             stride) +
                            unsigned(tileX) * tc::BW::XYWalktilesPerBuildtile];
}

BWAPI::Unitset const& TCGame::getStaticNeutralUnits() const {
  return staticNeutralUnits_;
}

BWAPI::TilePosition::list const& TCGame::getStartLocations() const {
  return startLocations_;
}

int TCGame::mapWidth() const {
  return unsigned(s_->map_size[0]) / tc::BW::XYWalktilesPerBuildtile;
}

int TCGame::mapHeight() const {
  return unsigned(s_->map_size[1]) / tc::BW::XYWalktilesPerBuildtile;
}

BWAPI::Unit TCGame::getUnit(int unitID) const {
  for (auto& u : staticNeutralUnits_) {
    if (u->getID() == unitID) {
      return u;
    }
  }
  return nullptr;
}

void TCGame::throwNotImplemented() const {
  throw std::runtime_error("tcbwapi::TCGame: Method not implemented");
}

} // namespace tcbwapi
