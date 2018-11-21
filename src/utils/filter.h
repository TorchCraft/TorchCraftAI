
/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

namespace cherrypi {
namespace utils {

template <typename Units, typename UnaryPredicate>
inline auto filterUnits(Units&& units, UnaryPredicate pred) {
  std::vector<typename std::decay<Units>::type::value_type> result;
  std::copy_if(units.begin(), units.end(), std::back_inserter(result), pred);
  return result;
}

template <typename Units, typename UnaryPredicate>
inline auto countUnits(Units&& units, UnaryPredicate pred) {
  return std::count_if(units.begin(), units.end(), pred);
}

inline std::vector<tc::Unit> filterUnitsByType(
    std::vector<tc::Unit> const& units,
    tc::BW::UnitType type) {
  return filterUnits(
      units, [type](tc::Unit const& u) { return u.type == type; });
}

inline std::vector<tc::Unit> filterUnitsByTypes(
    std::vector<tc::Unit> const& units,
    std::vector<tc::BW::UnitType> const& types) {
  return filterUnits(units, [types](tc::Unit const& u) {
    for (auto type : types) {
      if (u.type == type) {
        return true;
      }
    }
    return false;
  });
}

inline std::vector<tc::Unit> filterUnitsByType(
    std::vector<tc::Unit> const& units,
    std::function<bool(tc::BW::UnitType)> const& pred) {
  return filterUnits(units, [pred](tc::Unit const& u) {
    auto ut = tc::BW::UnitType::_from_integral_nothrow(u.type);
    if (ut) {
      return pred(*ut);
    }
    return false;
  });
}

} // namespace utils
} // namespace cherrypi
