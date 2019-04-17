/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "cherrypi.h"
#include "unitsinfo.h"

#include <autogradpp/autograd.h>
#include <cereal/cereal.hpp>
#include <glog/logging.h>
#include <mapbox/variant.hpp>

#include <deque>
#include <functional>
#include <vector>

namespace cherrypi {

class State;

namespace features {
void initialize();
}

/**
 * Defines a family of "plain" features.
 *
 * These features can directly be extracted from the bot State into a spatial
 * FeatureData instance. Use featurizePlain() to extract these features.
 */
enum class PlainFeatureType {
  Invalid = -1,
  /// Ground height: 0 (low), 1 (high) or 2 (very high); -1 outside of map.
  GroundHeight = 1,
  /// Whether units can walk here or not: 0 or 1; -1 outside of map
  Walkability,
  /// Whether buildings can be placed here or not: 0 or 1; -1 outside of map
  Buildability,
  /// Whether this position is under the fog of war: 0 or 1; -1 outside of map
  FogOfWar,
  /// Whether there is creep here: 0 or 1; -1 outside of map
  Creep,
  /// Whether the enemy starts from this position: 0 or 1, -1 outside of map
  CandidateEnemyStartLocations,
  /// Whether the corresponding buildtile is reserved
  ReservedAsUnbuildable,
  /// Whether this walktile contains a doodad that alters the ground height and
  /// thus affects visibility and attack miss rates.
  TallDoodad,
  /// One-hot ground height: channel for height 0, 2, 4 and on the map (4
  /// total)
  OneHotGroundHeight,
  /// Whether this position is a starting location
  StartLocations,
  /// Grid of X/Y coordinates from (0,0) top left to (N,M) bottom right. One
  /// channel for Y, one channel for X. -1 outside of map. N is map_width/512, M
  /// map_height/512 (all in walktiles).
  XYGrid,
  /// 1 if there is a resource tile at this location, 0 otherwise
  Resources,
  /// This map tile has a structure on it, so it's not passable.
  /// Since this works at the walktile level and structures are on pixels,
  /// it will mark a walktile as impassable as long as the walktile is at
  /// all partially impassable.
  HasStructure,

  /// User-defined single-channel feature
  UserFeature1 = 1001,
  /// User-defined two channel feature
  UserFeature2 = 1002,
};

/**
 * Defines custom features.
 *
 * These features are extracted using various custom feature extractors. They're
 * defined explicitly so that they can be referred to easily in feature
 * descriptors.
 *
 * Use this enum as a central "registry" for your feature type.
 */
enum class CustomFeatureType {
  UnitPresence = 10001,
  UnitType,
  UnitFlags,
  UnitHP,
  UnitShield,
  UnitGroundCD,
  UnitAirCD,
  UnitStat,
  UnitTypeDefogger,
  UnitTypeMDefogger, // featurizes morphing units with target type

  Other = 1 >> 30,
};

using AnyFeatureType =
    mapbox::util::variant<PlainFeatureType, CustomFeatureType>;

/**
 * Decribes a specific feature within FeatureData.
 */
struct FeatureDescriptor {
  AnyFeatureType type;
  std::string name;
  int numChannels;

 private:
  static int constexpr kPlainType = 0;
  static int constexpr kCustomType = 1;

 public:
  FeatureDescriptor() {}
  FeatureDescriptor(PlainFeatureType type, std::string name, int numChannels)
      : type(type), name(std::move(name)), numChannels(numChannels) {}
  FeatureDescriptor(CustomFeatureType type, std::string name, int numChannels)
      : type(type), name(std::move(name)), numChannels(numChannels) {}
  FeatureDescriptor(FeatureDescriptor const& other)
      : type(other.type), name(other.name), numChannels(other.numChannels) {}
  FeatureDescriptor(FeatureDescriptor&& other)
      : type(std::move(other.type)),
        name(std::move(other.name)),
        numChannels(other.numChannels) {}
  ~FeatureDescriptor() = default;
  FeatureDescriptor& operator=(FeatureDescriptor const& other) {
    type = other.type;
    name = other.name;
    numChannels = other.numChannels;
    return *this;
  }
  FeatureDescriptor& operator=(FeatureDescriptor&& other) {
    type = std::move(other.type);
    name = std::move(other.name);
    numChannels = other.numChannels;
    return *this;
  }

  // Mostly for tests...
  bool operator==(FeatureDescriptor const& other) const;

  template <typename Archive>
  void save(Archive& ar) const {
    int kind = -1;
    int value = -1;
    type.match(
        [&](PlainFeatureType t) {
          kind = kPlainType;
          value = static_cast<int>(t);
        },
        [&](CustomFeatureType t) {
          kind = kCustomType;
          value = static_cast<int>(t);
        });
    ar(CEREAL_NVP(kind),
       CEREAL_NVP(value),
       CEREAL_NVP(name),
       CEREAL_NVP(numChannels));
  }

  template <typename Archive>
  void load(Archive& ar) {
    int kind = -1;
    int value = -1;
    ar(CEREAL_NVP(kind),
       CEREAL_NVP(value),
       CEREAL_NVP(name),
       CEREAL_NVP(numChannels));
    switch (kind) {
      case kPlainType:
        type = static_cast<PlainFeatureType>(value);
        break;
      case kCustomType:
        type = static_cast<CustomFeatureType>(value);
        break;
      default:
        throw std::runtime_error(
            "Unknown feature kind: " + std::to_string(kind));
    };
  }
};

/**
 * Represents a collection of spatial feature data.
 */
struct FeatureData {
  /// Format is [c][y][x]
  torch::Tensor tensor;
  std::vector<FeatureDescriptor> desc;
  /// Decimation factor wrt walktile resolution
  int scale;
  /// [0][0] of tensor corresponds to this point (walktiles)
  Position offset;

  /// Number of channels in tensor
  int numChannels() const;
  /// Bounding box in walktiles
  Rect boundingBox() const;
  /// Bounding box in current scale
  Rect boundingBoxAtScale() const;

  template <typename Archive>
  void serialize(Archive& ar) {
    ar(CEREAL_NVP(tensor),
       CEREAL_NVP(desc),
       CEREAL_NVP(scale),
       CEREAL_NVP(offset));
  }
};

/**
 * Various methods for spatial subsampling.
 */
enum class SubsampleMethod {
  Sum,
  Average,
  Max,
};

/**
 * Extracts plain features from the current state.
 * boundingBox defaults to all available data, but can also be larger to have
 * constant-size features irrespective of actual map size, for example.
 */
FeatureData featurizePlain(
    State* state,
    std::vector<PlainFeatureType> types,
    Rect boundingBox = Rect());

/**
 * Combines multiple features along channels.
 * Ensures they have the same scale and performs zero-padding according to
 * feature offsets.
 */
FeatureData combineFeatures(std::vector<FeatureData> const& feats);

/**
 * Selects a subset of features.
 * Assumes that types is a subset of the ones the feat. If not, you'll get
 * an exception. Reorders types from feat to be as in types.
 */
FeatureData selectFeatures(
    FeatureData const& feat,
    std::vector<AnyFeatureType> types);

/**
 * Applies a spatial subsampling method to a feature.
 * The scale of the resulting feature will be original scale times the given
 * factor.
 */
FeatureData subsampleFeature(
    FeatureData const& feat,
    SubsampleMethod method,
    int factor,
    int stride = -1);

/**
 * Maps walktile positions to feature positions for a given bounding box.
 *
 * This is mostly useful for actual featurizer implementations.  Use `(x, y)` to
 * map a position. For invalid positions (outside of the intersection of
 * bounding box and map rectangle), `(-1, -1)` is returned.
 */
struct FeaturePositionMapper {
  FeaturePositionMapper(Rect const& boundingBox, Rect const& mapRect) {
    Rect ir = boundingBox.intersected(mapRect);
    irx1 = ir.left();
    iry1 = ir.top();
    irx2 = ir.right() - 1;
    iry2 = ir.bottom() - 1;
    offx = mapRect.x - boundingBox.x;
    offy = mapRect.y - boundingBox.y;
  }
  int irx1;
  int irx2;
  int iry1;
  int iry2;
  int offx;
  int offy;

  Position operator()(Position const& pos) const {
    if (pos.x < irx1 || pos.y < iry1 || pos.x > irx2 || pos.y > iry2) {
      return kInvalidPosition;
    }
    return Position(pos.x + offx, pos.y + offy);
  }
};

} // namespace cherrypi
