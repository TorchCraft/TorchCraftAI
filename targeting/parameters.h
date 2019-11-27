/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#include <unordered_map>

class Parameters {
 public:
  static void init();
  static void perturbate();
  static float get_float(const std::string& key);
  static int get_int(const std::string& key);
  static void broadcast(int rank);

 protected:
  static std::unordered_map<std::string, int> int_params;
  static std::unordered_map<std::string, float> float_params;
};
