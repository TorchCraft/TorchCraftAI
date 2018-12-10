/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 */

#include "modelpf.h"

#include "common/autograd.h"
#include "utils/debugging.h"
#include "utils/upcs.h"

#include <cuda_runtime.h>
#include <fmt/ostream.h>
#include <prettyprint/prettyprint.hpp>

namespace microbattles {

using common::MLP;

namespace {

static constexpr BoundingBox<21> bounds{};

std::vector<torch::Tensor> initializeMesh() {
  auto lst = std::vector<torch::Tensor>();
  for (auto i = 0U; i < torch::cuda::device_count(); i++) {
    cudaSetDevice(i);
    lst.push_back(
        at::stack(
            {torch::arange(0, bounds.kHeight, defaultDevice())
                 .repeat({bounds.kWidth, 1}),
             torch::arange(0, bounds.kWidth, defaultDevice())
                 .repeat({bounds.kHeight, 1})
                 .t()},
            2)
            .toType(at::kFloat));
  }
  cudaSetDevice(0);
  return lst;
}
}

std::vector<torch::Tensor> PotentialKernel::mesh_ = initializeMesh();

torch::Tensor PiecewiseLinearPotential::forward(
    torch::Tensor locs,
    torch::Tensor params) {
  // locs: U x (y, x); params: U x 2
  auto eMesh = mesh_[at::globalContext().current_device()].unsqueeze(2).expand(
      {-1, -1, locs.size(0), -1});
  auto pLocs = locs.toType(at::kFloat).unsqueeze_(0).unsqueeze_(0);
  // H x W x U
  auto distfield = (pLocs.expand_as(eMesh) - eMesh).pow_(2).sum(3).sqrt_();
  // Sane initializations...? to help learning?
  auto p0 = (torch::elu((params.t()[0] + 0.5) * 20) + 1)
                .unsqueeze(0)
                .unsqueeze(0)
                .expand_as(distfield);
  auto p1 = (torch::elu((params.t()[1] + 0.5) * 20) + minDropOff)
                .unsqueeze(0)
                .unsqueeze(0)
                .expand_as(distfield);
  auto field = at::clamp((p0 + p1 - distfield) / p1, 0, 1);
  return field;
}

struct PFFeaturizer : public MicroFeaturizer {
  virtual int mapPadding() override {
    return kMovementBoundingBox - 1;
  }
  virtual int mapOffset() override {
    return mapPadding() / 2;
  }

  static constexpr int kMovementBoundingBox = 21; // 21 x 21
  static_assert(kMovementBoundingBox % 2 == 1, "Movement box should be odd");
};

std::shared_ptr<MicroFeaturizer> PFModel::getFeaturizer() {
  return std::make_shared<PFFeaturizer>();
}

std::vector<MicroModel::MicroAction> PFModel::decodeOutput(
    cherrypi::State* state,
    ag::tensor_list input,
    ag::tensor_list output) {
  auto& ourUnits = state->unitsInfo().myUnits();
  auto& nmyUnits = state->unitsInfo().enemyUnits();
  auto ourLocsCPU = input[1].to(at::kCPU);
  auto ourLocs = ourLocsCPU.accessor<int, 2>();
  auto nmyLocsCPU = input[3].to(at::kCPU);
  auto nmyLocs = nmyLocsCPU.accessor<int, 2>();

  auto checkLocs = [&](auto units, auto locs) {
    for (auto i = 0U; i < units.size(); i++) {
      if (units[i]->y != locs[i][0] || units[i]->x != locs[i][1]) {
        throw std::runtime_error(
            fmt::format(
                "Units are ordered incorrectly?? ({}, {}) vs ({} {})",
                units[i]->x,
                units[i]->y,
                locs[i][1],
                locs[i][0]));
      }
    }
  };
  checkLocs(ourUnits, ourLocs);
  checkLocs(nmyUnits, nmyLocs);

  // auto cmdScores = output[0].to(at::kCPU);
  auto cmdScores = [&]() {
    if (FLAGS_sample_command) {
      return at::multinomial(at::softmax(output[0].to(at::kCPU), 1), 1);
    } else {
      return std::get<1>(output[0].to(at::kCPU).max(1, true));
    }
  }();
  auto cmdScoresA = cmdScores.accessor<int64_t, 2>();
  auto atkScores = output[2].to(at::kCPU);
  auto atkScoresA = atkScores.accessor<float, 2>();
  auto moveScores = output[1].squeeze(1).to(at::kCPU);
  moveScores += torch::randn(moveScores.sizes(), moveScores.options()) * 1e-3;
  auto moveScoresA = moveScores.accessor<float, 3>();

  std::vector<MicroModel::MicroAction> actions;
  auto offset = moveScores.size(1) / 2;
  auto moveMax = moveScores.size(1);
  for (auto i = 0U; i < ourUnits.size(); i++) {
    if (cmdScoresA[i][0] == 0) {
      auto ux = ourUnits[i]->x;
      auto uy = ourUnits[i]->y;
      float bestMoveScore = std::numeric_limits<float>::lowest();
      int bestMoveY = -1, bestMoveX = -1;
      for (auto y = std::max(0L, offset - uy);
           y < std::min(moveMax, kMapHeight + offset - uy);
           y++) {
        for (auto x = std::max(0L, offset - ux);
             x < std::min(moveMax, kMapHeight + offset - ux);
             x++) {
          // Do we check for walkability here? Maybe it doesn't matter
          if (moveScoresA[i][y][x] > bestMoveScore) {
            bestMoveScore = moveScoresA[i][y][x];
            bestMoveY = y + uy - offset;
            bestMoveX = x + ux - offset;
          }
        }
      }
      actions.push_back(
          {MicroAction::Move, ourUnits[i], nullptr, {bestMoveX, bestMoveY}});
    } else if (cmdScoresA[i][0] == 1) {
      float bestAtkScore = std::numeric_limits<float>::lowest();
      int bestAtkInd = -1;
      for (auto j = 0; j < atkScoresA.size(1); j++) {
        if (atkScoresA[i][j] >
            bestAtkScore /* && nmyUnits[j]->inRangeOf(ourUnits[i], FLAGS_frameSkip) */) {
          bestAtkScore = atkScoresA[i][j];
          bestAtkInd = j;
        }
      }
      if (bestAtkInd < 0) {
        fmt::print("Why am I here: {} {}\n", nmyUnits.size(), bestAtkInd);
        actions.push_back(
            {MicroAction::None,
             ourUnits[i],
             nullptr,
             cherrypi::kInvalidPosition});
      } else {
        actions.push_back(
            {MicroAction::Attack,
             ourUnits[i],
             nmyUnits[bestAtkInd],
             cherrypi::kInvalidPosition});
      }
    } else {
      auto tgt = [&]() -> cherrypi::Unit* {
        for (auto& u : ourUnits[i]->enemyUnitsInSightRange) {
          if (ourUnits[i]->canAttack(u))
            return u;
        }
        return nullptr;
      }();
      if (tgt == nullptr) {
        actions.push_back(
            {MicroAction::None,
             ourUnits[i],
             nullptr,
             cherrypi::kInvalidPosition});
      } else {
        actions.push_back(
            {MicroAction::Attack,
             ourUnits[i],
             tgt,
             cherrypi::kInvalidPosition});
      }
    }
  }
  return actions;
}

void PFModel::reset() {
  int constexpr kUnitEncSize = 128;
  auto npot = numPotentials_;
  PARAM(
      unitBaseEncoder_,
      MLP()
          .nIn(numUnitFeatures_)
          .nHid(kUnitEncSize)
          .nOut(kUnitEncSize)
          .nLayers(3)
          .make());
  PARAM(
      ourEmbHead_,
      MLP().nIn(kUnitEncSize).nHid(kUnitEncSize).nOut(npot).nLayers(3).make());
  PARAM(
      nmyEmbHead_,
      MLP().nIn(kUnitEncSize).nHid(kUnitEncSize).nOut(npot).nLayers(3).make());
  PARAM(
      ourPotHead_,
      MLP()
          .nIn(kUnitEncSize)
          .nHid(kUnitEncSize)
          .nOut(kernel_->numParams())
          .nLayers(3)
          .make());
  PARAM(
      nmyPotHead_,
      MLP()
          .nIn(kUnitEncSize)
          .nHid(kUnitEncSize)
          .nOut(kernel_->numParams())
          .nLayers(3)
          .make());
  // Let this just be a linear for now.
  auto moveEmbSz = 3 * npot + numMapEmbSize_;
  PARAM(
      movementNetwork_,
      ag::Sequential().append(ag::Conv2d(moveEmbSz, 1, 1).make()).make());
  PARAM(
      attackNetwork_,
      MLP().nIn(6 * npot + 1).nHid(kUnitEncSize).nOut(1).nLayers(2).make());
  PARAM(
      commandNetwork_,
      MLP()
          .nIn(3 * npot)
          .nHid(kUnitEncSize)
          .nOut(numActions_)
          .nLayers(2)
          .make());
  PARAM(
      mapEncoder_,
      ag::Conv2d(MicroFeaturizer::kMapFeatures, numMapEmbSize_, 1)
          .padding(0)
          .make());
  for (auto& parameter : parameters()) {
    parameter.detach().zero_();
  }
}

ag::Variant PFModel::forward(ag::Variant input) {
  ag::tensor_list& inp = input.getTensorList();
  auto mapFeats = inp[0];
  auto ourLocs = inp[1];
  auto ourFeats = inp[2];
  auto nmyLocs = inp[3];
  auto nmyFeats = inp[4];
  auto ourNumUnits = ourLocs.size(0);
  auto nmyNumUnits = nmyLocs.size(0);

  // Do offset for OOB
  ourLocs = ourLocs + bounds.kOffset;
  nmyLocs = nmyLocs + bounds.kOffset;

  auto mapEmb = mapEncoder_->forward({mapFeats.unsqueeze(0)})[0].squeeze(0);

  // Create unit embeddings, should be U x K
  auto ourBase = at::relu(unitBaseEncoder_->forward({ourFeats})[0]);
  auto nmyBase = at::relu(unitBaseEncoder_->forward({nmyFeats})[0]);
  auto ourEmb = ourEmbHead_->forward({ourBase})[0];
  auto nmyEmb = nmyEmbHead_->forward({nmyBase})[0];

  // Let's compute some potentials!
  // Each unit has the same potential kernels, but possibly different spreads:
  // Should be U x P_p
  auto ourPotParams = ourPotHead_->forward({ourBase})[0];
  auto nmyPotParams = nmyPotHead_->forward({nmyBase})[0];

  // Now it's H x W x U
  auto ourPot = kernel_->forward(ourLocs, ourPotParams);
  auto nmyPot = kernel_->forward(nmyLocs, nmyPotParams);

  // This implicitly sums over the U dimension
  auto spatialPotFieldSum = ourPot.matmul(ourEmb) + nmyPot.matmul(nmyEmb);
  // And this is the max
  auto spatialPotFieldMax =
      at::cat({ourPot.unsqueeze(-1) * ourEmb, nmyPot.unsqueeze(-1) * nmyEmb}, 2)
          .max_values(2);
  // S_k = numPotentials_ * 2
  // Now it's H x W x S_k
  auto spatialPotField = at::cat({spatialPotFieldSum, spatialPotFieldMax}, 2);

  auto ourLocsCPU = ourLocs.to(at::kCPU);
  auto nmyLocsCPU = nmyLocs.to(at::kCPU);
  auto indexSpatialEmbeddings = [&](torch::Tensor locs) {
    auto acc = locs.accessor<int, 2>();
    std::vector<torch::Tensor> embs;
    for (auto i = 0; i < acc.size(0); i++) {
      auto y = acc[i][0];
      auto x = acc[i][1];
      embs.push_back(spatialPotField[y][x]);
    }
    return at::stack(embs, 0);
  };

  auto ourSpatialEmbs = indexSpatialEmbeddings(ourLocsCPU); // A x S_k
  auto nmySpatialEmbs = indexSpatialEmbeddings(nmyLocsCPU); // E x S_k

  // Get the movement planes, U x H x W x N_p
  auto ourMovementPlane = [&]() {
    auto acc = ourLocsCPU.accessor<int, 2>();
    std::vector<torch::Tensor> slices;
    std::vector<torch::Tensor> mapSlices;
    slices.reserve(ourLocsCPU.size(0));
    mapSlices.reserve(ourLocsCPU.size(0));
    for (auto i = 0; i < acc.size(0); i++) {
      auto y = acc[i][0];
      auto x = acc[i][1];
      slices.push_back(
          spatialPotField.slice(0, y - bounds.kOffset, y + bounds.kOffset + 1)
              .slice(1, x - bounds.kOffset, x + bounds.kOffset + 1));
      mapSlices.push_back(
          mapEmb.slice(1, y - bounds.kOffset, y + bounds.kOffset + 1)
              .slice(2, x - bounds.kOffset, x + bounds.kOffset + 1)
              .permute({1, 2, 0}));
    }
    return at::cat(
        {
            at::stack(slices, 0),
            at::stack(mapSlices, 0),
            // Maybe we should use different embeddings here
            ourEmb.unsqueeze(1).unsqueeze(1).expand(
                {-1, bounds.kSize, bounds.kSize, -1}),
        },
        3);
  }();
  ourMovementPlane = ourMovementPlane.permute({0, 3, 1, 2});
  auto ourMovementScores = movementNetwork_->forward({ourMovementPlane})[0];

  // These are U x 3 S_k
  auto ourFinalEmb = at::cat({ourEmb, ourSpatialEmbs}, 1);
  auto nmyFinalEmb = at::cat({nmyEmb, nmySpatialEmbs}, 1);
  auto relDist = at::cat(
                     {
                         ourLocs.unsqueeze(1).expand({-1, nmyNumUnits, -1}),
                         nmyLocs.unsqueeze(0).expand({ourNumUnits, -1, -1}),
                     },
                     2)
                     .pow_(2)
                     .sum(2, true)
                     .toType(at::kFloat)
                     .sqrt_()
                     .div_(20);
  auto ourActionEmbs = at::cat(
      {ourFinalEmb.unsqueeze(1).expand({-1, nmyNumUnits, -1}),
       nmyFinalEmb.unsqueeze(0).expand({ourNumUnits, -1, -1}),
       relDist},
      2);
  ourActionEmbs = ourActionEmbs.view({-1, ourActionEmbs.size(2)});
  auto ourAttackScores = attackNetwork_->forward({ourActionEmbs})[0].view(
      {ourNumUnits, nmyNumUnits});

  auto ourCommandScores = commandNetwork_->forward({ourFinalEmb})[0];

  return {ourCommandScores, ourMovementScores, ourAttackScores};
}
} // namespace microbattles
