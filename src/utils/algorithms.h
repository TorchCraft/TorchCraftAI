/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <algorithm>
#include <math.h>
#include <torchcraft/client.h>
#include <type_traits>
#include <vector>

#ifdef HAVE_TORCH
#include <autogradpp/autograd.h>
#endif // HAVE_TORCH

#include "buildtype.h"
#include "cherrypi.h"
#include "filter.h"
#include "gamemechanics.h"
#include "state.h"
#include "unitsinfo.h"

namespace cherrypi {
namespace utils {

struct NoValue {};
static constexpr NoValue noValue{};
template <typename A, typename B>
constexpr bool isEqualButNotNoValue(A&& a, B&& b) {
  return a == b;
}
template <typename A>
constexpr bool isEqualButNotNoValue(A&& a, NoValue) {
  return false;
}
template <typename B>
constexpr bool isEqualButNotNoValue(NoValue, B&& b) {
  return false;
}

/// This function iterates from begin to end, passing each value to the
/// provided score function.
/// It returns the corresponding iterator for the value whose score function
/// returned the lowest value (using operator <).
/// If invalidScore is provided, then any score which compares equal to it will
/// never be returned.
/// If bestPossibleScore is provided, then any score which compares equal to it
/// (and also is the best score thus far) will cause an immediate return,
/// without iterating to the end.
/// If the range is empty or no value can be returned (due to invalidScore),
/// then the end iterator is returned.
template <typename Iterator,
          typename Score,
          typename InvalidScore = NoValue,
          typename BestPossibleScore = NoValue>
auto getBestScore(
    Iterator begin,
    Iterator end,
    Score&& score,
    InvalidScore&& invalidScore = InvalidScore(),
    BestPossibleScore&& bestPossibleScore = BestPossibleScore()) {
  if (begin == end) {
    return end;
  }
  auto i = begin;
  auto best = i;
  auto bestScore = score(*i);
  ++i;
  if (isEqualButNotNoValue(bestScore, invalidScore)) {
    best = end;
    for (; i != end; ++i) {
      auto s = score(*i);
      if (isEqualButNotNoValue(s, invalidScore)) {
        continue;
      }
      best = i;
      bestScore = s;
      if (isEqualButNotNoValue(s, bestPossibleScore)) {
        return best;
      }
      break;
    }
  }
  for (; i != end; ++i) {
    auto s = score(*i);
    if (isEqualButNotNoValue(s, invalidScore)) {
      continue;
    }
    if (s < bestScore) {
      best = i;
      bestScore = s;
      if (isEqualButNotNoValue(s, bestPossibleScore)) {
        break;
      }
    }
  }
  return best;
}

/// This function is equivalent to getBestScore, but it can be passed a range
/// or container instead of two iterators.
/// The return value is still an iterator.
template <typename Range,
          typename Score,
          typename InvalidScore = NoValue,
          typename BestPossibleScore = NoValue>
auto getBestScore(
    Range&& range,
    Score&& score,
    InvalidScore&& invalidScore = InvalidScore(),
    BestPossibleScore&& bestPossibleScore = BestPossibleScore()) {
  return getBestScore(
      range.begin(),
      range.end(),
      std::forward<Score>(score),
      std::forward<InvalidScore>(invalidScore),
      std::forward<BestPossibleScore>(bestPossibleScore));
}

/// This function is the same as getBestScore, but it returns a copy of the
/// value retrieved by dereferencing the returned iterator (using auto type
/// semantics; it's a copy, not a reference).
/// If the end iterator would be returned, a value initialized object is
/// returned as if by T{}.
template <typename Range,
          typename Score,
          typename InvalidScore = NoValue,
          typename BestPossibleScore = NoValue>
auto getBestScoreCopy(
    Range&& range,
    Score&& score,
    InvalidScore&& invalidScore = InvalidScore(),
    BestPossibleScore&& bestPossibleScore = BestPossibleScore()) {
  auto i = getBestScore(
      range.begin(),
      range.end(),
      std::forward<Score>(score),
      std::forward<InvalidScore>(invalidScore),
      std::forward<BestPossibleScore>(bestPossibleScore));
  if (i == range.end()) {
    return typename std::remove_reference<decltype(*i)>::type{};
  }
  return *i;
}

/// This function is the same as getBestScore, but it returns a pointer
/// to the value of the dereferenced result iterator, or nullptr if the
/// end iterator would be returned.
template <typename Range,
          typename Score,
          typename InvalidScore = NoValue,
          typename BestPossibleScore = NoValue>
auto getBestScorePointer(
    Range&& range,
    Score&& score,
    InvalidScore&& invalidScore = InvalidScore(),
    BestPossibleScore&& bestPossibleScore = BestPossibleScore()) {
  auto i = getBestScore(
      range.begin(),
      range.end(),
      std::forward<Score>(score),
      std::forward<InvalidScore>(invalidScore),
      std::forward<BestPossibleScore>(bestPossibleScore));
  if (i == range.end()) {
    return (decltype(&*i)) nullptr;
  }
  return &*i;
}

inline std::string buildTypeString(BuildType const* buildType) {
  return (buildType ? buildType->name : "null");
}

template <typename Units>
inline Position centerOfUnits(Units&& units) {
  Position p;
  if (units.size() != 0) {
    for (Unit const* unit : units) {
      p += Position(unit);
    }
    p /= units.size();
  } else {
    VLOG(2) << "Center of no units is (0, 0)";
    return Position(0, 0);
  }
  return p;
}

template <typename InputIterator>
inline Position centerOfUnits(InputIterator start, InputIterator end) {
  Position p(0, 0);
  auto size = 0U;
  if (start == end) {
    VLOG(2) << "Center of no units is (0, 0)";
    return Position(0, 0);
  }
  for (; start != end; start++, size++) {
    p += Position(*start);
  }
  p /= size;
  return p;
}

inline bool isWithinRadius(Unit* unit, int32_t x, int32_t y, float radius) {
  return distance(unit->x, unit->y, x, y) <= radius;
}

template <typename Units>
inline std::vector<Unit*>
filterUnitsByDistance(Units&& units, int32_t x, int32_t y, float radius) {
  return filterUnits(
      units, [=](Unit* u) { return isWithinRadius(u, x, y, radius); });
}

// Determine the closest unit to a given position
template <typename It>
inline It getClosest(int x, int y, It first, It last) {
  It closest = last;
  float mind = FLT_MAX;
  while (first != last) {
    float d = float(x - first->x) * (x - first->x) +
        float(y - first->y) * (y - first->y);
    if (d < mind) {
      closest = first;
      mind = d;
    }
    ++first;
  }
  return closest;
}

template <typename Units>
std::unordered_set<Unit*> findNearbyEnemyUnits(State* state, Units&& units) {
  auto& enemyUnits = state->unitsInfo().enemyUnits();
  std::unordered_set<Unit*> nearby;
  for (auto unit : units) {
    // from UAlbertaBot
    auto wRange = 75;
    for (auto enemy :
         filterUnitsByDistance(enemyUnits, unit->x, unit->y, wRange)) {
      // XXX What if it's gone??
      if (!enemy->gone) {
        nearby.insert(enemy);
      }
    }
  }
  return nearby;
}

#ifdef HAVE_TORCH
// Returns argmax (x,y) and value in walktiles
inline std::tuple<int, int, float> argmax(torch::Tensor const& pos, int scale) {
  if (!pos.defined() || pos.dim() != 2) {
    throw std::runtime_error("Two-dimensional tensor expected");
  }
  // ATen needs a const accessor...
  auto acc = const_cast<torch::Tensor&>(pos).accessor<float, 2>();
  int xmax = 0;
  int ymax = 0;
  float max = kfLowest;
  for (int y = 0; y < acc.size(0); y++) {
    for (int x = 0; x < acc.size(1); x++) {
      auto el = acc[y][x];
      if (el > max) {
        max = el;
        xmax = x;
        ymax = y;
      }
    }
  }

  return std::make_tuple(xmax * scale, ymax * scale, max);
}
#endif // HAVE_TORCH

template <typename T>
inline void inplace_flat_vector_add(
    std::vector<T>& in,
    const std::vector<T>& add) {
  for (size_t i = 0; i < in.size(); ++i) {
    in[i] += add[i];
  }
}

template <typename T>
inline void inplace_flat_vector_addcmul(
    std::vector<T>& in,
    const std::vector<T>& mul1,
    const std::vector<T>& mul2) {
  for (size_t i = 0; i < in.size(); ++i) {
    in[i] += mul1[i] * mul2[i];
  }
}
template <typename T>
inline void inplace_flat_vector_addcmul(
    std::vector<T>& in,
    const std::vector<T>& mul1,
    T mul2) {
  for (size_t i = 0; i < in.size(); ++i) {
    in[i] += mul1[i] * mul2;
  }
}

template <typename T>
inline void inplace_flat_vector_div(std::vector<T>& in, T div) {
  for (size_t i = 0; i < in.size(); ++i) {
    in[i] /= div;
  }
}

template <typename T>
inline T l2_norm_vector(const std::vector<T>& v) {
  T s2 = 0;
  for (auto& e : v)
    s2 += pow(e, 2);
  return sqrt(s2);
}

template <typename T>
inline size_t argmax(const std::vector<T>& v) {
  return std::distance(v.begin(), std::max_element(v.begin(), v.end()));
}

template <typename TCollection, typename TKey>
bool contains(TCollection& collection, TKey& key) {
  return collection.find(key) != collection.end();
}

namespace detail {
// Add overload for specific container below if needed
template <typename K, typename V, typename U>
void cmergeInto(std::map<K, V>& dest, U&& src) {
  dest.insert(src.begin(), src.end());
}
template <typename K, typename V, typename U>
void cmergeInto(std::unordered_map<K, V>& dest, U&& src) {
  dest.insert(src.begin(), src.end());
}
template <typename T, typename U>
void cmergeInto(T& dest, U&& src) {
  dest.insert(dest.end(), src.begin(), src.end());
}
} // namespace detail

/// Merges two or more STL containers.
/// For associative containers, values will not be overwritten during merge,
/// i.e. for duplicate keys, the value of the *first* argument containing that
/// key will be used.
template <typename T, typename... Args>
inline typename std::remove_reference<T>::type cmerge(T&& c1, Args&&... cs) {
  typename std::remove_reference<T>::type m;
  detail::cmergeInto(m, c1);
  (void)std::initializer_list<int>{(detail::cmergeInto(m, cs), 0)...};
  return m;
}

} // namespace utils
} // namespace cherrypi
