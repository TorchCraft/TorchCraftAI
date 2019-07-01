/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 */

#include "gasmodel_globalcnn.h"
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
  VLOG_EVERY_N(0, 200) << fmt::format( \
      "{}:{}\t{}: {} [{}]\n", __FILE__, __LINE__, #t, t.sizes(), t.dtype())
namespace microbattles {

using common::MLP;

static const int kDownsample = 4;
static constexpr BoundingBox<21, kDownsample> bounds{};

std::shared_ptr<MicroFeaturizer> GasGlobalCNNModel::getFeaturizer() {
  return std::make_shared<GasFeaturizer>();
}

std::vector<PFMicroActionModel::PFMicroAction> GasGlobalCNNModel::decodeOutput(
    cherrypi::State*,
    ag::Variant input,
    ag::Variant output) {
  throw std::runtime_error(
      "This GAS model should use decodeGasOutput, not decodeOutput");
}
std::pair<torch::Tensor, std::vector<PFMicroActionModel::PFMicroAction>>
GasGlobalCNNModel::decodeGasOutput(
    cherrypi::State* state,
    ag::Variant input,
    ag::Variant output,
    int lod,
    float epsilon) {
  VLOG_EVERY_N(0, 100) << "decoding output";
  ag::Variant inputState = input.getDict()[kStateKey];
  auto& ourUnits = state->unitsInfo().myUnits();
  auto& nmyUnits = state->unitsInfo().enemyUnits();
  auto ourLocsCPU = inputState[kOurLocsKey].to(at::kCPU);
  auto ourLocs = ourLocsCPU.accessor<int, 2>();
  auto nmyLocsCPU = inputState[kNmyLocsKey].to(at::kCPU);
  auto nmyLocs = nmyLocsCPU.accessor<int, 2>();
  auto ourGrps = inputState[kGrpAssignments].to(at::kCPU);
  VLOG_EVERY_N(0, 100) << "our mean " << ourLocsCPU.to(at::kFloat).mean(0);
  VLOG_EVERY_N(0, 100) << "nmy mean " << nmyLocsCPU.to(at::kFloat).mean(0);

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
  VLOG_EVERY_N(0, 100) << cmdScores;
  auto greedyCommands = std::get<1>(cmdScores.max(1));
  if (FLAGS_override_action >= 0) {
    greedyCommands.fill_(FLAGS_override_action);
  }
  int maxGrps = std::pow(2, FLAGS_max_lod);
  auto grpCommands = greedyCommands.clone();
  int lodGrps = std::pow(2, lod);
  VLOG_EVERY_N(0, 100) << "grpCommands" << grpCommands;
  VLOG_EVERY_N(0, 100)<< "lodgrps" << lodGrps;
  auto exploreCommands =
      std::get<0>(greedyCommands.reshape({maxGrps / lodGrps, lodGrps}).max(0))
          .reshape({lodGrps});
  VLOG_EVERY_N(0, 100) << "explore cmds" << exploreCommands;
  for (int g = 0; g < lodGrps; g++) {
    float r = std::uniform_real_distribution<>(0.0, 1.0)(rngEngine_);
    if (r < epsilon) {
      exploreCommands[g] = (int)(common::Rand::rand() % numActions_);
    }
  }
  grpCommands = exploreCommands.unsqueeze(1)
                    .repeat({1, maxGrps / lodGrps})
                    .reshape({maxGrps});
  VLOG_EVERY_N(0, 100) << "grpCommands after explore" << grpCommands;
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
  VLOG_EVERY_N(0, 100) << "done decoding";
  return std::make_pair(grpCommands, actions);
}

void GasGlobalCNNModel::reset() {
    int constexpr kUnitEncSize = 128;
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

      convLayers_ = std::vector<ag::Container>();
      convLayers_.push_back(
          add(ag::Sequential()
                  .append(ag::Conv2d(kUnitEncSize * 2 + numMapFeatures_, hidSz_, 7).padding(3).make())
                  .append(ag::Functional(torch::relu).make())
                  .make(),
              "conv1"));
      convLayers_.push_back(
          add(ag::Sequential()
                  .append(ResidualBlock()
                              .in_channels(hidSz_)
                              .out_channels(hidSz_)
                              .kernel_size(3)
                              .padding(1)
                              .batchnorm(true)
                              .make())
                  .append(ResidualBlock()
                              .in_channels(hidSz_)
                              .out_channels(hidSz_)
                              .kernel_size(3)
                              .padding(1)
                              .batchnorm(true)
                              .make())
                  .append(ResidualBlock()
                              .in_channels(hidSz_)
                              .out_channels(hidSz_)
                              .kernel_size(3)
                              .padding(1)
                              .batchnorm(true)
                              .make())
                  .append(ResidualBlock()
                              .in_channels(hidSz_)
                              .out_channels(hidSz_)
                              .kernel_size(3)
                              .padding(1)
                              .batchnorm(true)
                              .make())
                  .make(),
              "residualBlock1"));
      convLayers_.push_back(
          add(ag::Sequential()
                  .append(ag::Conv2d(hidSz_, hidSz_, 3).padding(1).make())
                  .append(ag::Functional(torch::relu).make())
                  .make(),
              "conv2"));
                    
    for (int i = 0; i <= FLAGS_max_lod; i++) {
      evalNetworks_.push_back(
          add(ag::Conv2d(hidSz_, 2, 1).make(),
              "eval_lod_" + std::to_string(i)));
    }
    if (FLAGS_state_value) {
      PARAM(
          stateValueHead_,
          MLP().nIn(hidSz_).nHid(kUnitEncSize).nOut(1).nLayers(2).make());
    }
    
    for (auto& parameter : parameters()) {
      // Please see https://pytorch.org/docs/stable/_modules/torch/nn/init.html
      // for the gain
      if (parameter.dim() == 1) {
        parameter.detach().zero_();
      } else {
        kaiming_normal_(parameter, std::sqrt(2.0));
      }
    }
    // for (auto& parameter : convLayers_.back()->parameters()) {
    //   parameter.detach().zero_();
    // }
}

ag::Variant GasGlobalCNNModel::forward(ag::Variant input) {
  auto heatmapCropCenter = [&](torch::Tensor t) {
    auto y = t.size(0);
    auto x = t.size(1);
    auto finalX = 64;
    auto finalY = 64;
    auto offsetX = (x - finalX) / 2;
    auto offsetY = (y - finalY) / 2;
    return t.slice(0, offsetY, y - offsetY).slice(1, offsetX, x - offsetX);
  };

  ag::VariantDict heatmaps;
  VLOG_EVERY_N(0, 200) << "gas model cnn global forward";
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
  VLOG_EVERY_N(0, 200) << "mapsz " << mapsz;
  ourLocs = ourLocs / kDownsample;
  nmyLocs = nmyLocs / kDownsample;
  mapFeats = torch::adaptive_avg_pool2d(mapFeats, {mapsz, mapsz});
  LOGSHAPE(mapFeats);
  LOGSHAPE(ourLocs);

  // Do offset for OOB
  ourLocs = ourLocs + bounds.kOffset;
  nmyLocs = nmyLocs + bounds.kOffset;

  auto H = mapsz;
  auto W = mapsz;
  auto ourUsz = ourFeats.size(1);
  auto nmyUsz = nmyFeats.size(1);
  auto hidSz = 64;
  VLOG_EVERY_N(0, 200) << fmt::format( \
        "{}:{}\t{}: Expected {} {} {} {}\n", __FILE__, __LINE__, "mapFeats", bsz, numMapFeatures_, H, W);
  LOGSHAPE(mapFeats);


  auto ourUnitsMask =
      ourLocs.select(2, 0).ge(0).unsqueeze(2).to(torch::kFloat); // B x U x 1
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
  VLOG_EVERY_N(0, 200) << "ourGrps " << ourGrps;
  
  LOGSHAPE(ourFeats);
  LOGSHAPE(nmyFeats);
  ourFeats = ourUnitBaseEncoder_->forward(ourFeats)[0];
  VLOG_EVERY_N(0, 200) << fmt::format( \
    "{}:{}\t{}: Expected {} {} {}\n", __FILE__, __LINE__, "ourFeats", bsz, ourUsz, hidSz);
  LOGSHAPE(ourFeats);
  nmyFeats = nmyUnitBaseEncoder_->forward(nmyFeats)[0];
  VLOG_EVERY_N(0, 200) << fmt::format( \
        "{}:{}\t{}: Expected {} {} {}\n", __FILE__, __LINE__, "nmyFeats", bsz, nmyUsz, hidSz);
  LOGSHAPE(nmyFeats);
  
  
  auto ourLocs1 = ourLocs.reshape({bsz, ourUsz, 2});
  LOGSHAPE(ourLocs1);
  ourFeats = ourFeats.reshape({bsz, ourUsz, numUnitEmbSize_});
  LOGSHAPE(ourFeats);
  auto ourScattered = common::scatterSum2d(
      ourLocs1,
      ourFeats,
      {H, W});
  LOGSHAPE(ourScattered);    
  nmyLocs = nmyLocs.reshape({bsz, nmyUsz, 2});
  nmyFeats = nmyFeats.reshape({bsz, nmyUsz, numUnitEmbSize_});
  LOGSHAPE(nmyFeats);
  auto nmyScattered = common::scatterSum2d(
      nmyLocs, 
      nmyFeats,
      {H, W});
  LOGSHAPE(nmyScattered);
  ourScattered = ourScattered.reshape({bsz, -1, H, W}); //probs unecessary
  LOGSHAPE(ourScattered);
  nmyScattered = nmyScattered.reshape({bsz, -1, H, W});; //probs unecessary
  LOGSHAPE(nmyScattered);

  auto convInput = torch::cat({ourScattered, nmyScattered, mapFeats}, 1);
  VLOG_EVERY_N(0, 200) << fmt::format( \
        "{}:{}\t{}: Expected {} {} {} {}\n", __FILE__, __LINE__, "convInput", bsz, numUnitFeatures_ * 2 + numMapFeatures_, H, W);
  LOGSHAPE(convInput);
  convInput = convInput.reshape({bsz, -1, H, W}); //probs unecessary
  LOGSHAPE(convInput);

  // B x G x H x W
  auto ourGrpMasks =
      common::scatterSum2d(ourLocs, ourGrpsScattered, {mapsz, mapsz}).gt(0);
  LOGSHAPE(ourGrpMasks);
  VLOG_EVERY_N(0, 200) << "groups sum " << ourGrpMasks.sum(-1).sum(-1);

  for (size_t i = 0; i < convLayers_.size(); i++) {
    VLOG_EVERY_N(0, 200) << "input to layer " << std::to_string(i) << ": " << common::tensorStats(convInput);
    VLOG_EVERY_N(0, 200) << "norm of the input to layer " << std::to_string(i) << ": " << convInput.norm().item<float>();
    if(i ==0) {
      VLOG_EVERY_N(0, 200) << fmt::format( \
            "{}:{}\t{}: Expected {} {} {} {}\n", __FILE__, __LINE__, "convInput", bsz, numUnitFeatures_ * 2 + numMapFeatures_, H, W);
      for (int j =0; j < numUnitFeatures_ * 2 + numMapFeatures_; j++) {
        heatmaps["input_to_layer_" + std::to_string(i)+"_feat_" + std::to_string(j)] = convInput.select(1, j);
      }
    }
    else{
      VLOG_EVERY_N(0, 200) << fmt::format( \
            "{}:{}\t{}: Expected {} {} {} {}\n", __FILE__, __LINE__, "convInput", bsz, hidSz_, H, W);
      for (int j =0; j < hidSz_; j++) {
        heatmaps["input_to_layer_" + std::to_string(i)+"_feat_" + std::to_string(j)] = convInput.select(1, j);
      }
    }
    LOGSHAPE(convInput);
    heatmaps["input_to_layer_" + std::to_string(i)] = convInput.sum(1);
    convInput = convLayers_[i]->forward(convInput)[0];
  }
  
  VLOG_EVERY_N(0, 200) << "norm of the output of CNN:"  << convInput.norm().item<float>();  
  for (int j =0; j < hidSz_; j++) {
    heatmaps["output_of_resnet_feat_" + std::to_string(j)] = convInput.select(1, j);
  }
  heatmaps["output_of_resnet"] = convInput.sum(1);
  
  // B x S_k x H x W
  VLOG_EVERY_N(0, 200) << fmt::format( \
        "{}:{}\t{}: Expected {} {} {} {}\n", __FILE__, __LINE__, "convInput", bsz, hidSz_, H, W);
  LOGSHAPE(convInput);
  VLOG_EVERY_N(0, 200) << "emb " << common::tensorStats(convInput);

  std::vector<torch::Tensor> allQs;
  torch::Tensor totalQ, actQ;
  torch::Tensor fullMapPooled;
  if (FLAGS_max_pool) {
    fullMapPooled = torch::relu(convInput.max_values(3).max_values(2));
  }
  else {
    fullMapPooled = torch::relu(convInput.mean(3).mean(2));
  }
  torch::Tensor stateValue;
  if (FLAGS_state_value) {
    stateValue = stateValueHead_->forward({fullMapPooled})[0];
    LOGSHAPE(stateValue);
  }
  for (uint64_t lod = 0; lod <= FLAGS_max_lod; lod++) {
    VLOG_EVERY_N(0, 200) << "lod " << lod;
    int lodGrps = std::pow(2, lod);
    // B x LG x 1 x H x W
    auto lodGrpMask = ourGrpMasks.clone()
                          .reshape({bsz, -1, lodGrps, mapsz, mapsz})
                          .sum(1)
                          .gt(0)
                          .unsqueeze(2);
    LOGSHAPE(lodGrpMask);
    auto expandEmbeddings =
        convInput.unsqueeze(1).expand({-1, lodGrps, -1, -1, -1});
    LOGSHAPE(expandEmbeddings);
    auto groupEmbeddings = expandEmbeddings.slice(2, expandEmbeddings.size(2) / 2, expandEmbeddings.size(2));
    LOGSHAPE(groupEmbeddings);
    auto maskedEmbeddings = groupEmbeddings;
    maskedEmbeddings.masked_fill_(lodGrpMask.expand_as(maskedEmbeddings), -999999999);
    // auto maskedEmbeddings = groupEmbeddings * lodGrpMask.to(torch::kFloat);
    LOGSHAPE(maskedEmbeddings);
    // B x LG x S_k
    torch::Tensor pooledEmbeddings;
    if (FLAGS_max_pool) {
      pooledEmbeddings = torch::relu(maskedEmbeddings.max_values(4).max_values(3));
    }
    else {
      pooledEmbeddings = torch::relu(maskedEmbeddings.mean(4).mean(3));
    }
    LOGSHAPE(pooledEmbeddings);
    // B x LG x K x 1 x 1
    auto broadcastEmbeddings = pooledEmbeddings.reshape({bsz * lodGrps, -1, 1, 1});
    broadcastEmbeddings = broadcastEmbeddings.repeat({1, 1, convInput.size(2), convInput.size(3)});
    LOGSHAPE(broadcastEmbeddings);
    auto allUnitsEmbeddings = expandEmbeddings.slice(2, 0, expandEmbeddings.size(2) /2);
    LOGSHAPE(allUnitsEmbeddings);
    auto fullEmbeddings = torch::cat({broadcastEmbeddings,
        allUnitsEmbeddings.reshape({bsz * lodGrps, -1, convInput.size(2), convInput.size(3)})},
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
      if (FLAGS_state_value) {
        totalQ = repeatEval + stateValue.unsqueeze(1).expand_as(repeatEval);
      }
      else {
        totalQ = repeatEval;
      }
      actQ = torch::zeros_like(totalQ);
    } else {
      totalQ = totalQ + repeatEval;
    }
    auto lodMask = actLod.eq((int)lod);
    LOGSHAPE(lodMask);
    actQ.masked_scatter_(lodMask.unsqueeze(2).expand_as(totalQ), totalQ);

    allQs.push_back(totalQ.clone());
  }
  if (!FLAGS_debug_update) {
    heatmaps.clear();
  }
  ag::VariantDict res{{kAllQKey, allQs}, {kQKey, actQ}, {"heatmaps", heatmaps}};

  VLOG_EVERY_N(0, 200) << "done forward";
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
