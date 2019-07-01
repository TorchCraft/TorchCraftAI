/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 */
#include "features/features.h"
#include "gasmodelcnn.h"
#include "common/assert.h"
#include "common/autograd.h"
#include "utils/debugging.h"
#include "utils/upcs.h"

#include "modelpf.h"
#include <c10/cuda/CUDAFunctions.h>
#include <cuda_runtime.h>
#include <fmt/ostream.h>
#include <prettyprint/prettyprint.hpp>
#include "gasmodelpf.h"

#define LOGSHAPE(t)       \
  VLOG(3) << fmt::format( \
      "{}:{}\t{}: {} [{}]\n", __FILE__, __LINE__, #t, t.sizes(), t.dtype())
namespace microbattles {

using common::MLP;

DEFINE_bool(multi_headed_q, false, "Have a different state value for each lod. Q value depends only on state value and eval at specified lod");
DEFINE_bool(embedding_per_group, false, "Allocate each group a chunk of the embedding");

static const int kDownsample = 4;
static constexpr BoundingBox<21, kDownsample> bounds{};

static const int kCmdOptions = 8;

void GasCNNModel::reset() {
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
  evalNetworks_ = std::vector<ag::Container>();                
  for (int i = 0; i <= FLAGS_max_lod; i++) {
    evalNetworks_.push_back(
        add(MLP()
                .nIn(hidSz_/(FLAGS_embedding_per_group ? std::pow(2, i) : 1))
                .nHid(kUnitEncSize)
                .nOut(1 + 2 * kCmdOptions)
                .nLayers(2)
                .make(),
            "eval_lod_" + std::to_string(i)));
  }
  int stateOut = FLAGS_multi_headed_q ? FLAGS_max_lod + 1 : 1;
  if (FLAGS_state_value) {
    PARAM(
        stateValueHead_,
        MLP().nIn(hidSz_).nHid(kUnitEncSize).nOut(stateOut).nLayers(2).make());
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

ag::Variant GasCNNModel::forward(ag::Variant input) {
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

  auto H = mapsz;
  auto W = mapsz;
  auto ourUsz = ourFeats.size(1);
  auto nmyUsz = nmyFeats.size(1);
  auto hidSz = 64;
  VLOG(0) << fmt::format( \
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
  VLOG(0) << "ourGrps " << ourGrps;
  
  LOGSHAPE(ourFeats);
  LOGSHAPE(nmyFeats);
  ourFeats = ourUnitBaseEncoder_->forward(ourFeats)[0];
  VLOG(0) << fmt::format( \
    "{}:{}\t{}: Expected {} {} {}\n", __FILE__, __LINE__, "ourFeats", bsz, ourUsz, hidSz);
  LOGSHAPE(ourFeats);
  nmyFeats = nmyUnitBaseEncoder_->forward(nmyFeats)[0];
  VLOG(0) << fmt::format( \
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
  VLOG(0) << fmt::format( \
        "{}:{}\t{}: Expected {} {} {} {}\n", __FILE__, __LINE__, "convInput", bsz, numUnitFeatures_ * 2 + numMapFeatures_, H, W);
  LOGSHAPE(convInput);
  convInput = convInput.reshape({bsz, -1, H, W}); //probs unecessary
  LOGSHAPE(convInput);

  // B x G x H x W
  auto ourGrpMasks =
      common::scatterSum2d(ourLocs, ourGrpsScattered, {mapsz, mapsz}).gt(0);
  LOGSHAPE(ourGrpMasks);
  VLOG(0) << "groups sum " << ourGrpMasks.sum(-1).sum(-1);

  for (size_t i = 0; i < convLayers_.size(); i++) {
    VLOG(0) << "input to layer " << std::to_string(i) << ": " << common::tensorStats(convInput);
    VLOG(-1) << "norm of the input to layer " << std::to_string(i) << ": " << convInput.norm().item<float>();
    if(i ==0) {
      VLOG(0) << fmt::format( \
            "{}:{}\t{}: Expected {} {} {} {}\n", __FILE__, __LINE__, "convInput", bsz, numUnitFeatures_ * 2 + numMapFeatures_, H, W);
      for (int j =0; j < numUnitFeatures_ * 2 + numMapFeatures_; j++) {
        heatmaps["input_to_layer_" + std::to_string(i)+"_feat_" + std::to_string(j)] = convInput.select(1, j);
      }
    }
    else{
      VLOG(0) << fmt::format( \
            "{}:{}\t{}: Expected {} {} {} {}\n", __FILE__, __LINE__, "convInput", bsz, hidSz_, H, W);
      for (int j =0; j < hidSz_; j++) {
        heatmaps["input_to_layer_" + std::to_string(i)+"_feat_" + std::to_string(j)] = convInput.select(1, j);
      }
    }
    LOGSHAPE(convInput);
    heatmaps["input_to_layer_" + std::to_string(i)] = convInput.sum(1);
    convInput = convLayers_[i]->forward(convInput)[0];
  }
  
  VLOG(-1) << "norm of the output of CNN:"  << convInput.norm().item<float>();  
  for (int j =0; j < hidSz_; j++) {
    heatmaps["output_of_resnet_feat_" + std::to_string(j)] = convInput.select(1, j);
  }
  heatmaps["output_of_resnet"] = convInput.sum(1);
  
  // B x S_k x H x W
  VLOG(0) << fmt::format( \
        "{}:{}\t{}: Expected {} {} {} {}\n", __FILE__, __LINE__, "convInput", bsz, hidSz_, H, W);
  LOGSHAPE(convInput);
  VLOG(0) << "emb " << common::tensorStats(convInput);

  std::vector<torch::Tensor> allQs;
  torch::Tensor totalQ, actQ, fullMapPooled;

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
  for (int lod = 0; lod <= FLAGS_max_lod; lod++) {
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
    torch::Tensor expandEmbeddings;
    if (FLAGS_embedding_per_group) {
      auto groupEmbeddings = torch::chunk(convInput.clone(), lodGrps, 1);
      expandEmbeddings = torch::stack(groupEmbeddings, 1);
    }
    else {
      expandEmbeddings = convInput.unsqueeze(1).expand({-1, lodGrps, -1, -1, -1});  
    }
    LOGSHAPE(expandEmbeddings);
    auto maskedEmbeddings = expandEmbeddings * lodGrpMask;
    LOGSHAPE(maskedEmbeddings);
    torch::Tensor pooledEmbeddings;
    if (FLAGS_max_pool) {
      pooledEmbeddings = torch::relu(maskedEmbeddings.max_values(4).max_values(3));
    }
    else {
      pooledEmbeddings = torch::relu(maskedEmbeddings.mean(4).mean(3));
    }
    // auto pooledEmbeddings = torch::tanh(maskedEmbeddings.sum(4).sum(3));
    LOGSHAPE(pooledEmbeddings);
    auto lodEval = evalNetworks_[lod]->forward({pooledEmbeddings})[0];
    LOGSHAPE(lodEval);

    if (FLAGS_a2c) {
      allQs.push_back(lodEval.clone());
      // this is (should be) an unused dummy;
      actQ = lodEval.clone();
    } else {
      auto repeatEval = lodEval.unsqueeze(2)
                            .repeat({1, 1, numGrps / lodGrps, 1})
                            .reshape({bsz, numGrps, -1});
      LOGSHAPE(repeatEval);
      if (FLAGS_multi_headed_q) {
        if (FLAGS_state_value) {
          totalQ = repeatEval + stateValue.select(1, lod).unsqueeze(1).unsqueeze(2).expand_as(repeatEval);
        }
        else {
          totalQ = repeatEval;
        }
        if (lod == 0) {
          actQ = torch::zeros_like(totalQ);
        }
      } else if (lod == 0) {
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
  }

  if (!FLAGS_debug_update) {
    heatmaps.clear();
  }
  if (!FLAGS_state_value) {
    ag::VariantDict res{{kAllQKey, allQs},
                          {kQKey, actQ},
                          {"heatmaps", heatmaps}};
    return res;
  }
  ag::VariantDict res{{kAllQKey, allQs},
                      {kQKey, actQ},
                      {kVKey, stateValue},
                      {"heatmaps", heatmaps}};

  VLOG(0) << "done forward";
  return res;
}
std::shared_ptr<MicroFeaturizer> GasCNNModel::getFeaturizer() {
  return std::make_shared<GasFeaturizer>();
}

std::vector<PFMicroActionModel::PFMicroAction> GasCNNModel::decodeOutput(
    cherrypi::State*,
    ag::Variant input,
    ag::Variant output) {
  throw std::runtime_error(
      "This GAS model should use decodeGasOutput, not decodeOutput");
}
std::pair<torch::Tensor, std::vector<PFMicroActionModel::PFMicroAction>>
GasCNNModel::decodeGasOutput(
    cherrypi::State* state,
    ag::Variant input,
    ag::Variant output,
    int lod,
    float epsilon) {
  return decodeCardinalGasOutput(state,input,output,lod,epsilon,rngEngine_);
}

} // namespace microbattles
