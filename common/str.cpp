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
namespace {
/*
 * Glob-style pattern matching from Redis (src/util.c)
 * BSD-licensed, Copyright (c) 2009-2012, Salvatore Sanfilippo
 */
int stringmatchlen(
    const char* pattern,
    int patternLen,
    const char* string,
    int stringLen,
    int nocase) {
  while (patternLen && stringLen) {
    switch (pattern[0]) {
      case '*':
        while (pattern[1] == '*') {
          pattern++;
          patternLen--;
        }
        if (patternLen == 1)
          return 1; /* match */
        while (stringLen) {
          if (stringmatchlen(
                  pattern + 1, patternLen - 1, string, stringLen, nocase))
            return 1; /* match */
          string++;
          stringLen--;
        }
        return 0; /* no match */
        break;
      case '?':
        if (stringLen == 0)
          return 0; /* no match */
        string++;
        stringLen--;
        break;
      case '[': {
        int not_, match;

        pattern++;
        patternLen--;
        not_ = pattern[0] == '^';
        if (not_) {
          pattern++;
          patternLen--;
        }
        match = 0;
        while (1) {
          if (pattern[0] == '\\' && patternLen >= 2) {
            pattern++;
            patternLen--;
            if (pattern[0] == string[0])
              match = 1;
          } else if (pattern[0] == ']') {
            break;
          } else if (patternLen == 0) {
            pattern--;
            patternLen++;
            break;
          } else if (pattern[1] == '-' && patternLen >= 3) {
            int start = pattern[0];
            int end = pattern[2];
            int c = string[0];
            if (start > end) {
              int t = start;
              start = end;
              end = t;
            }
            if (nocase) {
              start = tolower(start);
              end = tolower(end);
              c = tolower(c);
            }
            pattern += 2;
            patternLen -= 2;
            if (c >= start && c <= end)
              match = 1;
          } else {
            if (!nocase) {
              if (pattern[0] == string[0])
                match = 1;
            } else {
              if (tolower((int)pattern[0]) == tolower((int)string[0]))
                match = 1;
            }
          }
          pattern++;
          patternLen--;
        }
        if (not_)
          match = !match;
        if (!match)
          return 0; /* no match */
        string++;
        stringLen--;
        break;
      }
      case '\\':
        if (patternLen >= 2) {
          pattern++;
          patternLen--;
        }
        /* fall through */
      default:
        if (!nocase) {
          if (pattern[0] != string[0])
            return 0; /* no match */
        } else {
          if (tolower((int)pattern[0]) != tolower((int)string[0]))
            return 0; /* no match */
        }
        string++;
        stringLen--;
        break;
    }
    pattern++;
    patternLen--;
    if (stringLen == 0) {
      while (*pattern == '*') {
        pattern++;
        patternLen--;
      }
      break;
    }
  }
  if (patternLen == 0 && stringLen == 0)
    return 1;
  return 0;
}
} // namespace

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

bool gmatch(std::string_view str, std::string_view pattern) {
  return stringmatchlen(
      pattern.data(), pattern.size(), str.data(), str.size(), 0);
}

bool gmatchi(std::string_view str, std::string_view pattern) {
  return stringmatchlen(
      pattern.data(), pattern.size(), str.data(), str.size(), 1);
}

} // namespace common
