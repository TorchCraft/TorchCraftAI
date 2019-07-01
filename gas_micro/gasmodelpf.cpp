/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 */

#include "gasmodelpf.h"

#include "common/autograd.h"
#include "utils/debugging.h"
#include "utils/upcs.h"

#include "modelpf.h"
#include <c10/cuda/CUDAFunctions.h>
#include <cuda_runtime.h>
#include <fmt/ostream.h>
#include <prettyprint/prettyprint.hpp>

#define LOGSHAPE(t)       \
  VLOG(0) << fmt::format( \
      "{}:{}\t{}: {} [{}]\n", __FILE__, __LINE__, #t, t.sizes(), t.dtype())
namespace microbattles {
  static const int numActions_ = 2 * 8 + 1;
  static const int kCmdOptions = 8;

  static const int cmdOffsets_[8][2] =
      {{-1, 1}, {0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}};

  std::pair<torch::Tensor, std::vector<PFMicroActionModel::PFMicroAction>> decodeCardinalGasOutput(
      cherrypi::State* state,
      ag::Variant input,
      ag::Variant output,
      int lod,
      float epsilon, 
      std::ranlux24 rngEngine) {
    VLOG(0) << "decoding output";
    ag::Variant inputState = input.getDict()[kStateKey];
    auto& ourUnits = state->unitsInfo().myUnits();
    auto& nmyUnits = state->unitsInfo().enemyUnits();
    auto ourLocsCPU = inputState[kOurLocsKey].to(at::kCPU);
    auto ourLocs = ourLocsCPU.accessor<int, 2>();
    auto nmyLocsCPU = inputState[kNmyLocsKey].to(at::kCPU);
    auto nmyLocs = nmyLocsCPU.accessor<int, 2>();
    auto ourGrps = inputState[kGrpAssignments].to(at::kCPU);
    VLOG(0) << "our mean " << ourLocsCPU.to(at::kFloat).mean(0);
    VLOG(0) << "nmy mean " << nmyLocsCPU.to(at::kFloat).mean(0);

    auto checkLocs = [&](auto units, auto locs) {
      for (auto i = 0U; i < units.size(); i++) {
        if (units[i]->y != locs[i][0] || units[i]->x != locs[i][1]) {
          throw std::runtime_error(fmt::format(
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

    // DO EPS GREEDY HERE?
    torch::Tensor cmdScores;
    if (FLAGS_max_lod == 0) {
      cmdScores = output[kAllQKey].to(at::kCPU);
    } else {
      cmdScores = output.getDict()[kAllQKey].getTensorList()[lod].to(at::kCPU);
    }
    VLOG(0) << cmdScores;

    torch::Tensor exploreCommands;
    int maxGrps = std::pow(2, FLAGS_max_lod);
    int lodGrps = std::pow(2, lod);
    if (FLAGS_a2c) {
      if (epsilon > 0.0) {
        exploreCommands = torch::softmax(cmdScores, 1).multinomial(1).squeeze(1);
      } else {
        exploreCommands = std::get<1>(cmdScores.max(1));
      }
    } else {
      auto greedyCommands = std::get<1>(cmdScores.max(1));
      if (FLAGS_override_action >= 0) {
        greedyCommands.fill_(FLAGS_override_action);
      }
      exploreCommands =
          std::get<0>(greedyCommands.reshape({maxGrps / lodGrps, lodGrps}).max(0))
              .reshape({lodGrps});
      for (int g = 0; g < lodGrps; g++) {
        float r = std::uniform_real_distribution<>(0.0, 1.0)(rngEngine);
        if (r < epsilon) {
          exploreCommands[g] = (int)(common::Rand::rand() % numActions_);
        }
      }
    }
    auto grpCommands = exploreCommands.unsqueeze(1)
                      .repeat({1, maxGrps / lodGrps})
                      .reshape({maxGrps});

    std::vector<PFMicroActionModel::PFMicroAction> actions;
    for (auto i = 0U; i < ourUnits.size(); i++) {
      auto unitGrp = ourGrps[i];
      auto unitCmd = grpCommands[unitGrp].item<int64_t>();
      int direction = (unitCmd - 1) % kCmdOptions;
      int offset = FLAGS_command_offset;
      int ux = ourUnits[i]->x;
      int uy = ourUnits[i]->y;

      int target_x = ux + cmdOffsets_[direction][0] * offset;
      int target_y = uy + cmdOffsets_[direction][1] * offset;
      std::vector<int> offsets{offset};
      if (target_x >= kMapHeight) {
        offsets.push_back(kMapHeight - ux - 1);
      } else if (target_x < 0) {
        offsets.push_back(ux);
      }
      if (target_y >= kMapHeight) {
        offsets.push_back(kMapHeight - uy - 1);
      } else if (target_y < 0) {
        offsets.push_back(uy);
      }
      int minOffset = *std::min_element(offsets.begin(), offsets.end());
      target_x = ux + cmdOffsets_[direction][0] * minOffset;
      target_y = uy + cmdOffsets_[direction][1] * minOffset;

      // XXX clip commands to map
      if (unitCmd == 0) {
        actions.push_back({PFMicroActionModel::PFMicroAction::None,
                           ourUnits[i],
                           nullptr,
                           cherrypi::kInvalidPosition});
      } else if (unitCmd < 1 + kCmdOptions) {
        // move
        actions.push_back(
            {PFMicroActionModel::PFMicroAction::Move, ourUnits[i], nullptr, {target_x, target_y}});
      } else {
        // attack
        actions.push_back({PFMicroActionModel::PFMicroAction::AttackMove,
                           ourUnits[i],
                           nullptr,
                           {target_x, target_y}});
      }
    }
    return std::make_pair(grpCommands, actions);
  }

using common::MLP;

namespace {
static const int kDownsample = 4;
static constexpr BoundingBox<21, kDownsample> bounds{};

std::vector<torch::Tensor> initializeMesh() {
  auto lst = std::vector<torch::Tensor>();
  for (auto i = 0U; i < torch::cuda::device_count(); i++) {
    cudaSetDevice(i);
    lst.push_back(at::stack(
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
} // namespace

std::vector<torch::Tensor> GasPotentialKernel::mesh_ = initializeMesh();

torch::Tensor GasPiecewiseLinearPotential::forward(
    torch::Tensor locs,
    torch::Tensor params) {
  auto bsz = locs.size(0);
  LOGSHAPE(locs);
  LOGSHAPE(params);
  LOGSHAPE(mesh_[c10::cuda::current_device()]);
  // locs: U x (y, x); params: U x 2
  auto eMesh =
      mesh_[c10::cuda::current_device()].unsqueeze(2).unsqueeze(0).expand(
          {bsz, -1, -1, locs.size(1), -1});
  LOGSHAPE(eMesh);
  auto pLocs = locs.toType(at::kFloat).unsqueeze_(1).unsqueeze_(1);
  LOGSHAPE(pLocs);
  // H x W x U
  auto distfield = (pLocs.expand_as(eMesh) - eMesh).pow_(2).sum(4).sqrt_();
  LOGSHAPE(distfield);
  // Sane initializations...? to help learning?
  auto p0 = (torch::elu((params.select(2, 0) + 0.5) * 20) + 1)
                .unsqueeze(1)
                .unsqueeze(1)
                .expand_as(distfield);
  auto p1 = (torch::elu((params.select(2, 1) + 0.5) * 20) + minDropOff)
                .unsqueeze(1)
                .unsqueeze(1)
                .expand_as(distfield);
  LOGSHAPE(p0);
  auto field = at::clamp((p0 + p1 - distfield) / p1, 0, 1);
  LOGSHAPE(field);
  return field;
}

std::shared_ptr<MicroFeaturizer> GasPFModel::getFeaturizer() {
  return std::make_shared<GasFeaturizer>();
}

std::vector<PFMicroActionModel::PFMicroAction> GasPFModel::decodeOutput(
    cherrypi::State*,
    ag::Variant input,
    ag::Variant output) {
  throw std::runtime_error(
      "This GAS model should use decodeGasOutput, not decodeOutput");
}
std::pair<torch::Tensor, std::vector<PFMicroActionModel::PFMicroAction>>
GasPFModel::decodeGasOutput(
    cherrypi::State* state,
    ag::Variant input,
    ag::Variant output,
    int lod,
    float epsilon) {
  return decodeCardinalGasOutput(state,input,output,lod,epsilon, rngEngine_);
}

void GasPFModel::reset() {
  int constexpr kUnitEncSize = 128;
  auto npot = numPotentials_;
  PARAM(
      nmyUnitBaseEncoder_,
      MLP()
          .nIn(numUnitFeatures_)
          .nHid(kUnitEncSize)
          .nOut(kUnitEncSize)
          .nLayers(3)
          .make());
  PARAM(
      ourUnitBaseEncoder_,
      MLP()
          .nIn(numUnitFeatures_ + std::pow(2, FLAGS_max_lod))
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
  for (int i = 0; i <= FLAGS_max_lod; i++) {
    evalNetworks_.push_back(
        add(MLP()
                .nIn(2 * npot)
                .nHid(kUnitEncSize)
                .nOut(1 + 2 * kCmdOptions)
                .nLayers(2)
                .make(),
            "eval_lod_" + std::to_string(i)));
  }
  PARAM(
      stateValueHead_,
      MLP().nIn(2 * npot).nHid(kUnitEncSize).nOut(1).nLayers(2).make());
}

ag::Variant GasPFModel::forward(ag::Variant input) {
  VLOG(0) << "modelpf forward";
  auto state = input.getDict()[kStateKey];
  auto mapFeats = state[kMapFeatsKey];
  auto ourLocs = state[kOurLocsKey];
  auto ourFeats = state[kOurFeatsKey];
  auto nmyLocs = state[kNmyLocsKey];
  auto nmyFeats = state[kNmyFeatsKey];
  auto ourGrps = state[kGrpAssignments];
  auto actLod = state[kLodKey];
  auto ourNumUnits = ourLocs.size(1);
  auto nmyNumUnits = nmyLocs.size(1);
  auto bsz = mapFeats.size(0);
  LOGSHAPE(mapFeats);
  auto mapsz = mapFeats.size(2) / kDownsample;
  VLOG(0) << "mapsz " << mapsz;
  ourLocs = ourLocs / kDownsample;
  nmyLocs = nmyLocs / kDownsample;
  mapFeats = torch::adaptive_avg_pool2d(mapFeats, {mapsz, mapsz});
  LOGSHAPE(mapFeats);
  LOGSHAPE(ourLocs);

  // Do offset for OOB
  ourLocs = ourLocs + bounds.kOffset;
  nmyLocs = nmyLocs + bounds.kOffset;

  auto ourUnitsMask =
      ourLocs.select(2, 0).ge(0).unsqueeze(2).to(torch::kFloat); // B x U x 1
  auto nmyUnitsMask = nmyLocs.select(2, 0).ge(0).unsqueeze(2).to(torch::kFloat);
  LOGSHAPE(ourUnitsMask);
  int numGrps = std::pow(2, FLAGS_max_lod);
  auto ourGrpsScattered =
      torch::zeros({bsz, ourNumUnits, numGrps}, options().dtype(torch::kInt));
  LOGSHAPE(ourGrps);
  // fill in zeros for empty things
  ourGrps.masked_scatter_(ourGrps.lt(0), torch::zeros_like(ourGrps));
  ourGrpsScattered.scatter_(2, ourGrps.to(torch::kLong).unsqueeze(2), 1);
  LOGSHAPE(ourGrpsScattered);
  LOGSHAPE(ourFeats);
  ourFeats = torch::cat({ourFeats, ourGrpsScattered.to(torch::kFloat)}, 2);
  LOGSHAPE(ourFeats);

  // B x G x H x W
  auto ourGrpMasks =
      common::scatterSum2d(ourLocs, ourGrpsScattered, {mapsz, mapsz}).gt(0);
  LOGSHAPE(ourGrpMasks);

  // Create unit embeddings, should be U x K
  auto ourBase = at::relu(ourUnitBaseEncoder_->forward({ourFeats})[0]);
  auto nmyBase = at::relu(nmyUnitBaseEncoder_->forward({nmyFeats})[0]);
  auto ourEmb = ourEmbHead_->forward({ourBase})[0];
  auto nmyEmb = nmyEmbHead_->forward({nmyBase})[0];
  LOGSHAPE(ourEmb);
  ourEmb = ourEmb * ourUnitsMask;
  nmyEmb = nmyEmb * nmyUnitsMask;
  LOGSHAPE(ourEmb);

  // Let's compute some potentials!
  // Each unit has the same potential kernels, but possibly different spreads:
  // Should be U x P_p
  auto ourPotParams = ourPotHead_->forward({ourBase})[0];
  auto nmyPotParams = nmyPotHead_->forward({nmyBase})[0];
  LOGSHAPE(ourPotParams);

  // Now it's H x W x U
  auto ourPot = kernel_->forward(ourLocs, ourPotParams);
  auto nmyPot = kernel_->forward(nmyLocs, nmyPotParams);

  // This implicitly sums over the U dimension
  auto spatialPotFieldSum = ourPot.view({bsz, -1, ourNumUnits}).bmm(ourEmb) +
      nmyPot.view({bsz, -1, nmyNumUnits}).bmm(nmyEmb);
  LOGSHAPE(spatialPotFieldSum);
  spatialPotFieldSum =
      spatialPotFieldSum.view({bsz, ourPot.size(1), ourPot.size(2), -1});
  LOGSHAPE(spatialPotFieldSum);
  // And this is the max
  auto spatialPotFieldMax =
      at::cat(
          {ourPot.unsqueeze(-1) * ourEmb.unsqueeze(1).unsqueeze(1),
           nmyPot.unsqueeze(-1) * nmyEmb.unsqueeze(1).unsqueeze(1)},
          3)
          .max_values(3);
  LOGSHAPE(spatialPotFieldMax);
  // Now it's B x H x W x S_k
  auto spatialPotField = at::cat({spatialPotFieldSum, spatialPotFieldMax}, 3);
  LOGSHAPE(spatialPotField);

  // B x S_k x H x W
  auto spatialEmbeddings = spatialPotField.transpose(1, 3);

  std::vector<torch::Tensor> allQs;
  torch::Tensor totalQ, actQ;

  auto fullMapPooled = torch::relu(spatialEmbeddings.mean(3).mean(2));
  auto stateValue = stateValueHead_->forward({fullMapPooled})[0];
  LOGSHAPE(stateValue);
  for (uint64_t lod = 0; lod <= FLAGS_max_lod; lod++) {
    int lodGrps = std::pow(2, lod);
    // B x LG x 1 x H x W
    auto lodGrpMask = ourGrpMasks.clone()
                          .reshape({bsz, -1, lodGrps, mapsz, mapsz})
                          .sum(1)
                          .gt(0)
                          .unsqueeze(2)
                          .to(torch::kFloat);
    LOGSHAPE(lodGrpMask);
    auto expandEmbeddings =
        spatialEmbeddings.unsqueeze(1).expand({-1, lodGrps, -1, -1, -1});
    LOGSHAPE(expandEmbeddings);
    auto maskedEmbeddings = expandEmbeddings * lodGrpMask;
    LOGSHAPE(maskedEmbeddings);
    // B x LG x S_k
    auto pooledEmbeddings = torch::relu(maskedEmbeddings.mean(4).mean(3));
    // auto pooledEmbeddings = torch::tanh(maskedEmbeddings.sum(4).sum(3));
    LOGSHAPE(pooledEmbeddings);
    auto lodEval = evalNetworks_[lod]->forward({pooledEmbeddings})[0];
    LOGSHAPE(lodEval);

    auto repeatEval = lodEval.unsqueeze(2)
                          .repeat({1, 1, numGrps / lodGrps, 1})
                          .reshape({bsz, numGrps, -1});
    LOGSHAPE(repeatEval);
    if (lod == 0) {
      totalQ = repeatEval + stateValue.unsqueeze(1).expand_as(repeatEval);
      actQ = torch::zeros_like(totalQ);
    } else {
      totalQ = totalQ + repeatEval;
    }
    auto lodMask = actLod.eq((int)lod);
    LOGSHAPE(lodMask);
    actQ.masked_scatter_(lodMask.unsqueeze(2).expand_as(totalQ), totalQ);

    allQs.push_back(totalQ.clone());
  }
  ag::VariantDict res{{kAllQKey, allQs}, {kQKey, actQ}};

  VLOG(0) << "done forward";
  return res;
}


ag::Variant GasFeaturizer::featurize(cherrypi::State* state) {
  auto unitFeatures = MicroFeaturizer::featurize(state);
  auto gasFeatures = gasUnitFeaturizer_.extract(state, state->unitsInfo().myUnits());
  auto onehot = gasFeatures.data * (FLAGS_unit_type_dist/2.0);
  auto postype = torch::cat({gasFeatures.positions, onehot}, 1);
  return {
    unitFeatures[0], 
    unitFeatures[1], 
    unitFeatures[2], 
    unitFeatures[3], 
    unitFeatures[4], 
    postype,
  };
};


} // namespace microbattles
