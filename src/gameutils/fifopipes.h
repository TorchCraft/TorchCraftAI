/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>

namespace cherrypi {
namespace detail {
struct FifoPipes {
  std::string pipe1;
  std::string pipe2;

  FifoPipes();
  ~FifoPipes();

 private:
  std::string root_;
};
} // namespace detail
} // namespace cherrypi