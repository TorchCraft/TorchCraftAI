/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 */

#include "gasmodel_global.h"
#include "gasmodelpf.h"

#include "common/autograd.h"
#include "utils/debugging.h"
#include "utils/upcs.h"
#include "utils.h"

#include "modelpf.h"
#include <c10/cuda/CUDAFunctions.h>
#include <cuda_runtime.h>
#include <fmt/ostream.h>
#include <prettyprint/prettyprint.hpp>

#define LOGSHAPE(t)       \
  VLOG(0) << fmt::format( \
      "{}:{}\t{}: {} [{}]\n", __FILE__, __LINE__, #t, t.sizes(), t.dtype())
namespace microbattles {

using common::MLP;

namespace {

static const int kDownsample = 4;
static constexpr BoundingBox<21, kDownsample> bounds{};

// XXX: What's going on with this? Where can I factor it out for all PF models?
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

std::shared_ptr<MicroFeaturizer> GasGlobalModel::getFeaturizer() {
  return std::make_shared<GasFeaturizer>();
}

std::vector<PFMicroActionModel::PFMicroAction> GasGlobalModel::decodeOutput(
    cherrypi::State*,
    ag::Variant input,
    ag::Variant output) {
  throw std::runtime_error(
      "This GAS model should use decodeGasOutput, not decodeOutput");
}
std::pair<torch::Tensor, std::vector<PFMicroActionModel::PFMicroAction>>
GasGlobalModel::decodeGasOutput(
    cherrypi::State* state,
    ag::Variant input,
    ag::Variant output,
    int lod,
    float epsilon) {
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
  LOGSHAPE(cmdScores);
  //index 0 gives group index 1 gives the command
  auto greedyCommands = std::get<1>(cmdScores.max(1));
  if (FLAGS_override_action >= 0) {
    greedyCommands.fill_(FLAGS_override_action);
  }
  int maxGrps = std::pow(2, FLAGS_max_lod);
  auto grpCommands = greedyCommands.clone();
  int lodGrps = std::pow(2, lod);
  VLOG(0) << "grpCommands" << grpCommands;
  VLOG(0) << "lodgrps" << lodGrps;
  auto exploreCommands =
      std::get<0>(greedyCommands.reshape({maxGrps / lodGrps, lodGrps}).max(0))
          .reshape({lodGrps});
  VLOG(0) << "explore cmds" << exploreCommands;
  for (int g = 0; g < lodGrps; g++) {
    float r = std::uniform_real_distribution<>(0.0, 1.0)(rngEngine_);
    if (r < epsilon) {
      exploreCommands[g] = (int)(common::Rand::rand() % numActions_);
    }
  }
  grpCommands = exploreCommands.unsqueeze(1)
                    .repeat({1, maxGrps / lodGrps})
                    .reshape({maxGrps});
  //LOGSHAPE(grpCommands);
  VLOG(0) << "grpCommands after explore" << grpCommands;
  // XXX adjust scores at all or e-greedy good enough?
  
  std::vector<bool> cmdTypes;
  std::vector<std::pair<int, int>> cmdTargets;
  for (auto i = 0U; i < grpCommands.size(0); i++) {
    auto cmd = grpCommands[i].item<int64_t>();
    bool move = cmd > kCmdOptions_;
    cmdTypes.push_back(move);
    int targetLocRaveled = cmd % kCmdOptions_;

    int xTargetUnscaled = targetLocRaveled % FLAGS_act_grid_sz;
    int yTargetUnscaled = targetLocRaveled / FLAGS_act_grid_sz;

    auto rescale = [&](int coord) {
      return ((coord + 0.5) * bounds.kWidth * kDownsample / FLAGS_act_grid_sz - bounds.kOffset * kDownsample);
    };
    int xTargetScaled = rescale(xTargetUnscaled);
    int yTargetScaled = rescale(yTargetUnscaled);
    int xTarget = cherrypi::utils::clamp(xTargetScaled, 1, kMapWidth - 1);
    int yTarget = cherrypi::utils::clamp(yTargetScaled, 1, kMapHeight - 1);
    cmdTargets.push_back(std::make_pair(xTarget, yTarget));
  }

  std::vector<PFMicroActionModel::PFMicroAction> actions;
  for (auto i = 0U; i < ourUnits.size(); i++) {
    auto unitGrp = ourGrps[i].item<int64_t>();
    bool move = cmdTypes[unitGrp];
    int xTarget, yTarget;
    std::tie(xTarget, yTarget) = cmdTargets[unitGrp];

    if (move) {
      // move
      actions.push_back(
          {PFMicroAction::Move, ourUnits[i], nullptr, {xTarget, yTarget}});
    } else {
      // attack
      actions.push_back({PFMicroAction::AttackMove,
                         ourUnits[i],
                         nullptr,
                         {xTarget, yTarget}});
    }
  }
  VLOG(0) << "done decoding";
  return std::make_pair(grpCommands, actions);
}

void GasGlobalModel::reset() {
  int constexpr kUnitEncSize = 128;
  auto npot = numPotentials_;
  std::random_device rd;
  rngEngine_.seed(rd());
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
        add(ag::Conv2d(2 * npot, 2, 1).make(),
            "eval_lod_" + std::to_string(i)));
  }
  PARAM(
      stateValueHead_,
      MLP().nIn(2 * npot).nHid(kUnitEncSize).nOut(1).nLayers(2).make());
}

ag::Variant GasGlobalModel::forward(ag::Variant input) {
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
  VLOG(0) << "ourGrps " << ourGrps;

  // B x G x H x W
  auto ourGrpMasks =
      common::scatterSum2d(ourLocs, ourGrpsScattered, {mapsz, mapsz}).gt(0);
  LOGSHAPE(ourGrpMasks);
  VLOG(0) << "groups sum " << ourGrpMasks.sum(-1).sum(-1);


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
  VLOG(0) << "ourpot " << common::tensorStats(ourPot);
  VLOG(0) << "ouremb " << common::tensorStats(ourEmb);

  LOGSHAPE(ourEmb);
  LOGSHAPE(ourPot);

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
  auto spatialPotField = at::cat({spatialPotFieldSum.unsqueeze(4),
                          spatialPotFieldMax.unsqueeze(4)}, 4);
  spatialPotField = spatialPotField.reshape({spatialPotField.size(0),
                                             spatialPotField.size(1),
                                             spatialPotField.size(2),
                                             -1});
  LOGSHAPE(spatialPotField);

  // B x S_k x H x W
  auto spatialEmbeddings = spatialPotField.transpose(1, 3);
  VLOG(0) << "emb " << common::tensorStats(spatialEmbeddings);

  std::vector<torch::Tensor> allQs;
  torch::Tensor totalQ, actQ;

  auto fullMapPooled = torch::relu(spatialEmbeddings.mean(3).mean(2));
  auto stateValue = stateValueHead_->forward({fullMapPooled})[0];
  LOGSHAPE(stateValue);
  for (uint64_t lod = 0; lod <= FLAGS_max_lod; lod++) {
    VLOG(0) << "lod " << lod;
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
    auto groupEmbeddings = expandEmbeddings.slice(2, expandEmbeddings.size(2) / 2, expandEmbeddings.size(2));
    LOGSHAPE(groupEmbeddings);
    auto maskedEmbeddings = groupEmbeddings * lodGrpMask;
    LOGSHAPE(maskedEmbeddings);
    // B x LG x S_k
    auto pooledEmbeddings = torch::relu(maskedEmbeddings.mean(4).mean(3));
    LOGSHAPE(pooledEmbeddings);
    // B x LG x K x 1 x 1
    auto broadcastEmbeddings = pooledEmbeddings.reshape({bsz * lodGrps, -1, 1, 1});
    broadcastEmbeddings = broadcastEmbeddings.repeat({1, 1, spatialEmbeddings.size(2), spatialEmbeddings.size(3)});
    LOGSHAPE(broadcastEmbeddings);
    auto allUnitsEmbeddings = expandEmbeddings.slice(2, 0, expandEmbeddings.size(2) /2);
    LOGSHAPE(allUnitsEmbeddings);
    auto fullEmbeddings = torch::cat({broadcastEmbeddings,
        allUnitsEmbeddings.reshape({bsz * lodGrps, -1, spatialEmbeddings.size(2), spatialEmbeddings.size(3)})},
        1);
    fullEmbeddings = torch::adaptive_avg_pool2d(fullEmbeddings, {FLAGS_act_grid_sz, FLAGS_act_grid_sz});
    fullEmbeddings = torch::relu(fullEmbeddings);
    LOGSHAPE(fullEmbeddings);
    auto lodEval = evalNetworks_[lod]->forward({fullEmbeddings})[0];
    LOGSHAPE(lodEval);
    lodEval = lodEval.reshape({bsz, lodGrps, -1});
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


// ag::Variant GasFeaturizer::featurize(cherrypi::State* state) {
//   auto unitFeatures = MicroFeaturizer::featurize(state);
//   auto gasFeatures = gasUnitFeaturizer_.extract(state, state->unitsInfo().myUnits());
//   auto onehot = gasFeatures.data * (FLAGS_unit_type_dist/2.0);
//   auto postype = torch::cat({gasFeatures.positions, onehot}, 1);
//   return {
//     unitFeatures[0], 
//     unitFeatures[1], 
//     unitFeatures[2], 
//     unitFeatures[3], 
//     unitFeatures[4], 
//     postype,
//   };
// };


} // namespace microbattles
