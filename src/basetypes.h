/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cereal/cereal.hpp>

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <utility>

namespace cherrypi {

typedef int PlayerId;
typedef int FrameNum;
typedef int UpcId;
UpcId constexpr kRootUpcId = 0;
UpcId constexpr kInvalidUpcId = -1;
UpcId constexpr kFilteredUpcId = -2;

double constexpr kDegPerRad = 180 / M_PI;
float constexpr kfInfty = std::numeric_limits<float>::infinity();
float constexpr kfLowest = std::numeric_limits<float>::lowest();
float constexpr kfMax = std::numeric_limits<float>::max();
float constexpr kfEpsilon = std::numeric_limits<float>::epsilon();
double constexpr kdInfty = std::numeric_limits<double>::infinity();
double constexpr kdLowest = std::numeric_limits<double>::lowest();
double constexpr kdMax = std::numeric_limits<double>::max();
double constexpr kdEpsilon = std::numeric_limits<double>::epsilon();
int constexpr kForever = 24 * 60 * 60 * 24 * 7;
constexpr int kLarvaFrames = 342;

#define DEFINE_FLAG_OPERATORS(Type)                    \
  inline Type operator|(Type a, Type b) {              \
    return static_cast<Type>(                          \
        static_cast<std::underlying_type_t<Type>>(a) | \
        static_cast<std::underlying_type_t<Type>>(b)); \
  }                                                    \
  inline Type operator&(Type a, Type b) {              \
    return static_cast<Type>(                          \
        static_cast<std::underlying_type_t<Type>>(a) & \
        static_cast<std::underlying_type_t<Type>>(b)); \
  }


template <typename T>
class Vec2T {
 public:
  T x;
  T y;

  constexpr Vec2T() : x(0), y(0) {}
  constexpr Vec2T(T x, T y) : x(x), y(y) {}
  template <typename U>
  explicit Vec2T(U const& other) : x(other.x), y(other.y) {}
  template <typename U>
  explicit Vec2T(U* other) : x(other->x), y(other->y) {}
  template <typename U, typename V>
  explicit Vec2T(std::pair<U, V> const& other) : x(other.first), y(other.second) {}

  Vec2T& operator=(Vec2T const& other) {
    x = other.x;
    y = other.y;
    return *this;
  }
  
  bool operator==(Vec2T const& other) const {
    return x == other.x && y == other.y;
  }
  bool operator!=(Vec2T const& other) const {
    return x != other.x || y != other.y;
  }
  // For use in std::map as a key, since it's ordered and keeps a compare
  bool operator<(Vec2T<T> const& other) const {
    return x < other.x || (x == other.x && y < other.y);
  }
  
  Vec2T operator+(T scalar) const {
    return Vec2T(x + scalar, y + scalar);
  }
  Vec2T operator-(T scalar) const {
    return Vec2T(x - scalar, y - scalar);
  }
  Vec2T operator*(T scalar) const {
    return Vec2T(x * scalar, y * scalar);
  }
  Vec2T operator/(T scalar) const {
    return Vec2T(x / scalar, y / scalar);
  }
  Vec2T operator+(Vec2T const& other) const {
    return Vec2T(x + other.x, y + other.y);
  }
  Vec2T operator-(Vec2T const& other) const {
    return Vec2T(x - other.x, y - other.y);
  }
  Vec2T& operator+=(T scalar) {
    x += scalar;
    y += scalar;
    return *this;
  }
  Vec2T& operator-=(T scalar) {
    x -= scalar;
    y -= scalar;
    return *this;
  }
  Vec2T& operator*=(T scalar) {
    x *= scalar;
    y *= scalar;
    return *this;
  }
  Vec2T& operator/=(T scalar) {
    x /= scalar;
    y /= scalar;
    return *this;
  }
  Vec2T& operator+=(Vec2T const& other) {
    x += other.x;
    y += other.y;
    return *this;
  }
  Vec2T& operator-=(Vec2T const& other) {
    x -= other.x;
    y -= other.y;
    return *this;
  }

  double distanceTo(Vec2T const& other) const {
    return Vec2T(other.x - x, other.y - y).length();
  }
  template <typename U>
  double distanceTo(U* other) const {
    return Vec2T(other->x - x, other->y - y).length();
  }
  double length() const {
    return std::sqrt(x * x + y * y);
  }
  Vec2T& normalize() {
    // Warning -- using this on Vec2<int> is a bad idea
    // because *= casts the multiplier as an int
    double len = length();
    *this *= len == 0 ? 1. : 1. / len;
    return *this;
  }
  Vec2T& rotateDegrees(double degrees) {
    double radians = degrees / kDegPerRad;
    double sine = std::sin(radians), cosine = std::cos(radians);
    double xNew = x * cosine - y * sine;
    double yNew = x * sine + y * cosine;
    x = xNew;
    y = yNew;
    return *this;
  }

  static double cos(Vec2T const& a, Vec2T const& b) {
    double denominator = a.length() * b.length();
    return denominator == 0 ? 0 : Vec2T::dot(a, b) / denominator;
  }
  static T dot(Vec2T const& a, Vec2T const& b) {
    return a.x * b.x + a.y * b.y;
  }
  T dot(Vec2T const& other) {
    return dot(*this, other);
  }
  static T cross(Vec2T const& a, Vec2T const& b) {
    return (a.x * b.y) - (a.y * b.x);
  }

  template <class Archive>
  void serialize(Archive& ar) {
    ar(CEREAL_NVP(x), CEREAL_NVP(y));
  }
};

typedef Vec2T<float> Vec2;
typedef Vec2T<int> Position;
constexpr Position kInvalidPosition{-1, -1};

template <typename T>
class Rect2T {
 public:
  T x;
  T y;
  T w;
  T h;

  Rect2T() : x(0), y(0), w(0), h(0) {}
  Rect2T(T x, T y, T width, T height) : x(x), y(y), w(width), h(height) {}
  Rect2T(Vec2T<T> const& topLeft, Vec2T<T> const& bottomRight)
      : x(topLeft.x),
        y(topLeft.y),
        w(bottomRight.x - topLeft.x),
        h(bottomRight.y - topLeft.y) {}
  Rect2T(Vec2T<T> const& topLeft, T width, T height)
      : x(topLeft.x), y(topLeft.y), w(width), h(height) {}
  template <typename T2>
  Rect2T(Rect2T<T2> const& r) : x(r.x), y(r.y), w(r.w), h(r.h) {}
  template <typename T2>
  Rect2T<T>& operator=(Rect2T<T2> const& r) {
    x = r.x;
    y = r.y;
    w = r.w;
    h = r.h;
  }

  bool operator==(Rect2T<T> const& r) const {
    return x == r.x && y == r.y && w == r.w && h == r.w;
  }

  T left() const {
    return x;
  }
  T right() const {
    return x + w;
  }
  T top() const {
    return y;
  }
  T bottom() const {
    return y + h;
  }
  T width() const {
    return w;
  }
  T height() const {
    return h;
  }

  Vec2T<T> center() const {
    return Vec2T<T>(x + w / 2, y + h / 2);
  }
  static Rect2T<T> centeredWithSize(Vec2T<T> const& center, T width, T height) {
    Rect2T<T> t;
    t.x = center.x - width / 2;
    t.y = center.y - height / 2;
    t.w = width;
    t.h = height;
    return t;
  }

  bool null() const {
    return w == T(0) && h == T(0);
  }
  bool empty() const {
    return w <= T(0) && h <= T(0);
  }

  Rect2T<T> united(Rect2T<T> const& r) {
    if (empty()) {
      return r;
    }
    if (r.empty()) {
      return *this;
    }

    Rect2T<T> t;
    t.x = std::min(x, r.x);
    t.y = std::min(y, r.y);
    t.w = std::max(x + w, r.x + r.w) - t.x;
    t.h = std::max(y + h, r.y + r.h) - t.y;
    return t;
  }

  Rect2T<T> intersected(Rect2T<T> const& r) const {
    if (empty() || r.empty()) {
      return Rect2T<T>();
    }

    T left1 = x;
    T right1 = x + w;
    T left2 = r.x;
    T right2 = r.x + r.w;

    if (left1 >= right2 || left2 >= right1) {
      // No intersection
      return Rect2T<T>();
    }

    T top1 = y;
    T bottom1 = y + h;
    T top2 = r.y;
    T bottom2 = r.y + r.h;

    if (top1 >= bottom2 || top2 >= bottom1) {
      // No intersection
      return Rect2T<T>();
    }

    Rect2T<T> t;
    t.x = std::max(left1, left2);
    t.y = std::max(top1, top2);
    t.w = std::min(right1, right2) - t.x;
    t.h = std::min(bottom1, bottom2) - t.y;
    return t;
  }

  bool contains(const Vec2T<T>& pt) const {
    return pt.x >= left() && pt.x < right() && pt.y >= top() && pt.y < bottom();
  }

  template <class Archive>
  void serialize(Archive& ar) {
    ar(CEREAL_NVP(x), CEREAL_NVP(y), CEREAL_NVP(w), CEREAL_NVP(h));
  }
};

typedef Rect2T<int> Rect;

/**
 * Abstract "meta" commands for UPCTuples.
 */
enum Command : uint64_t {
  // clang-format off
  None              = 0,
  Create            = 1 << 0,
  Move              = 1 << 1,
  Delete            = 1 << 2, // Kill things (ie. attack)
  Gather            = 1 << 3,
  Scout             = 1 << 4,
  Cancel            = 1 << 5,
  Harass            = 1 << 6,
  Flee              = 1 << 7,
  SetCreatePriority = 1 << 8,
  ReturnCargo       = 1 << 9,
  MAX               = 1 << 10,
  // clang-format on
};

/// Does not count the "None" command
int constexpr numUpcCommands() {
  static_assert(Command::MAX > 0, "Invalid MAX command value");
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_ctzl(Command::MAX);
#else
  std::underlying_type<Command>::type n = Command::MAX;
  int count = 0;
  while (n % 2 == 0) {
    count++;
    n >>= 1;
  }
  return count;
#endif
}
} // namespace cherrypi

namespace std {
template <typename T>
inline ostream& operator<<(ostream& strm, cherrypi::Vec2T<T> const& p) {
  return strm << "(" << p.x << "," << p.y << ")";
}

template <typename T>
inline ostream& operator<<(ostream& strm, cherrypi::Rect2T<T> const& r) {
  return strm << "(" << r.x << "," << r.y << " " << r.w << "x" << r.h << ")";
}

template <typename T>
struct hash<cherrypi::Vec2T<T>> {
  size_t operator()(cherrypi::Vec2T<T> const& pos) const {
    return hashT(pos.x * pos.y) ^ hashT(pos.y);
  }

 private:
  function<size_t(T)> hashT = hash<T>();
};

template <>
struct hash<cherrypi::Command> {
  using utype = std::underlying_type<cherrypi::Command>::type;
  size_t operator()(cherrypi::Command const& cmd) const {
    return h(static_cast<utype>(cmd));
  }

 private:
  hash<utype> h;
};
} // namespace std
