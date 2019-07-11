/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "modeldefiler.h"
#include "common/assert.h"
#include "modules/cherryvisdumper.h"

using namespace cherrypi;

namespace microbattles {

namespace {
constexpr int kMapDim = 128;
struct Fan {
  explicit Fan(torch::Tensor& tensor) {
    const auto dimensions = tensor.ndimension();
    AT_CHECK(
        dimensions >= 2,
        "Fan in and fan out can not be computed for tensor with fewer than 2 "
        "dimensions");

    if (dimensions == 2) {
      in = tensor.size(1);
      out = tensor.size(0);
    } else {
      in = tensor.size(1) * tensor[0][0].numel();
      out = tensor.size(0) * tensor[0][0].numel();
    }
  }

  int64_t in;
  int64_t out;
};
} // namespace

torch::Tensor kaiming_normal_(torch::Tensor tensor, double gain) {
  // Use Fan_in as default
  torch::NoGradGuard guard;
  Fan fan(tensor);
  const auto std = gain / std::sqrt(fan.in);
  return tensor.normal_(0, std);
}

AUTOGRAD_CONTAINER_CLASS(ScatterSumTrunk) {
  ag::Container typeEmbed_, enoughEnergyEmbed_, defilerFeatEmbed_;
  virtual void reset() override {
    AUTOGRAD_REGISTER(
        typeEmbed_,
        ag::Embedding(cherrypi::UnitTypeFeaturizer::kNumUnitTypes, embedSize)
            .make());
    AUTOGRAD_REGISTER(
        enoughEnergyEmbed_,
        ag::Embedding(
            8, // Combination of 3 bools

            3)
            .make());
    AUTOGRAD_REGISTER(
        defilerFeatEmbed_, ag::Linear(4, kDefilerFeatures).make());
  }

  virtual ag::Variant forward(ag::Variant input) override {
    auto& inp = input.getDict().at("features").getDict();
    auto& mapFeats = inp.at("map_features").get();
    // These are all B x nUnits x nFeats
    // Types has B * nFeats = 1
    // Locs has B * nFeats = 2 (y, x)
    std::vector<torch::Tensor> unitsLocs = {
        inp.at("our_loc").get(),
        inp.at("nmy_loc").get(),
        inp.at("our_loc_t_1").get(),
        inp.at("our_loc_t_2").get(),
        inp.at("nmy_loc_t_1").get(),
        inp.at("nmy_loc_t_2").get(),
    };
    std::vector<torch::Tensor> unitsTypes = {
        inp.at("our_types").get(),
        inp.at("nmy_types").get(),
        inp.at("our_types_t_1").get(),
        inp.at("our_types_t_2").get(),
        inp.at("nmy_types_t_1").get(),
        inp.at("nmy_types_t_2").get(),
    };
    auto& defilerPosition = inp.at("defiler_position").get();
    auto& unitPlagued = inp.at("unit_plagued").get();
    auto& darkSwarmPosition = inp.at("dark_swarm_position").get();
    auto& darkSwarmTimer = inp.at("dark_swarm_timer").get();
    auto& energy = inp.at("energy").get();
    auto& enoughEnergyIndicator = inp.at("enough_energy_indicator").get();

    auto H_BT = kMapDim;
    auto W_BT = kMapDim;

    std::vector<torch::Tensor> outFeatures;

    for (auto i = 0; i < unitsLocs.size(); ++i) {
      auto types = unitsTypes[i].squeeze(-1);
      auto locs = unitsLocs[i];
      auto emb = typeEmbed_->forward(types.to(torch::kLong))[0];
      auto noUnits = types == 0;
      emb.masked_fill_(noUnits.unsqueeze(-1), 0);
      outFeatures.push_back(common::scatterSum2d(locs / 4, emb, {H_BT, W_BT}));
    }

    auto enoughEnergyEmbed =
        enoughEnergyEmbed_->forward(enoughEnergyIndicator.to(torch::kLong))[0];
    auto defilerEmb = defilerFeatEmbed_->forward(
        {torch::cat({enoughEnergyEmbed, energy.unsqueeze(1)}, -1)})[0];
    auto defilerScattered =
        common::scatterSum2d(defilerPosition, defilerEmb, {H_BT, W_BT});
    outFeatures.insert(
        outFeatures.end(),
        {mapFeats,
         unitPlagued,
         darkSwarmPosition,
         darkSwarmTimer,
         defilerScattered});

    return torch::cat(outFeatures, 1);
  }
};

void ResidualBlock::reset() {
  auto block1 = ag::Sequential();
  block1.append(ag::Conv2d(in_channels_, mid_channels_, kernel_size_)
                    .padding(padding_)
                    .stride(stride_)
                    .make());
  if (batchnorm_) {
    block1.append(ag::BatchNorm(mid_channels_).stateful(true).make());
  }
  block1.append(ag::Functional(nonlin_).make());
  block1.append(ag::Conv2d(mid_channels_, in_channels_, kernel_size_)
                    .padding(padding_)
                    .stride(stride_)
                    .make());
  if (batchnorm_) {
    block1.append(ag::BatchNorm(in_channels_).stateful(true).make());
  }
  block1_ = add(block1.make(), "block1");
  auto block2 = ag::Sequential();
  block2.append(ag::Functional(nonlin_).make());
  if (in_channels_ != out_channels_) {
    block2.append(ag::Conv2d(in_channels_, out_channels_, 1).make());
    block2.append(ag::Functional(nonlin_).make());
  }
  block2_ = add(block2.make(), "block2");
}

ag::Variant ResidualBlock::forward(ag::Variant inp) {
  torch::Tensor res;
  if (inp.isTensorList()) {
    if (inp.getTensorList().size() != 1) {
      throw std::runtime_error(
          "Malformed model input: " +
          std::to_string(inp.getTensorList().size()) + " inputs");
    }
    res = inp.getTensorList()[0];
  } else if (inp.isTensor()) {
    res = inp.get();
  } else {
    throw std::runtime_error("Forward received unsupported type");
  }
  auto output = block1_->forward(res)[0];
  return block2_->forward({output + res});
}

std::shared_ptr<MicroFeaturizer> DefileConv2dModel::getFeaturizer() {
  return std::make_shared<DefileConv2dFeaturizer>();
}

ag::Variant DefileConv2dFeaturizer::featurize(cherrypi::State* state) {
  //  Defogger style unit type count by build tile resolution
  auto udf = cherrypi::UnitTypeDefoggerFeaturizer();
  // [2*U x 64 x 64]
  auto unitCount = udf.toDefoggerFeature(
      udf.extract(
          state,
          state->unitsInfo().liveUnits(),
          cherrypi::Rect(
              {-mapOffset(), -mapOffset()},
              kMapWidth + mapPadding(),
              kMapHeight + mapPadding())),
      torchcraft::BW::XYWalktilesPerBuildtile,
      torchcraft::BW::XYWalktilesPerBuildtile);

  // Mark current position of the defiler
  int widthBt =
      (kMapWidth + mapPadding()) / torchcraft::BW::XYWalktilesPerBuildtile;
  int heightBt =
      (kMapHeight + mapPadding()) / torchcraft::BW::XYWalktilesPerBuildtile;
  auto dPosition = torch::zeros({widthBt, heightBt});
  // start with single Defiler case
  if (state->unitsInfo()
          .myUnitsOfType(cherrypi::buildtypes::Zerg_Defiler)
          .size() != 1) {
    VLOG(0) << "We got "
            << state->unitsInfo()
                   .myUnitsOfType(cherrypi::buildtypes::Zerg_Defiler)
                   .size()
            << " defilers.";
    throw std::runtime_error("Defiler number is not correct.");
  }

  auto pos = state->unitsInfo()
                 .myUnitsOfType(cherrypi::buildtypes::Zerg_Defiler)[0]
                 ->pos();
  int x = pos.x / torchcraft::BW::XYWalktilesPerBuildtile;
  int y = pos.y / torchcraft::BW::XYWalktilesPerBuildtile;
  auto a = dPosition.accessor<float, 2>();
  a[y][x] = 1;

  auto ret = ag::VariantDict{
      {"unit_count", unitCount.tensor},
      {"last_unit_count",
       lastUnitCounts.defined() ? lastUnitCounts : unitCount.tensor},
      {"defiler_position", dPosition.unsqueeze(0)},
  };
  lastUnitCounts = unitCount.tensor;
  return ret;
}

ag::Variant DefileConvNetFeaturizer::featurize(cherrypi::State* state) {
  auto defilers =
      state->unitsInfo().myUnitsOfType(cherrypi::buildtypes::Zerg_Defiler);
  auto bbox = cherrypi::Rect(
      {-mapOffset(), -mapOffset()},
      {kMapHeight + mapOffset(), kMapWidth + mapOffset()});
  int64_t batchSize = defilers.size();
  ag::tensor_list defilerPosition;
  ag::tensor_list energyList;
  ag::tensor_list enoughEnergyIndicatorTensorList;

  int height = (kMapHeight + mapPadding() - res) / stride + 1;
  int width = (kMapWidth + mapPadding() - res) / stride + 1;
  // static deatures
  auto mapFeatures =
      featurizePlain(state, {cherrypi::PlainFeatureType::Walkability}, bbox);
  auto subMapFeatures = subsampleFeature(
      mapFeatures, cherrypi::SubsampleMethod::Average, res, stride);

  auto const& ourUnits = state->unitsInfo().myUnits();
  auto nmyUnits = state->unitsInfo().enemyUnitsMapHacked();
  auto unitFeaturizer = cherrypi::UnitStatFeaturizer();
  auto unitType = cherrypi::UnitTypeFeaturizer();
  auto ourTypes = unitType.extract(state, ourUnits, bbox).data;
  auto nmyTypes = unitType.extract(state, nmyUnits, bbox).data;
  auto ourUnitFeatures = unitFeaturizer.extract(state, ourUnits, bbox);
  auto nmyUnitFeatures = unitFeaturizer.extract(state, nmyUnits, bbox);

  auto unitPlagued =
      udf.toDefoggerFeature(
             udf.extract(
                 state,
                 [](cherrypi::Unit* u) {
                   return u->flag(torchcraft::Unit::Flags::Plagued);
                 },
                 cherrypi::Rect(
                     {-mapOffset(), -mapOffset()},
                     kMapWidth + mapPadding(),
                     kMapHeight + mapPadding())),
             res,
             stride)
          .tensor.narrow(0, 118, 118)
          .sum(0)
          .div(10.)
          .unsqueeze(0);
  //  Mark the position of darkswarm
  auto darkSwarmPosition = torch::zeros({height, width});
  auto nertualUnits = state->unitsInfo().neutralUnits();
  cherrypi::UnitsInfo::Units darkSwarms(nertualUnits.size());
  auto it = std::copy_if(
      nertualUnits.begin(),
      nertualUnits.end(),
      darkSwarms.begin(),
      [](cherrypi::Unit* u) { return u->type->name == "Spell_Dark_Swarm"; });
  darkSwarms.resize(std::distance(darkSwarms.begin(), it));
  // map the position on map
  auto darkSwarmTimer = torch::zeros({height, width});
  for (const auto& u : darkSwarms) {
    auto y = u->pos().y;
    auto x = u->pos().x;
    uint64_t div1 = stride == 1 ? torchcraft::BW::XYPixelsPerWalktile
                                : torchcraft::BW::XYPixelsPerWalktile *
            torchcraft::BW::XYWalktilesPerBuildtile;
    uint64_t div2 = stride == 1 ? 1 : torchcraft::BW::XYWalktilesPerBuildtile;
    x = x / div2;
    y = y / div2;
    uint64_t left = x - u->type->dimensionLeft / div1;
    uint64_t right = x + u->type->dimensionRight / div1;
    uint64_t up = y - u->type->dimensionUp / div1;
    uint64_t down = y + u->type->dimensionDown / div1;
    auto timer = (state->currentFrame() - u->firstSeen) / 1000.;
    darkSwarmPosition.narrow(0, up, down - up)
        .narrow(1, left, right - left)
        .fill_(1);
    darkSwarmTimer.narrow(0, up, down - up)
        .narrow(1, left, right - left)
        .fill_(timer);
  }
  darkSwarmTimer.unsqueeze_(0);
  darkSwarmPosition.unsqueeze_(0);

  // Mark current position of the defiler
  for (const auto& defiler : defilers) {
    auto dPosition = torch::zeros({2});
    auto pos = defiler->pos();
    dPosition[0] = cherrypi::utils::clamp(
        0, ((int)pos.y - res) / stride + 1, height - 1); // y
    dPosition[1] = cherrypi::utils::clamp(
        0, ((int)pos.x - res) / stride + 1, width - 1); // x
    defilerPosition.push_back(dPosition.unsqueeze(0));
    auto energy = torch::zeros({1});
    energy[0] = (float)defiler->unit.energy / 200.;
    energyList.push_back(energy);
    auto enoughEnergyIndicatorTensor = torch::zeros({1});
    int enoughEnergyIndicator = 0;
    int enoughPlague = defiler->unit.energy >= 150 ? 1 : 0;
    enoughEnergyIndicator += enoughPlague;
    int enoughDarkSwarm = (defiler->unit.energy >= 100 ? 1 : 0) << 1;
    enoughEnergyIndicator += enoughDarkSwarm;
    int enoughDoubleDarkSwarm = (defiler->unit.energy >= 200 ? 1 : 0) << 2;
    enoughEnergyIndicator += enoughDoubleDarkSwarm;
    enoughEnergyIndicatorTensor[0] = enoughEnergyIndicator;
    enoughEnergyIndicatorTensorList.push_back(enoughEnergyIndicatorTensor);
  }

  auto features = ag::VariantDict{
      {"map_features", subMapFeatures.tensor.expand({batchSize, -1, -1, -1})},
      {"our_loc", ourUnitFeatures.positions.expand({batchSize, -1, -1})},
      {"our_loc_t_1",
       featuresFrom1.find("our_loc") != featuresFrom1.end()
           ? featuresFrom1["our_loc"].expand({batchSize, -1, -1})
           : ourUnitFeatures.positions.expand({batchSize, -1, -1})},
      {"our_loc_t_2",
       featuresFrom2.find("our_loc") != featuresFrom2.end()
           ? featuresFrom2["our_loc"].expand({batchSize, -1, -1})
           : ourUnitFeatures.positions.expand({batchSize, -1, -1})},
      {"nmy_loc", nmyUnitFeatures.positions.expand({batchSize, -1, -1})},
      {"nmy_loc_t_1",
       featuresFrom1.find("nmy_loc") != featuresFrom1.end()
           ? featuresFrom1["nmy_loc"].expand({batchSize, -1, -1})
           : nmyUnitFeatures.positions.expand({batchSize, -1, -1})},
      {"nmy_loc_t_2",
       featuresFrom2.find("nmy_loc") != featuresFrom2.end()
           ? featuresFrom2["nmy_loc"].expand({batchSize, -1, -1})
           : nmyUnitFeatures.positions.expand({batchSize, -1, -1})},
      {"our_types", ourTypes.expand({batchSize, -1, -1})},
      {"our_types_t_1",
       featuresFrom1.find("our_types") != featuresFrom1.end()
           ? featuresFrom1["our_types"].expand({batchSize, -1, -1})
           : ourTypes.expand({batchSize, -1, -1})},
      {"our_types_t_2",
       featuresFrom2.find("our_types") != featuresFrom2.end()
           ? featuresFrom2["our_types"].expand({batchSize, -1, -1})
           : ourTypes.expand({batchSize, -1, -1})},
      {"nmy_types", nmyTypes.expand({batchSize, -1, -1})},
      {"nmy_types_t_1",
       featuresFrom1.find("nmy_types") != featuresFrom1.end()
           ? featuresFrom1["nmy_types"].expand({batchSize, -1, -1})
           : nmyTypes.expand({batchSize, -1, -1})},
      {"nmy_types_t_2",
       featuresFrom2.find("nmy_types") != featuresFrom2.end()
           ? featuresFrom2["nmy_types"].expand({batchSize, -1, -1})
           : nmyTypes.expand({batchSize, -1, -1})},
      {"defiler_position", torch::stack(defilerPosition)},
      {"unit_plagued", unitPlagued.expand({batchSize, -1, -1, -1})},
      {"dark_swarm_position",
       darkSwarmPosition.expand({batchSize, -1, -1, -1})},
      {"dark_swarm_timer", darkSwarmTimer.expand({batchSize, -1, -1, -1})},
      {"energy", torch::stack(energyList)},
      {"enough_energy_indicator",
       torch::stack(enoughEnergyIndicatorTensorList)}};
  // Dump some heatmaps for visualization
  std::unordered_map<std::string, ag::Variant> heatmaps;
  heatmaps["map_features"] = subMapFeatures.tensor.mean(0);
  heatmaps["unit_plagued"] = unitPlagued.squeeze(0);
  heatmaps["dark_swarm_position"] = darkSwarmPosition.squeeze(0);
  heatmaps["darkSwarmTimer"] = darkSwarmTimer.squeeze(0);
  featuresFrom2 = featuresFrom1;
  featuresFrom1["our_loc"] = ourUnitFeatures.positions;
  featuresFrom1["nmy_loc"] = nmyUnitFeatures.positions;
  featuresFrom1["our_types"] = ourTypes;
  featuresFrom1["nmy_types"] = nmyTypes;
  return ag::VariantDict{{"heatmaps", heatmaps}, {"features", features}};
}

void DefileConv2dModel::reset() {
  convnet_ =
      add(ag::Sequential()
              .append(ag::Conv2d(4 * 118 + 1, 2, 5).padding(2).stride(1).make())
              .make(),
          "convnet");
}

ag::Variant DefileConv2dModel::forward(ag::Variant inp) {
  auto ft = inp.getDict().at("features").getDict();
  auto unitCountT = ft.at("unit_count").get();
  auto unitCountTLast = ft.at("last_unit_count").get();
  auto dPosition = ft.at("defiler_position").get();
  auto convInput =
      at::cat({unitCountT, unitCountTLast, dPosition}, 0).unsqueeze(0);
  auto convOutput = convnet_->forward(convInput)[0];
  int batchSize = convOutput.sizes()[0];
  auto outputTensor = at::softmax(convOutput.view({batchSize, -1}), 1);
  return {{"Pi", outputTensor.view({batchSize, -1})}};
}

std::vector<PFMicroActionModel::PFMicroAction> DefileConvNetModel::decodeOutput(
    cherrypi::State* state,
    ag::Variant /* input */,
    ag::Variant _output) {
  auto output = _output.getDict();
  std::vector<PFMicroActionModel::PFMicroAction> actions;
  auto actionTaken = output.at(cpid::kActionKey).get();
  auto actionProbas = output.at(cpid::kPiKey).get();
  auto& ourUnits =
      state->unitsInfo().myUnitsOfType(cherrypi::buildtypes::Zerg_Defiler);
  LOG_IF(FATAL, ourUnits.size() != (size_t)actionTaken.size(0))
      << "Wrong batchSize " << actionTaken.size(0)
      << " from model, expected to match number of Defilers "
      << ourUnits.size();
  LOG_IF(FATAL, actionProbas[0].sizes() != at::IntList({kMapDim * kMapDim * 2}))
      << "Model output at key \"Pi\" has wrong size " << actionProbas.sizes();

  for (auto i = 0U; i < ourUnits.size(); ++i) {
    auto action = actionTaken[i].item<int64_t>();
    auto actionValue = actionProbas[i][action].item<float>();
    int x = action % kMapDim;
    action /= kMapDim;
    int y = action % kMapDim;
    action /= kMapDim;
    bool isDarkSwarm = action % 2 == 1;
    float threshold = plague_threshold_;
    auto PFMicroAction = PFMicroAction::Plague;
    if (isDarkSwarm) {
      threshold = dark_swarm_threshold_;
      PFMicroAction = PFMicroAction::DarkSwarm;
    }

    if (actionValue < threshold) {
      continue;
    }

    x = cherrypi::utils::clamp(x, 0, state->mapWidth());
    y = cherrypi::utils::clamp(y, 0, state->mapWidth());
    actions.push_back({PFMicroAction,
                       ourUnits[i],
                       nullptr,
                       {x * torchcraft::BW::XYWalktilesPerBuildtile,
                        y * torchcraft::BW::XYWalktilesPerBuildtile}});
  }
  return actions;
}

void DefileResConv2dModelBT2::reset() {
  DefileConvNetModel* basePtr = dynamic_cast<DefileConvNetModel*>(this);
  basePtr->n_input_channels_ = n_input_channels_;
  basePtr->plague_threshold_ = plague_threshold_;
  basePtr->dark_swarm_threshold_ = dark_swarm_threshold_;
  basePtr->mask_plague_ = mask_plague_;
  basePtr->mask_dark_swarm_ = mask_dark_swarm_;
  DefileConvNetModel::reset();

  convLayers_ = std::vector<ag::Container>();
  convLayers_.push_back(
      add(ag::Sequential()
              .append(ag::Conv2d(n_input_channels_, 32, 3).padding(1).make())
              .append(ag::Functional(torch::relu).make())
              .append(ag::Functional([](torch::Tensor x) {
                        return torch::max_pool2d(x, {2, 2}, 2);
                      })
                          .make())
              .make(),
          "conv1"));
  convLayers_.push_back(
      add(ag::Sequential()
              .append(ResidualBlock()
                          .in_channels(32)
                          .out_channels(32)
                          .kernel_size(3)
                          .padding(1)
                          .make())
              .append(ag::Functional([](torch::Tensor x) {
                        return torch::max_pool2d(x, {2, 2}, 2);
                      })
                          .make())
              .append(ResidualBlock()
                          .in_channels(32)
                          .out_channels(32)
                          .kernel_size(3)
                          .padding(1)
                          .make())
              .append(ResidualBlock()
                          .in_channels(32)
                          .out_channels(32)
                          .kernel_size(3)
                          .padding(1)
                          .make())
              .append(ag::Functional([](torch::Tensor x) {
                        return torch::upsample_bicubic2d(x, {64, 64}, true);
                      })
                          .make())
              .append(ResidualBlock()
                          .in_channels(32)
                          .out_channels(32)
                          .kernel_size(3)
                          .padding(1)
                          .make())
              .append(ag::Functional([](torch::Tensor x) {
                        return torch::upsample_bicubic2d(x, {128, 128}, true);
                      })
                          .make())
              .make(),
          "residualBlock1"));
  convLayers_.push_back(
      add(ag::Sequential()
              .append(ag::Conv2d(32, 2, 3).padding(4).make())
              .append(ag::Functional(torch::relu).make())
              .make(),
          "conv3"));
}

void DefileConvNetModel::reset() {
  auto avgPool = [](torch::Tensor x) {
    return torch::avg_pool2d(x, {4, 4}, 4);
  };

  AUTOGRAD_REGISTER(scatterSum_, ScatterSumTrunk().make());
  AUTOGRAD_REGISTER(
      valuePooling_,
      ag::Sequential()
          .append(ag::Conv2d(2 * embedSize + 1, 8, 3).padding(1).make())
          .append(ag::Functional(avgPool).make())
          .append(ag::Functional(torch::relu).make())
          .append(ag::BatchNorm(8).stateful(true).make())
          .append(ag::Conv2d(8, 16, 3).padding(1).make())
          .append(ag::Functional(avgPool).make())
          .append(ag::Functional(torch::relu).make())
          .append(ag::BatchNorm(16).stateful(true).make())
          .make());
  AUTOGRAD_REGISTER(
      valueHead_,
      ag::Sequential()
          .append(ag::Linear(32 * 32, 1).make())
          // make the value output the same scale as reward
          .make());
}

void DefileResConv2dBaseLineModel::reset() {
  DefileConvNetModel* basePtr = dynamic_cast<DefileConvNetModel*>(this);
  basePtr->n_input_channels_ = n_input_channels_;
  basePtr->plague_threshold_ = plague_threshold_;
  basePtr->dark_swarm_threshold_ = dark_swarm_threshold_;
  basePtr->mask_plague_ = mask_plague_;
  basePtr->mask_dark_swarm_ = mask_dark_swarm_;
  DefileConvNetModel::reset();
  convLayers_ = std::vector<ag::Container>();
  convLayers_.push_back(
      add(ag::Sequential()
              .append(ag::Conv2d(n_input_channels_, 32, 5).padding(2).make())
              .append(ag::Functional(torch::relu).make())
              .append(ag::Conv2d(32, 2, 3).padding(1).make())
              .make(),
          "conv1"));
}

void DefileResEncoderDecoderModel::reset() {
  DefileConvNetModel* basePtr = dynamic_cast<DefileConvNetModel*>(this);
  basePtr->n_input_channels_ = n_input_channels_;
  basePtr->plague_threshold_ = plague_threshold_;
  basePtr->dark_swarm_threshold_ = dark_swarm_threshold_;
  basePtr->mask_plague_ = mask_plague_;
  basePtr->mask_dark_swarm_ = mask_dark_swarm_;
  DefileConvNetModel::reset();
  convLayers_ = std::vector<ag::Container>();
  convLayers_.push_back(
      add(ag::Sequential()
              .append(common::EncoderDecoder()
                          .inShape({n_input_channels_, kMapDim, kMapDim})
                          .intermSize(32)
                          .nOutFeats(2)
                          .stride(1)
                          .numBlocks(3)
                          .batchNorm(true)
                          .make())
              .make(),
          "conv1"));
}

ag::Variant DefileConvNetModel::forward(ag::Variant inp) {
  auto ft = inp.getDict().at("features").getDict();
  auto convInput = scatterSum_->forward(inp)[0];
  auto unitsCount = convInput.narrow(1, 0, 2 * embedSize);

  VLOG(1) << "norm of the input " << convInput.norm().item<float>();
  for (size_t i = 0; i < convLayers_.size(); i++) {
    auto output = convLayers_[i]->forward(convInput)[0];
    VLOG(1) << "norm of the input " << output.norm().item<float>();
    VLOG(1) << "size of the input " << output.sizes();

    convInput = output;
  }
  auto batchSize = convInput.sizes()[0];

  if (convInput.sizes()[2] != kMapDim || convInput.sizes()[3] != kMapDim) {
    int heightStart = (convInput.sizes()[2] - kMapDim) / 2;
    int widthStart = (convInput.sizes()[3] - kMapDim) / 2;
    convInput = convInput.narrow(2, heightStart, kMapDim)
                    .narrow(3, widthStart, kMapDim)
                    .clone()
                    .contiguous();
  }
  auto sizes = convInput.sizes().vec();
  auto defilerPosition = ft.at("defiler_position").get().clone().to(at::kCPU);
  auto a = defilerPosition.accessor<float, 3>();
  std::vector<torch::Tensor> defilerPosiionMasks;
  for (int i = 0; i < batchSize; i++) {
    auto defilerPosiionMask = torch::zeros({2, sizes[2], sizes[3]});
    auto b = a[i][0];
    int yStart = cherrypi::utils::clamp(0, (int)(b[0] - 30), (int)sizes[2] - 1);
    int xStart = cherrypi::utils::clamp(0, (int)(b[1] - 30), (int)sizes[3] - 1);
    defilerPosiionMask
        .narrow(1, yStart, std::min((int)(sizes[2] - yStart), 30 * 2))
        .narrow(2, xStart, std::min((int)(sizes[3] - xStart), 30 * 2))
        .fill_(1);
    if (mask_plague_) {
      defilerPosiionMask.narrow(0, 0, 1).fill_(0);
    }
    if (mask_dark_swarm_) {
      defilerPosiionMask.narrow(0, 1, 1).fill_(0);
    }
    defilerPosiionMasks.push_back(defilerPosiionMask);
  }
  auto defilerPosiionMasksTensor = at::stack(defilerPosiionMasks).to(at::kCUDA);
  auto Pi = common::maskedSoftmax(
                convInput.view({batchSize, -1}),
                defilerPosiionMasksTensor.view({batchSize, -1}),
                1,
                0)
                .view(sizes);
  auto batchSizes = ft[cpid::SubBatchAsyncBatcher::kBatchInfoKey].getDict().at(
      "defiler_position");
  return ag::VariantDict{// {cpid::kValueKey, valueTensor},
                         {cpid::kPiKey, Pi.view({batchSize, -1})},
                         {cpid::SubBatchAsyncBatcher::kBatchInfoKey,
                          ag::VariantDict{{cpid::kPiKey, batchSizes}}}};
}

std::shared_ptr<MicroFeaturizer> DefileConvNetModel::getFeaturizer() {
  return std::make_shared<DefileConvNetFeaturizer>();
}

} // namespace microbattles
