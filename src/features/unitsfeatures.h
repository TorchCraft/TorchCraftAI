/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "features/features.h"
#include "features/jitter.h"

namespace cherrypi {

/**
 * Abstract base class for featurizing unit attributes in a sparse manner.
 *
 * General usage of sub-classes for actual feature extraction boils down to
 * calling extract() with a desired subset of units to featurize. The resulting
 * data is sparse wrt positions, i.e. it contains a tensor of positions for each
 * unit and the accompanying data as defined by a featurizer implementation.
 *
 * toSpatialFeature() will transform the given data to a `FeatureData` object,
 * i.e. it will place the feature data at the respective positions.
 *
 * Optionally, users can set a jittering method that will be accounted for in
 * extract().
 */
struct UnitAttributeFeaturizer {
  using UnitFilter = std::function<bool(Unit*)>;
  using TensorDest = torch::TensorAccessor<float, 1>;

  struct Data {
    Rect boundingBox;
    // TODO Undefined position and data tensors currently represent an empty set
    // of units. Are tensors with a 0-sized dimension possible?
    torch::Tensor positions; /// #units X 2 (y, x)
    torch::Tensor data; /// #units X nchannels

    template <typename Archive>
    void serialize(Archive& ar) {
      ar(CEREAL_NVP(boundingBox), CEREAL_NVP(positions), CEREAL_NVP(data));
    }
  };

  /// Optional jittering of unit positions
  std::shared_ptr<BaseJitter> jitter = std::make_shared<NoJitter>();

  // This information will be used for converting this featurizer's data into
  // a spatial feature. Subclasses should customize this in their constructors.
  CustomFeatureType type;
  std::string name;
  int numChannels;

  virtual ~UnitAttributeFeaturizer() = default;

  /// Extract unit features for a given set of units
  virtual Data extract(
      State* state,
      UnitsInfo::Units const& units,
      Rect const& boundingBox = Rect());
  /// Extract unit features for all live units
  Data extract(State* state, Rect const& boundingBox = Rect());
  /// Extract unit features for all live units that pass the given filter
  Data
  extract(State* state, UnitFilter filter, Rect const& boundingBox = Rect());

  /// Embeds the unit attribute data into a spatial feature
  FeatureData toSpatialFeature(
      Data const& data,
      SubsampleMethod pooling = SubsampleMethod::Sum) const;

  /// Embeds the unit attribute data into a spatial feature.
  /// This version will re-use the tensor memory of the given feature data
  /// instance.
  void toSpatialFeature(
      FeatureData* dest,
      Data const& data,
      SubsampleMethod pooling = SubsampleMethod::Sum) const;

 protected:
  /// Reimplement this in actual featurizers.
  /// This function is expected to set acc[0], ..., acc[numChannels-1]
  virtual void extractUnit(TensorDest acc, Unit* unit) = 0;
};

/**
 * Sparse featurizer for unit presence.
 *
 * This will produce a binary feature with a single channel: 0 if there is no
 * unit, 1 if there is a unit.
 */
struct UnitPresenceFeaturizer : UnitAttributeFeaturizer {
  UnitPresenceFeaturizer() {
    type = CustomFeatureType::UnitPresence;
    name = "UnitPresence";
    numChannels = 1;
  }

 protected:
  virtual void extractUnit(TensorDest acc, Unit*) override {
    // Simply mark this unit as being present
    acc[0] = 1;
  }
};

/**
 * Sparse featurizer for numeric unit types.
 *
 * This will produce a single-channel feature that contains a unit type ID for
 * each unit. Unit IDs are mutually exclusive for allied (0-232), enemy
 * (233-465) and neutral units (466-698).
 *
 * The resulting sparse feature is suitable for embedding via lookup tables.
 */
struct UnitTypeFeaturizer : UnitAttributeFeaturizer {
  static int constexpr kNumUnitTypes = 233 * 3;

  UnitTypeFeaturizer() {
    type = CustomFeatureType::UnitType;
    name = "UnitType";
    numChannels = 1;
  }

  FeatureData toOneHotSpatialFeature(
      Data const& data,
      int unitValueOffset,
      std::unordered_map<int, int> const& channelValues) const;

 protected:
  virtual void extractUnit(TensorDest acc, Unit* unit) override {
    if (unit->isMine) {
      acc[0] = unit->type->unit + 233 * 0;
    } else if (unit->isEnemy) {
      acc[0] = unit->type->unit + 233 * 1;
    } else if (unit->isNeutral) {
      acc[0] = unit->type->unit + 233 * 2;
    }
  }
};

/**
 * Sparse featurizer for unit types, defogger-style.
 *
 * This featurizer maps unit types to 118 IDs (instead of the 234 possible IDs)
 * and assigns valid IDs to allied and enemy units only -- neutral units will be
 * be mapped to -1.
 *
 * `toDefoggerFeature()` supports pooling with a given resolution and
 * stride so that the result contains accumulated unit counts per type for each
 * "cell". It ignores neutral units.
 */
struct UnitTypeDefoggerFeaturizer : UnitAttributeFeaturizer {
  static int constexpr kNumUnitTypes = 118 * 2;

  UnitTypeDefoggerFeaturizer();

  int mapType(int unitType) const {
    return typemap_->at(unitType);
  }
  int unmapType(int mappedType) const {
    return itypemap_->at(mappedType);
  }

  FeatureData toDefoggerFeature(Data const& data, int res, int stride) const;

 protected:
  virtual void extractUnit(TensorDest acc, Unit* unit) override {
    if (unit->isMine) {
      acc[0] = mapType(unit->type->unit) + 118 * 0;
    } else if (unit->isEnemy) {
      acc[0] = mapType(unit->type->unit) + 118 * 1;
    } else {
      acc[0] = -1;
    }
  }

  std::array<int, 234>* typemap_;
  std::array<int, 234>* itypemap_;
};

/**
 * A variant of UnitTypeDefoggerFeaturizer that stores the target type of
 * morphing units.
 *
 * Morphing Zerglings will be featurized as two units.
 */
struct UnitTypeMDefoggerFeaturizer : UnitTypeDefoggerFeaturizer {
  static int constexpr kNumUnitTypes = 118 * 2;

  UnitTypeMDefoggerFeaturizer();

  /// Extract unit features for a given set of units
  virtual Data extract(
      State* state,
      UnitsInfo::Units const& units,
      Rect const& boundingBox = Rect());
};

/**
 * Sparse featurizer for unit flags.
 *
 * This will produce a feature with 52 channels, where each channel corresponds
 * to a flag of torchcraft::replayer::Unit. Each channel is binary, i.e. it's 1
 * if the flag is set and 0 otherwise.
 */
struct UnitFlagsFeaturizer : UnitAttributeFeaturizer {
  static int constexpr kNumUnitFlags = 52;

  UnitFlagsFeaturizer() {
    type = CustomFeatureType::UnitFlags;
    name = "UnitFlags";
    numChannels = kNumUnitFlags;
  }

 protected:
  virtual void extractUnit(TensorDest acc, Unit* unit) override {
    for (auto flag = 0; flag < kNumUnitFlags; flag++) {
      acc[flag] = (unit->unit.flags & (1 << flag)) ? 1 : 0;
    }
  }
};

struct UnitStatFeaturizer : UnitAttributeFeaturizer {
  static constexpr int kNumChannels =
      13 + UnitFlagsFeaturizer::kNumUnitFlags + 11;

  UnitStatFeaturizer() {
    type = CustomFeatureType::UnitStat;
    name = "UnitStat";
    numChannels = kNumChannels;
  }

 protected:
  virtual void extractUnit(TensorDest, cherrypi::Unit*) override;
};

/**
 * Sparse featurizer for unit ATTR.
 *
 * This will produce a single-channel feature that contains the ATTR for
 * each unit.
 */
#define GEN_SPARSE_UNIT_ATTRIBUTE_FEATURIZER(NAME, ATTR)            \
  struct Unit##NAME##Featurizer : UnitAttributeFeaturizer {         \
    Unit##NAME##Featurizer() {                                      \
      type = CustomFeatureType::Unit##NAME;                         \
      name = "Unit" #NAME;                                          \
      numChannels = 1;                                              \
    }                                                               \
                                                                    \
   protected:                                                       \
    virtual void extractUnit(TensorDest acc, Unit* unit) override { \
      acc[0] = unit->unit.ATTR;                                     \
    }                                                               \
  };

GEN_SPARSE_UNIT_ATTRIBUTE_FEATURIZER(HP, health);
GEN_SPARSE_UNIT_ATTRIBUTE_FEATURIZER(Shield, shield);
GEN_SPARSE_UNIT_ATTRIBUTE_FEATURIZER(GroundCD, groundCD);
GEN_SPARSE_UNIT_ATTRIBUTE_FEATURIZER(AirCD, airCD);

#undef GEN_SPARSE_UNIT_ATTRIBUTE_FEATURIZER
} // namespace cherrypi
