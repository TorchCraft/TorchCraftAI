/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

namespace cherrypi {

enum class GameType {
  Melee,
  UseMapSettings,
};

inline char const* gameTypeName(GameType type) {
  switch (type) {
    case GameType::Melee:
      return "MELEE";
    case GameType::UseMapSettings:
      return "USE_MAP_SETTINGS";
    default:
      break;
  }
  throw std::runtime_error("Unknown game type");
};

} // namespace cherrypi