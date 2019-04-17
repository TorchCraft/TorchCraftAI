/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <common/autograd.h>

#include <gflags/gflags.h>

DECLARE_string(bos_model_type);
DECLARE_bool(bos_bo_input);
DECLARE_bool(bos_mapid_input);
DECLARE_bool(bos_time_input);
DECLARE_bool(bos_res_input);
DECLARE_bool(bos_tech_input);
DECLARE_bool(bos_ptech_input);
DECLARE_bool(bos_units_input);
DECLARE_bool(bos_fabs_input);
DECLARE_int32(bos_hid_dim);
DECLARE_int32(bos_num_layers);
DECLARE_string(bos_targets);

namespace cherrypi {
namespace bos {

/// Construct a BOS module according to command-line flags
ag::Container modelMakeFromCli(double dropout = 0.0);
std::map<std::string, std::string> modelFlags();

AUTOGRAD_CONTAINER_CLASS(IdleModel) {
 public:
  void reset() override;
  ag::Variant forward(ag::Variant input) override;
};

AUTOGRAD_CONTAINER_CLASS(MapRaceEcoTimeFeaturize) {
 public:
  TORCH_ARG(int, bo_embsize) = 8;
  TORCH_ARG(int, mapid_embsize) = 8;
  TORCH_ARG(int, n_builds) = -1;
  TORCH_ARG(int, race_embsize) = 8;
  TORCH_ARG(int, resources_embsize) = 8;
  TORCH_ARG(int, tech_embsize) = 8;
  TORCH_ARG(int, ptech_embsize) = 8;
  TORCH_ARG(int, time_embsize) = 8;

  void reset() override;

  /** Expected input:
   * - a Bx1 tensor containing the map ID,
   * - a Bx2 tensor containing the races,
   * - a Bx4 tensor containing resource features.
   * - a Bx142 tensor containing upgrades and techonology features.
   * - a Bx142 tensor containing pending upgrades and techonology features.
   * - a Bx1 tensor containing the current time (in frames)
   * - a Bx1 tensor containing the active build
   *
   * B can also be TxB.
   *
   * Returns:
   * - a BxN tensor, with N being the sum of all embeddings
   */
  ag::Variant forward(ag::Variant input) override;

 protected:
  ag::Container embedM_; // map id
  ag::Container embedR_; // race
  ag::Container embedRS_; // resources
  ag::Container embedT_; // tech
  ag::Container embedPT_; // pending tech and upgrades
  ag::Container embedTM_; // time
  ag::Container embedBO_; // current build
};

AUTOGRAD_CONTAINER_CLASS(LinearModel) {
 public:
  TORCH_ARG(int, bo_embsize) = 8;
  TORCH_ARG(int, hid_dim) = 256;
  TORCH_ARG(int, mapid_embsize) = 8;
  TORCH_ARG(int, n_builds) = -1;
  TORCH_ARG(int, n_unit_types) = 118 * 2;
  TORCH_ARG(int, ptech_embsize) = 8;
  TORCH_ARG(int, race_embsize) = 8;
  TORCH_ARG(int, resources_embsize) = 8;
  TORCH_ARG(std::set<std::string>, target_builds) = {};
  TORCH_ARG(int, tech_embsize) = 8;
  TORCH_ARG(int, time_embsize) = 8;
  TORCH_ARG(bool, use_fabs) = false;
  TORCH_ARG(bool, zero_units) = false;

  void reset() override;

  /** Expected input: Dictionary with "features" as tensor vector:
   * - a BxF tensor containing the unit type counts (F = n_unit_types)
   * - a BxF tensor containing our future unit type counts (F = n_unit_types / 2
   * * 3)
   * - a Bx1 tensor containing the map ID.
   * - a Bx2 tensor containing the races,
   * - a Bx4 tensor containing resource features.
   * - a Bx142 tensor containing upgrades and techonology features.
   * - a Bx142 tensor containing pending upgrades and techonology features.
   * - a Bx1 tensor containing the current time (in frames)
   * - a Bx1 tensor containing the active build
   *
   * Returns an unordered_map:
   * - "vHeads": predicted values p(win) for all builds, masked wrt opponent
   *   race
   * - "Pi": softmax over p(win), masked wrt opponent race
   * - "V": overall value function -- currently just zeros
   */
  ag::Variant forward(ag::Variant input) override;

 protected:
  ag::Container trunk_; // Featurizer
  ag::Container linear_; // The linear model
  ag::Container vHeads_; // Predicts game outcome for each build
  torch::Tensor masks_; // Model output masks for all races
};

AUTOGRAD_CONTAINER_CLASS(MlpModel) {
 public:
  TORCH_ARG(int, bo_embsize) = 8;
  TORCH_ARG(int, hid_dim) = 256;
  TORCH_ARG(int, mapid_embsize) = 8;
  TORCH_ARG(int, n_builds) = -1;
  TORCH_ARG(int, n_layers) = 3;
  TORCH_ARG(int, n_unit_types) = 118 * 2;
  TORCH_ARG(int, race_embsize) = 8;
  TORCH_ARG(int, resources_embsize) = 8;
  TORCH_ARG(int, tech_embsize) = 8;
  TORCH_ARG(int, ptech_embsize) = 8;
  TORCH_ARG(int, time_embsize) = 8;
  TORCH_ARG(bool, use_fabs) = false;
  TORCH_ARG(bool, zero_units) = false;
  TORCH_ARG(std::set<std::string>, target_builds) = {};

  void reset() override;

  /** Expected input: Dictionary with "features" as tensor vector:
   * - a BxF tensor containing the unit type counts (F = n_unit_types)
   * - a BxF tensor containing our future unit type counts (F = n_unit_types / 2
   * * 3)
   * - a Bx1 tensor containing the map ID.
   * - a Bx2 tensor containing the races,
   * - a Bx4 tensor containing resource features.
   * - a Bx142 tensor containing upgrades and techonology features.
   * - a Bx142 tensor containing pending upgrades and techonology features.
   * - a Bx1 tensor containing the current time (in frames)
   * - a Bx1 tensor containing the active build
   *
   * Returns an unordered_map:
   * - "vHeads": predicted values p(win) for all builds, masked wrt opponent
   *   race
   * - "Pi": softmax over p(win), masked wrt opponent race
   * - "V": overall value function -- currently just zeros
   */
  ag::Variant forward(ag::Variant input) override;

 protected:
  ag::Container trunk_; // Featurizer
  ag::Container mlp_; // The actual model
  ag::Container vHeads_; // Predicts game outcome for each build
  torch::Tensor masks_; // Model output masks for all races
};

AUTOGRAD_CONTAINER_CLASS(LstmModel) {
 public:
  TORCH_ARG(int, bo_embsize) = 8;
  TORCH_ARG(int, hid_dim) = 256;
  TORCH_ARG(int, mapid_embsize) = 8;
  TORCH_ARG(int, n_builds) = -1;
  TORCH_ARG(int, n_layers) = 1;
  TORCH_ARG(int, n_unit_types) = 118 * 2;
  TORCH_ARG(int, race_embsize) = 8;
  TORCH_ARG(int, resources_embsize) = 8;
  TORCH_ARG(int, tech_embsize) = 8;
  TORCH_ARG(int, ptech_embsize) = 8;
  TORCH_ARG(int, time_embsize) = 8;
  TORCH_ARG(bool, use_fabs) = false;
  TORCH_ARG(bool, zero_units) = false;
  TORCH_ARG(std::set<std::string>, target_builds) = {};

  void reset() override;

  /** Expected input: Dictionary with "features" as tensor vector:
   * - a TxBxF tensor containing the unit type counts (F = n_unit_types)
   * - a TxBxF tensor containing our future unit type counts (F = n_unit_types /
   * 2 * 3)
   * - a TxBx1 tensor containing the map ID.
   * - a 1xBx2 tensor containing the races,
   * - a TxBx4 tensor containing resource features.
   * - a TxBx142 tensor containing upgrades and techonology features.
   * - a TxBx142 tensor containing pending upgrades and techonology features.
   * - a TxBx1 tensor containing the current time (in frames)
   * - a TxBx1 tensor containing the active build And optionally
   * - "hidden": hidden activations for the LSTMs
   *
   * The 'T' dimension may be omitted.
   *
   * Returns an unordered_map:
   * - "vHeads": predicted values p(win) for all builds, masked wrt opponent
   *   race
   * - "Pi": softmax over p(win), masked wrt opponent race
   * - "V": overall value function -- currently just zeros
   * - "hidden": the hidden state
   */
  ag::Variant forward(ag::Variant input) override;

 protected:
  ag::Container trunk_; // Featurizer
  ag::Container lstm_; // The actual model
  ag::Container vHeads_; // Predicts game outcome for each build
  torch::Tensor masks_; // Model output masks for all races
};

AUTOGRAD_CONTAINER_CLASS(ConvEncLstmModel) {
 public:
  TORCH_ARG(int, bo_embsize) = 8;
  TORCH_ARG(std::function<decltype(torch::relu)>, cnn_nonlinearity) =
      torch::relu;
  TORCH_ARG(bool, deep_conv) = false;
  TORCH_ARG(int, hid_dim) = 256;
  TORCH_ARG(int, kernel_size) = 5;
  TORCH_ARG(bool, map_features) = false;
  TORCH_ARG(int, mapid_embsize) = 8;
  TORCH_ARG(int, n_builds) = -1;
  TORCH_ARG(int, n_layers) = 1;
  TORCH_ARG(int, n_unit_types) = 118 * 2;
  TORCH_ARG(int, ptech_embsize) = 8;
  TORCH_ARG(int, race_embsize) = 8;
  TORCH_ARG(int, resources_embsize) = 8;
  TORCH_ARG(int, spatial_embsize) = 128;
  TORCH_ARG(std::set<std::string>, target_builds) = {};
  TORCH_ARG(int, tech_embsize) = 8;
  TORCH_ARG(int, time_embsize) = 8;
  TORCH_ARG(bool, use_fabs) = false;

  void reset() override;

  /**
   * Expected input:
   * Dictionary with "features" as tensor vector:
   * - a 1xBxCxhxw tensor containing the map features,
   * - a TxBx1 tensor containing the map ID.
   * - a 1xBx2 tensor containing the races,
   * - a TxBxFxHxW tensor containing the units features,
   * - a TxBxF tensor containing the unit type counts (F = n_unit_types)
   * - a TxBxF tensor containing our future unit type counts (F = n_unit_types /
   * 2 * 3)
   * - a TxBx4 tensor containing resource features.
   * - a TxBx142 tensor containing upgrades and techonology features.
   * - a TxBx142 tensor containing pending upgrades and techonology features.
   * - a TxBx1 tensor containing the current frame number
   * - a TxBx1 tensor containing the active build
   * And optionally
   * - "hidden": hidden activations for the LSTMs
   *
   * The 'T' dimension may be omitted.
   *
   * Returns an unordered_map:
   * - "vHeads": predicted values p(win) for all builds, masked wrt opponent
   * race
   * - "Pi": softmax over p(win), masked wrt opponent race
   * - "V": overall value function -- currently just zeros
   * - "hidden": the hidden state
   */
  ag::Variant forward(ag::Variant input) override;

 protected:
  ag::Container trunk_; // Featurizer
  ag::Container mapConv_;
  ag::Container convnet_; // CNN on spatial unit features
  ag::Container cembed_; // CNN output -> embedding
  ag::Container lstm_; // The actual model
  ag::Container vHeads_; // Predicts game outcome for each build
  torch::Tensor masks_; // Model output masks for all races
};

} // namespace bos
} // namespace cherrypi
