/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "cherrypi.h"
#include "features/features.h"
#include "features/unitsfeatures.h"

#include <autogradpp/autograd.h>

namespace cherrypi {

class State;
struct UPCTuple;

/**
 * Describes a sample that can be used to learn the BuildingPlacerModel.
 */
struct BuildingPlacerSample {
  using UnitType = int; // tc::BW::UnitType is a bit too much hassle
  static int constexpr kMapSize = 512; // walk tiles
  static int kNumMapChannels;

  /// Game-dependent input features
  struct StaticData {
    StaticData(State* state);
    FeatureData smap;
  };

  /// State-dependent input features
  struct {
    /// Various map features (plus UPC probabilities), build tile resolution
    FeatureData map;
    /// Unit type IDs that are present
    UnitTypeFeaturizer::Data units;
    /// Requested building type
    UnitType type = (+tc::BW::UnitType::MAX)._to_integral();
    /// Float tensor that contains all valid build locations wrt the input UPC
    /// (1 = valid, 0 = invalid). This is intended to be used as a mask for the
    /// model output.
    torch::Tensor validLocations;
  } features;

  UnitTypeFeaturizer unitFeaturizer;

  /// Frame number of this sample
  FrameNum frame;

  /// Map name (file name for replays); optional
  std::string mapName;
  /// Player name; optional
  std::string playerName;
  /// Area ID; optional, for easier baseline computations
  int areaId = -1;

  /// Model target output: a single position (in walk tiles)
  Position action;

  BuildingPlacerSample() = default;
  virtual ~BuildingPlacerSample() = default;

  /// Constructs a new sample with features extracted from the given state
  BuildingPlacerSample(
      State* state,
      std::shared_ptr<UPCTuple> upc,
      StaticData* staticData = nullptr);

  /// Constructs a new sample with features extracted from the given state,
  /// along with a target action.
  BuildingPlacerSample(
      State* state,
      Position action,
      std::shared_ptr<UPCTuple> upc);

  /// Assemble network input
  std::vector<torch::Tensor> networkInput() const;

  /// Maps an action (position) in walktiles to offset in flattened output or
  /// target tensor.
  /// "scale" will be accounted for in addtion to the scale of the extracted
  /// features.
  int64_t actionToOffset(Position pos, int scale = 1) const;

  /// Maps offset in flattened output or target tensor to a walktile position.
  /// "scale" will be accounted for in addtion to the scale of the extracted
  /// features.
  Position offsetToAction(int64_t offset, int scale = 1) const;

  template <class Archive>
  void serialize(Archive& ar, uint32_t const version) {
    ar(CEREAL_NVP(features.map),
       CEREAL_NVP(features.units),
       CEREAL_NVP(features.type),
       CEREAL_NVP(features.validLocations),
       CEREAL_NVP(frame),
       CEREAL_NVP(mapName),
       CEREAL_NVP(playerName),
       CEREAL_NVP(areaId),
       CEREAL_NVP(action));
  }
};

/**
 * A CNN model for determining building positions.
 *
 * This is a relatively simple feature pyramid model. The input is 128x128
 * (build tile resolution); after two conv layers + max pooling we are at 32x32.
 * Then, use a series of conv layers at that scale (4 by default).  Afterwards,
 * convolutions and upsampling to go back to 128x128. There are skip connections
 * from the first two conv layers to the deconv layers.
 *
 * The following properties will alter the model output:
 * - `masked`: Softmax masking to eliminiate zero-probability positions from
 *    input UPC. If this is set to false, forward() will return return an
 *    all-ones mask.
 * - `flatten`: Output flat tensors instead of 2-dimensional ones
 * - `logprobs`: Output log-probabilities instead of probabilities
 *
 * The following properties will alter the model structure:
 * - `num_top_channels`: The number of channels in the top-level
 *   (lowest-resolution) convolutions.
 * - `num_top_convs`: The number of convolutional layers at the top level.
 */
AUTOGRAD_CONTAINER_CLASS(BuildingPlacerModel) {
 public:
  TORCH_ARG(bool, masked) = false;
  TORCH_ARG(bool, flatten) = true;
  TORCH_ARG(bool, logprobs) = false;
  TORCH_ARG(int, num_top_channels) = 64;
  TORCH_ARG(int, num_top_convs) = 4;

  void reset() override;

  /// Build network input from a batch of samples.
  ag::Variant makeInputBatch(
      std::vector<BuildingPlacerSample> const& samples, torch::Device) const;
  ag::Variant makeInputBatch(std::vector<BuildingPlacerSample> const& samples)
      const;

  /**
   * Build network input and target from a batch of samples.
   *
   * The first element of the resulting pair is the network input, the second
   * element are the targets for this batch.
   */
  std::pair<ag::Variant, ag::Variant> makeBatch(
      std::vector<BuildingPlacerSample> const& samples, torch::Device) const;
  std::pair<ag::Variant, ag::Variant> makeBatch(
      std::vector<BuildingPlacerSample> const& samples) const;

  /**
   * Network forward.
   *
   * Expected input, with batch dimension as first dimension:
   * - `maps`: map features
   * - `units_pos`: 2D coordinates for entries in `units_data``:w
   * - `units_data`: unit type IDs
   * - `type`: requested building type
   * - `valid_mask`: mask set to 1 at valid build locations, 0 otherwise
   * Use makeBatch() to generate inputs from one or more samples.
   *
   * Output (batched):
   * - `output`: probability distribution over the whole map
   * - `mask`: effective mask that was applied to the output
   */
  ag::Variant forward(ag::Variant input) override;

 protected:
  ag::Container embedU;
  ag::Container embedT;
  ag::Container conv1;
  ag::Container conv2;
  ag::Container conv3;
  std::vector<ag::Container> convS;
  ag::Container dconv2;
  ag::Container skip2;
  ag::Container postskip2;
  ag::Container dconv1;
  ag::Container skip1;
  ag::Container postskip1;
  ag::Container out;
};

} // namespace cherrypi

CEREAL_CLASS_VERSION(cherrypi::BuildingPlacerSample, 1);
