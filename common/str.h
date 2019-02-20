/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <algorithm>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace common {

template <typename T>
std::string stringToLower(T&& str);

/**
 * Split a string into parts deliminted by the given separtion character.
 *
 * This will repeatedly call `getline()` with `sep` as the delimitation
 * character. If `max` is >= 0, at most `max` splits will be performed
 * (cf. Python's split() function).
 */
std::vector<std::string>
stringSplit(char const* str, size_t len, char sep, size_t max = -1);

std::vector<std::string>
stringSplit(char const* str, char sep, size_t max = -1);

std::vector<std::string>
stringSplit(std::string const& str, char sep, size_t max = -1);

template <typename T>
std::string joinVector(std::vector<T> const& v, char sep);

bool startsWith(std::string const& str, std::string const& prefix);

bool endsWith(std::string const& str, std::string const& suffix);

/// Glob-style pattern matching
bool gmatch(std::string_view str, std::string_view pattern);

/// Glob-style pattern matching (case-insensitive)
bool gmatchi(std::string_view str, std::string_view pattern);

/**************** IMPLEMENTATIONS ********************/

template <typename T>
std::string stringToLower(T&& str) {
  std::string lowered;
  lowered.resize(str.size());
  std::transform(str.begin(), str.end(), lowered.begin(), tolower);
  return lowered;
}

template <typename T>
std::string joinVector(std::vector<T> const& v, char sep) {
  std::ostringstream oss;
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      oss << sep;
    }
    oss << v[i];
  }
  return oss.str();
}

} // namespace common
