/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "str.h"
#include <algorithm>
#include <cstring>
#include <vector>

namespace common {
/**
 * Split a string into parts deliminted by the given separtion character.
 *
 * This will repeatedly call `getline()` with `sep` as the delimitation
 * character. If `max` is >= 0, at most `max` splits will be performed
 * (cf. Python's split() function).
 */
std::vector<std::string>
stringSplit(char const* str, size_t len, char sep, size_t max) {
  char const* bptr = str;
  char const* eptr = str;
  std::vector<std::string> result;
  if (max == 0) {
    result.emplace_back(bptr, str + len);
    return result;
  }

  while (true) {
    if (eptr == str + len) {
      result.emplace_back(bptr, eptr);
      break;
    } else if (*eptr == sep) {
      result.emplace_back(bptr, eptr);
      if (result.size() == max && eptr + 1 < str + len) {
        result.emplace_back(eptr + 1, str + len);
        break;
      }
      bptr = eptr + 1;
    }
    ++eptr;
  }
  return result;
}

std::vector<std::string> stringSplit(char const* str, char sep, size_t max) {
  return stringSplit(str, strlen(str), sep, max);
}

std::vector<std::string>
stringSplit(std::string const& str, char sep, size_t max) {
  return stringSplit(str.c_str(), str.size(), sep, max);
}

bool startsWith(std::string const& str, std::string const& prefix) {
  if (str.size() < prefix.size()) {
    return false;
  }
  return str.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(std::string const& str, std::string const& suffix) {
  if (str.size() < suffix.size()) {
    return false;
  }
  return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace common
