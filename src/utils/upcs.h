/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <memory>

#include "upc.h"

namespace cherrypi {
namespace utils {

inline auto makeSharpUPC(Unit* u, Command c) {
  auto upc = std::make_shared<UPCTuple>();
  upc->unit[u] = 1;
  upc->command[c] = 1;
  return upc;
}
inline auto makeSharpUPC(Unit* u, Position p, Command c) {
  auto upc = std::make_shared<UPCTuple>();
  upc->unit[u] = 1;
  upc->position = p;
  upc->command[c] = 1;
  return upc;
}
inline auto makeSharpUPC(Unit* u, Unit* p, Command c) {
  auto upc = std::make_shared<UPCTuple>();
  upc->unit[u] = 1;
  upc->position = UPCTuple::UnitMap{{p, 1}};
  upc->command[c] = 1;
  return upc;
}
inline auto makeSharpUPC(Unit* u, Position p, Command c, BuildType const* ct) {
  auto upc = std::make_shared<UPCTuple>();
  upc->unit[u] = 1;
  upc->position = p;
  upc->command[c] = 1;
  upc->state = UPCTuple::BuildTypeMap{{ct, 1}};
  return upc;
}
inline auto makeSharpUPC(UPCTuple& other_upc, Unit* u, Command c) {
  auto upc = std::make_shared<UPCTuple>(other_upc);
  upc->unit[u] = 1;
  upc->command[c] = 1;
  return upc;
}

} // namespace utils
} // namespace cherrypi
