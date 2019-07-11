/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "features/defoggerfeatures.h"

namespace cherrypi {

tc::Frame DefoggerFeaturizer::combine(
    const std::deque<tc::Frame>& frames,
    int playerId) {
  tc::Frame combined;
  for (const auto& next_frame : frames) {
    // For units, accumulate presence and commands
    for (const auto& player : next_frame.units) {
      auto& player_id = player.first;
      auto& player_units = player.second;
      auto& combined_units = combined.units[player_id];

      // Build dictionary of uid -> position in current frame unit vector
      std::unordered_map<int32_t, int32_t> next_idx;
      for (size_t i = 0; i < player_units.size(); i++)
        next_idx[player_units[i].id] = i;

      for (auto unit = combined_units.begin(); unit != combined_units.end();) {
        // If unit isn't in next frame, it must have died, so we delete it.
        // This doesn't delete units that went into the FOW, although it will
        // delete garrisoned marines I think.
        if (next_idx.count(unit->id) == 0)
          unit = combined_units.erase(unit);
        else
          unit++;
      }

      std::unordered_map<int32_t, int32_t> combined_idx;
      for (size_t i = 0; i < combined_units.size(); i++)
        combined_idx[combined_units[i].id] = i;

      // Iterate over units in next frame
      for (const auto& unit : player_units) {
        auto visible = unit.visible & (1 << playerId);
        if (!visible)
          continue; // Don't featurize if we can't see unit

        if (combined_idx.count(unit.id) == 0) {
          // Unit wasn't in current frame, add it
          combined_units.push_back(unit);
        } else {
          int32_t i = combined_idx[unit.id];
          // Take unit state from next frame but accumulate orders
          // so as to have a vector of all the orders taken
          auto ords = std::move(combined_units[i].orders);
          ords.reserve(ords.size() + unit.orders.size());
          for (auto& ord : unit.orders) {
            if (ords.empty() || !(ord == ords.back())) {
              ords.push_back(ord);
            }
          }
          combined_units[i] = unit;
          combined_units[i].orders = std::move(ords);
        }
      }
      // For resources: keep the ones of the next frame
      if (next_frame.resources.find(player_id) != next_frame.resources.end()) {
        auto next_res = next_frame.resources.at(player_id);
        combined.resources[player_id].ore = next_res.ore;
        combined.resources[player_id].gas = next_res.gas;
        combined.resources[player_id].used_psi = next_res.used_psi;
        combined.resources[player_id].total_psi = next_res.total_psi;
      }
    }
    // For other stuff, simply keep that of next_frame
    combined.actions = next_frame.actions;
    combined.bullets = next_frame.bullets;
    combined.reward = next_frame.reward;
    combined.is_terminal = next_frame.is_terminal;
  }

  return combined;
}

torch::Tensor DefoggerFeaturizer::featurize(
    tc::Frame* frame,
    int mapX,
    int mapY,
    int playerId,
    at::Device device) {
  auto nBinX = (double)(mapX - resX) / strideX + 1;
  auto nBinY = (double)(mapY - resY) / strideY + 1;
  if (nBinX != (int)nBinX)
    std::cerr << "WARNING: X dimension of " << mapX
              << " is not evenly tiled by kW " << resX << " and stride "
              << strideX << " because you get " << nBinX << "bins\n";
  if (nBinY != (int)nBinY)
    std::cerr << "WARNING: Y dimension of " << mapY
              << " is not evenly tiled by kW " << resY << " and stride "
              << strideY << " because you get " << nBinY << "bins\n";

  auto feat = torch::zeros(
      {(int64_t)nBinY, (int64_t)nBinX, 2 * (int64_t)feature_size},
      torch::TensorOptions().device(device).dtype(torch::kF32));

  for (auto unit : frame->units[playerId])
    featurize_unit(feat, unit, 0, playerId);
  for (auto unit : frame->units[1 - playerId])
    featurize_unit(feat, unit, 1, playerId);

  return feat;
}

void DefoggerFeaturizer::featurize_unit(
    torch::Tensor& feats,
    tc::Unit& u,
    int perspective,
    int playerId) {
  auto offset = perspective == 0 ? 0 : feature_size;
  auto visible = u.visible & (1 << playerId);
  if (!fullVision && !visible)
    return; // Don't featurize if we can't see unit
  inc_feature(feats, offset + typemapper.at(u.type), u.x, u.y);
}

void DefoggerFeaturizer::inc_feature(
    torch::Tensor& feature,
    int32_t c,
    int32_t x,
    int32_t y) const {
  int32_t nBinY = feature.size(0);
  int32_t nBinX = feature.size(1);

  // Determine resulting bins for this position.
  // The last kernel applications that contains it will be placed at
  // (floor(x/strideX), floor(y/strideY). The number of kernels
  // applications containing it (e.g. on the X axis) is given by
  // ceil((resX - x % strideX) / strideX). Here, (resX - x % strideX) is the
  // offset of x within the first kernel application (which happens at a
  // multiple of strideX by definition).
  // Note that if stride > res, the position might not end up in any
  // application.
  int32_t maxbX = std::min(x / strideX, nBinX - 1) + 1;
  int32_t maxbY = std::min(y / strideY, nBinY - 1) + 1;
  int32_t minbX = std::max(
      0, maxbX - (int32_t(resX) - (x % strideX) + strideX - 1) / strideX);
  int32_t minbY = std::max(
      0, maxbY - (int32_t(resY) - (y % strideY) + strideY - 1) / strideY);

  for (int32_t by = minbY; by < maxbY; by++) {
    for (int32_t bx = minbX; bx < maxbX; bx++) {
      feature[by][bx][c] += 1.0;
    }
  }
}
} // namespace cherrypi
