/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <algorithm>
#include <cassert>

namespace common {

template <class T, class Compare>
constexpr const T& clamp(const T& v, const T& lo, const T& hi, Compare comp) {
  return assert(!comp(hi, lo)), comp(v, lo) ? lo : comp(hi, v) ? hi : v;
}

template <class T>
constexpr const T& clamp(const T& v, const T& lo, const T& hi) {
  return common::clamp(v, lo, hi, std::less<>());
}

template <class T>
constexpr const T& safeClamp(const T& v1, const T& v2, const T& v3) {
  const T& min = std::min(v2, v3);
  const T& max = std::max(v2, v3);
  return std::min(max, std::max(min, v1));
}

} // namespace common
