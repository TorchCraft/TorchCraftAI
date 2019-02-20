/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "models.h"

#include "models/bos/sample.h"
#include "utils.h"

#include <common/autograd/models.h>

DEFINE_string(bos_model_type, "lstm", "linear|mlp|lstm|celstm");
DEFINE_bool(bos_bo_input, true, "Input current build order");
DEFINE_bool(bos_mapid_input, true, "Input map ID");
DEFINE_bool(bos_time_input, true, "Input current game time");
DEFINE_bool(bos_res_input, true, "Input current resources");
DEFINE_bool(bos_tech_input, true, "Input current tech");
DEFINE_bool(bos_ptech_input, true, "Input pending tech");
DEFINE_bool(bos_units_input, true, "Input current unit counts");
DEFINE_bool(bos_fabs_input, false, "Input future autobuild states");
DEFINE_int32(bos_hid_dim, 2048, "Model hidden dimension");
DEFINE_int32(bos_num_layers, 1, "Number of layers");
DEFINE_string(bos_targets, "ALL", "Use these builds for random switches");
DEFINE_bool(bos_celstm_deep, false, "Deep convnet for celstm model");
DEFINE_bool(
    bos_celstm_map_features,
    false,
    "Include map features in celstm CNN");
DEFINE_int32(bos_celstm_spatial_embsize, 128, "Output size of celstm CNN");

namespace cherrypi {
namespace bos {

namespace {

// Time embeddings won't consider longer games
auto constexpr kMaxFrames = 86400 * 1.5;
auto constexpr kNumFramesPerSec = 24;
auto constexpr kNumFramesPerMin = kNumFramesPerSec * 60;

torch::Tensor makeMasks(std::set<std::string> targetBuilds) {
  auto const& boMap = buildOrderMap();
  ag::tensor_list raceMasks;
  for (auto race :
       {+tc::BW::Race::Zerg, +tc::BW::Race::Terran, +tc::BW::Race::Protoss}) {
    raceMasks.push_back(getBuildOrderMaskByRace(race._to_integral()));
    auto acc = raceMasks.back().accessor<float, 1>();
    for (auto const& it : boMap) {
      if (targetBuilds.find(it.first) == targetBuilds.end()) {
        acc[it.second] = 0.0f;
      }
    }
  }
  return torch::stack(raceMasks);
}

ag::Variant doHeads(
    ag::Container m,
    torch::Tensor x,
    torch::Tensor enemyRace,
    torch::Tensor masks) {
  auto heads = m->forward({x})[0];

  auto bmask = masks.index_select(0, enemyRace.view({-1}));
  if (heads.dim() == 2) {
    bmask = bmask.view_as(heads);
  } else {
    // hopefully dim == 3
    bmask = bmask.view({1, heads.size(1), heads.size(2)}).expand_as(heads);
  }
  auto vHeads = at::sigmoid(heads) * bmask;
  auto pi = common::maskedSoftmax(heads, bmask, 1);
  // Dummy value head
  auto V = torch::zeros({1}).to(vHeads.options().device());
  return ag::VariantDict{
      {"Q", heads}, {"vHeads", vHeads}, {"Pi", pi}, {"V", V}};
}

} // namespace

ag::Container modelMakeFromCli(double dropout) {
  auto targets = FLAGS_bos_targets;
  if (targets.empty() || targets == "ALL") {
    targets = allowedTargetsAsFlag();
  }
  auto const& boMap = buildOrderMap();
  auto builds = utils::stringSplit(targets, '_');
  auto targetBuilds = std::set<std::string>(builds.begin(), builds.end());
  if (FLAGS_bos_model_type == "linear") {
    return LinearModel()
        .n_builds(boMap.size())
        .mapid_embsize(FLAGS_bos_mapid_input ? 8 : 0)
        .time_embsize(FLAGS_bos_time_input ? 1 : 0)
        .resources_embsize(FLAGS_bos_res_input ? 8 : 0)
        .tech_embsize(FLAGS_bos_tech_input ? 8 : 0)
        .ptech_embsize(FLAGS_bos_ptech_input ? 8 : 0)
        .bo_embsize(FLAGS_bos_bo_input ? 8 : 0)
        .zero_units(FLAGS_bos_units_input ? false : true)
        .hid_dim(FLAGS_bos_hid_dim)
        .use_fabs(FLAGS_bos_fabs_input)
        .target_builds(targetBuilds)
        .make();
  } else if (FLAGS_bos_model_type == "mlp") {
    return MlpModel()
        .n_builds(boMap.size())
        .mapid_embsize(FLAGS_bos_mapid_input ? 8 : 0)
        .time_embsize(FLAGS_bos_time_input ? 1 : 0)
        .resources_embsize(FLAGS_bos_res_input ? 8 : 0)
        .tech_embsize(FLAGS_bos_tech_input ? 8 : 0)
        .ptech_embsize(FLAGS_bos_ptech_input ? 8 : 0)
        .bo_embsize(FLAGS_bos_bo_input ? 8 : 0)
        .zero_units(FLAGS_bos_units_input ? false : true)
        .hid_dim(FLAGS_bos_hid_dim)
        .n_layers(FLAGS_bos_num_layers)
        .use_fabs(FLAGS_bos_fabs_input)
        .target_builds(targetBuilds)
        .make();
  } else if (FLAGS_bos_model_type == "lstm") {
    return LstmModel()
        .n_builds(boMap.size())
        .mapid_embsize(FLAGS_bos_mapid_input ? 8 : 0)
        .time_embsize(FLAGS_bos_time_input ? 1 : 0)
        .resources_embsize(FLAGS_bos_res_input ? 8 : 0)
        .tech_embsize(FLAGS_bos_tech_input ? 8 : 0)
        .ptech_embsize(FLAGS_bos_ptech_input ? 8 : 0)
        .bo_embsize(FLAGS_bos_bo_input ? 8 : 0)
        .zero_units(FLAGS_bos_units_input ? false : true)
        .hid_dim(FLAGS_bos_hid_dim)
        .n_layers(FLAGS_bos_num_layers)
        .use_fabs(FLAGS_bos_fabs_input)
        .target_builds(targetBuilds)
        .make();
  } else if (FLAGS_bos_model_type == "celstm") {
    return ConvEncLstmModel()
        .n_builds(boMap.size())
        .time_embsize(FLAGS_bos_time_input ? 1 : 0)
        .resources_embsize(FLAGS_bos_res_input ? 8 : 0)
        .tech_embsize(FLAGS_bos_tech_input ? 8 : 0)
        .ptech_embsize(FLAGS_bos_ptech_input ? 8 : 0)
        .bo_embsize(FLAGS_bos_bo_input ? 8 : 0)
        .hid_dim(FLAGS_bos_hid_dim)
        .n_layers(FLAGS_bos_num_layers)
        .deep_conv(FLAGS_bos_celstm_deep)
        .map_features(FLAGS_bos_celstm_map_features)
        .spatial_embsize(FLAGS_bos_celstm_spatial_embsize)
        .use_fabs(FLAGS_bos_fabs_input)
        .target_builds(targetBuilds)
        .make();
  } else if (FLAGS_bos_model_type == "idle") {
    return IdleModel().make();
  } else {
    throw std::runtime_error("Unknown model type: " + FLAGS_bos_model_type);
  }
}

std::map<std::string, std::string> modelFlags() {
  std::map<std::string, std::string> flags;
  std::vector<gflags::CommandLineFlagInfo> config;
  gflags::GetAllFlags(&config);
  for (auto const& c : config) {
    if (c.filename == __FILE__) {
      flags[c.name] = c.current_value;
    }
  }
  return flags;
}

void IdleModel::reset() {
  VLOG(0) << "Reset called! Please notice this is a idle model.";
  return;
}

ag::Variant IdleModel::forward(ag::Variant in) {
  return ag::VariantDict{};
}

void MapRaceEcoTimeFeaturize::reset() {
  if (mapid_embsize_ > 0) {
    // Pick some amount that's higher than the maps we have...
    embedM_ = add(ag::Embedding(24, mapid_embsize_).make(), "embedM");
  }
  if (race_embsize_ > 0) {
    embedR_ = add(ag::Embedding(3, race_embsize_).make(), "embedR");
  }
  // resources: ore, gas, used_psi, total_psi
  // (this is just a linear combination)
  if (resources_embsize_ > 0) {
    embedRS_ = add(ag::Linear(4, resources_embsize_).make(), "embedRS");
  }
  // tech and upgrades are 142 binary values
  // (this is just a linear combination)
  if (tech_embsize_ > 0) {
    embedT_ = add(ag::Linear(142, tech_embsize_).make(), "embedT");
  }
  if (ptech_embsize_ > 0) {
    embedPT_ = add(ag::Linear(142, ptech_embsize_).make(), "embedPT");
  }
  // time is stupidly embedded with minute resolution
  if (time_embsize_ > 0) {
    embedTM_ = add(
        ag::Embedding(std::ceil(kMaxFrames / kNumFramesPerMin), time_embsize_)
            .make(),
        "embedTM");
  }
  // (current) build order embedding
  if (bo_embsize_ > 0) {
    embedBO_ = add(ag::Embedding(n_builds_, bo_embsize_).make(), "embedBO");
  }
}

ag::Variant MapRaceEcoTimeFeaturize::forward(ag::Variant in) {
  ag::tensor_list& input = in.getTensorList();
  if (input.size() != 7) {
    throw std::runtime_error(
        "Malformed model input: " + std::to_string(input.size()) + " inputs");
  }

  auto mapId = input[0]; // Bx1
  auto race = input[1]; // Bx2
  auto resources = input[2]; // Bx4
  auto techs = input[3]; // Bx142
  auto ptechs = input[4]; // Bx142
  auto time = input[5]; // Bx1
  auto bo = input[6]; // Bx1

  auto hasTimeDim = false;
  for (auto& in : input) {
    hasTimeDim |= (in.defined() && in.dim() > 2);
  }

  ag::tensor_list outputs;
  if (embedM_ != nullptr) {
    outputs.push_back(embedM_->forward({mapId})[0]);
    outputs.back().squeeze_(outputs.back().dim() - 2);
    VLOG(2) << mapId.sizes() << " -> " << outputs.back().sizes();
  }
  if (embedR_ != nullptr) {
    auto outR = embedR_->forward({race})[0];
    // Flatten last two dimensions
    auto sizes = outR.sizes().vec();
    sizes.pop_back();
    sizes.pop_back();
    sizes.push_back(-1);
    outR = outR.view(sizes);
    if (hasTimeDim) { // TxBxX
      sizes[0] = time.size(0);
      outR = outR.expand(sizes);
    }
    outputs.push_back(outR);
    VLOG(2) << race.sizes() << " -> " << outputs.back().sizes();
  }
  if (embedRS_ != nullptr) {
    outputs.push_back(embedRS_->forward({resources})[0]);
    VLOG(2) << resources.sizes() << " -> " << outputs.back().sizes();
  }
  if (embedT_ != nullptr) {
    outputs.push_back(embedT_->forward({techs})[0]);
    VLOG(2) << techs.sizes() << " -> " << outputs.back().sizes();
  }
  if (embedPT_ != nullptr) {
    outputs.push_back(embedPT_->forward({ptechs})[0]);
    VLOG(2) << ptechs.sizes() << " -> " << outputs.back().sizes();
  }
  if (embedTM_ != nullptr) {
    outputs.push_back(at::tanh(time.to(at::kFloat).div(kNumFramesPerMin * 10)));
    VLOG(2) << time.sizes() << " -> " << outputs.back().sizes();
  }
  if (embedBO_ != nullptr) {
    outputs.push_back(embedBO_->forward({bo})[0]);
    outputs.back().squeeze_(outputs.back().dim() - 2);
    VLOG(2) << bo.sizes() << " -> " << outputs.back().sizes();
  }

  return {at::cat(outputs, time.dim() - 1)};
}

void LinearModel::reset() {
  if (n_builds_ < 1) {
    throw std::runtime_error("n_builds must be at least 1");
  }

  auto ninput = mapid_embsize_ + race_embsize_ * 2 + resources_embsize_ +
      tech_embsize_ + ptech_embsize_ + time_embsize_ + bo_embsize_ +
      n_unit_types_ + (use_fabs_ ? n_unit_types_ / 2 * 3 : 0);

  trunk_ =
      add(MapRaceEcoTimeFeaturize()
              .mapid_embsize(mapid_embsize_)
              .race_embsize(race_embsize_)
              .resources_embsize(resources_embsize_)
              .tech_embsize(tech_embsize_)
              .ptech_embsize(ptech_embsize_)
              .time_embsize(time_embsize_)
              .bo_embsize(bo_embsize_)
              .n_builds(n_builds_)
              .make(),
          "trunk");

  // Simple linear projection
  linear_ = add(ag::Linear(ninput, hid_dim_).make(), "linear");

  // Build order value heads
  vHeads_ = add(
      ag::Sequential().append(ag::Linear(hid_dim_, n_builds_).make()).make(),
      "v_head");

  // Build order masks by race
  masks_ = makeMasks(target_builds_).to(options().device());
}

ag::Variant LinearModel::forward(ag::Variant in) {
  ag::tensor_list& features = in.getDict()["features"].getTensorList();
  if (features.size() != 9) {
    throw std::runtime_error(
        "Malformed model input: " + std::to_string(features.size()) +
        " features");
  }

  auto trunkInput = ag::tensor_list(features.begin() + 2, features.end());
  auto trunkF = trunk_->forward(trunkInput)[0];
  auto units = zero_units_ ? torch::zeros_like(features[0]) : features[0];
  ag::tensor_list min{units, trunkF};
  if (use_fabs_) {
    min.push_back(features[1]);
  }
  auto x = linear_->forward({at::cat(min, 1)})[0];

  auto enemyRace = features[3].slice(features[3].dim() - 1, 1, 2).squeeze();
  masks_ = masks_.to(options().device());
  return doHeads(vHeads_, x, enemyRace, masks_);
}

void MlpModel::reset() {
  if (n_builds_ < 1) {
    throw std::runtime_error("n_builds must be at least 1");
  }

  auto ninput = mapid_embsize_ + race_embsize_ * 2 + resources_embsize_ +
      tech_embsize_ + ptech_embsize_ + time_embsize_ + bo_embsize_ +
      n_unit_types_ + (use_fabs_ ? n_unit_types_ / 2 * 3 : 0);

  trunk_ =
      add(MapRaceEcoTimeFeaturize()
              .mapid_embsize(mapid_embsize_)
              .race_embsize(race_embsize_)
              .resources_embsize(resources_embsize_)
              .tech_embsize(tech_embsize_)
              .ptech_embsize(ptech_embsize_)
              .time_embsize(time_embsize_)
              .bo_embsize(bo_embsize_)
              .n_builds(n_builds_)
              .make(),
          "trunk");

  // Multi-layer network
  mlp_ =
      add(common::MLP()
              .nIn(ninput)
              .nLayers(n_layers_)
              .nHid(hid_dim_)
              .nOut(n_builds_)
              .nonlinearity(at::tanh)
              .make(),
          "mlp");

  // Build order value heads
  vHeads_ = add(ag::Sequential().make(), "v_head");

  // Build order masks by race
  masks_ = makeMasks(target_builds_).to(options().device());
}

ag::Variant MlpModel::forward(ag::Variant in) {
  ag::tensor_list& features = in.getDict()["features"].getTensorList();
  if (features.size() != 9) {
    throw std::runtime_error(
        "Malformed model input: " + std::to_string(features.size()) +
        " features");
  }

  auto trunkInput = ag::tensor_list(features.begin() + 2, features.end());
  auto trunkF = trunk_->forward(trunkInput)[0];
  auto units = zero_units_ ? torch::zeros_like(features[0]) : features[0];
  ag::tensor_list min{units, trunkF};
  if (use_fabs_) {
    min.push_back(features[1]);
  }
  auto x = mlp_->forward({at::cat(min, 1)})[0];

  auto enemyRace = features[3].slice(features[3].dim() - 1, 1, 2).squeeze();
  masks_ = masks_.to(options().device());
  return doHeads(vHeads_, x, enemyRace, masks_);
}

void LstmModel::reset() {
  if (n_builds_ < 1) {
    throw std::runtime_error("n_builds must be at least 1");
  }

  auto ninput = mapid_embsize_ + race_embsize_ * 2 + resources_embsize_ +
      tech_embsize_ + ptech_embsize_ + time_embsize_ + bo_embsize_ +
      n_unit_types_ + (use_fabs_ ? n_unit_types_ / 2 * 3 : 0);

  trunk_ =
      add(MapRaceEcoTimeFeaturize()
              .mapid_embsize(mapid_embsize_)
              .race_embsize(race_embsize_)
              .resources_embsize(resources_embsize_)
              .tech_embsize(tech_embsize_)
              .ptech_embsize(ptech_embsize_)
              .time_embsize(time_embsize_)
              .bo_embsize(bo_embsize_)
              .n_builds(n_builds_)
              .make(),
          "trunk");

  // LSTM
  lstm_ = add(ag::LSTM(ninput, hid_dim_).layers(n_layers_).make(), "lstm");

  // Build order value heads
  vHeads_ = add(
      ag::Sequential().append(ag::Linear(hid_dim_, n_builds_).make()).make(),
      "v_head");

  // Build order masks by race
  masks_ = makeMasks(target_builds_).to(options().device());
}

ag::Variant LstmModel::forward(ag::Variant in) {
  ag::tensor_list& features = in.getDict()["features"].getTensorList();
  if (features.size() != 9) {
    throw std::runtime_error(
        "Malformed model input: " + std::to_string(features.size()) +
        " features");
  }

  torch::Tensor hidden;
  auto& d = in.getDict();
  if (d.find("hidden") != d.end()) {
    if (!d["hidden"].getTensorList().empty()) {
      hidden = d["hidden"].getTensorList()[0];
    }
  }
  auto trunkInput = ag::tensor_list(features.begin() + 2, features.end());
  auto trunkF = trunk_->forward(trunkInput)[0];
  auto units = zero_units_ ? torch::zeros_like(features[0]) : features[0];
  ag::tensor_list min{units, trunkF};
  if (use_fabs_) {
    min.push_back(features[1]);
  }
  auto lstmIn = at::cat(min, trunkF.dim() - 1);
  auto hasTimeDim = lstmIn.dim() == 3;
  ag::Variant x;
  if (hasTimeDim) {
    x = lstm_->forward({lstmIn, hidden});
  } else {
    x = lstm_->forward({lstmIn.unsqueeze(0), hidden});
    x[0] = x[0].squeeze(0);
  }
  auto newHidden = x[1];

  auto enemyRace = features[3].slice(features[3].dim() - 1, 1, 2).squeeze();
  masks_ = masks_.to(options().device());
  auto output = doHeads(vHeads_, x[0], enemyRace, masks_);
  output.getDict()["hidden"] = ag::tensor_list{newHidden};
  return output;
}

void ConvEncLstmModel::reset() {
  if (n_builds_ < 1) {
    throw std::runtime_error("n_builds must be at least 1");
  }

  auto ninput = spatial_embsize_ + race_embsize_ * 2 + resources_embsize_ +
      tech_embsize_ + ptech_embsize_ + time_embsize_ + bo_embsize_ +
      mapid_embsize_ + n_unit_types_ + (use_fabs_ ? n_unit_types_ / 2 * 3 : 0);

  trunk_ =
      add(MapRaceEcoTimeFeaturize()
              .mapid_embsize(mapid_embsize_)
              .race_embsize(race_embsize_)
              .resources_embsize(resources_embsize_)
              .tech_embsize(tech_embsize_)
              .ptech_embsize(ptech_embsize_)
              .time_embsize(time_embsize_)
              .bo_embsize(bo_embsize_)
              .n_builds(n_builds_)
              .make(),
          "trunk");

  // CNN for map
  auto mapOut = 0;
  if (map_features_) {
    mapOut = 8;
    mapConv_ =
        add(ag::Sequential()
                .append(common::ConvBlock()
                            .kernelSize(16)
                            .stride(16)
                            .nInFeats(4)
                            .nOutFeats(mapOut)
                            .nLayers(1)
                            .nonlinearity(cnn_nonlinearity_)
                            .residual(false)
                            .make())
                .make(),
            "map_cnn");
  }

  // CNN for map and units combined
  auto unitsOut = 64;
  convnet_ = add(
      ag::Sequential()
          .append(
              ag::Conv2d(n_unit_types_ + mapOut, unitsOut, 5).stride(2).make())
          .append(ag::Functional(cnn_nonlinearity_).make())
          .make(),
      "convnet");

  cembed_ =
      add(ag::Linear(unitsOut * 6 * 6, spatial_embsize_).make(), "cembed");

  // LSTM
  lstm_ = add(ag::LSTM(ninput, hid_dim_).layers(n_layers_).make(), "lstm");

  // Build order value heads
  vHeads_ = add(
      ag::Sequential().append(ag::Linear(hid_dim_, n_builds_).make()).make(),
      "v_head");

  // Build order masks by race
  masks_ = makeMasks(target_builds_).to(options().device());
}

ag::Variant ConvEncLstmModel::forward(ag::Variant in) {
  ag::tensor_list& features = in.getDict()["features"].getTensorList();
  if (features.size() != 11) {
    throw std::runtime_error(
        "Malformed model input: " + std::to_string(features.size()) +
        " features");
  }

  torch::Tensor hidden;
  auto& d = in.getDict();
  if (d.find("hidden") != d.end()) {
    if (!d["hidden"].getTensorList().empty()) {
      hidden = d["hidden"].getTensorList()[0];
    }
  }

  auto map = features[0];
  auto mapid = features[1];
  auto races = features[2];
  auto units = features[3];
  auto unitsBow = features[4];
  auto fabsUnitsBow = features[5];
  auto resources = features[6];
  auto tech = features[7];
  auto ptech = features[8];
  auto time = features[9];
  auto activeBo = features[10];

  auto trunkF = trunk_->forward(
      {mapid, races, resources, tech, ptech, time, activeBo})[0];

  auto hasTimeDim = map.dim() == 5;

  // Map features (optional)
  torch::Tensor mapF;
  if (mapConv_ != nullptr) {
    if (hasTimeDim) {
      map = map.squeeze(0);
    }
    mapF = mapConv_->forward({map})[0];
    mapF = at::avg_pool2d(mapF, {2, 2});

    if (hasTimeDim) {
      auto mapSizes = mapF.sizes().vec();
      mapSizes.insert(mapSizes.begin(), units.size(0));
      mapF = mapF.unsqueeze(0).expand(mapSizes);
    }
  }

  // Spatial unit features
  auto combined =
      mapF.defined() ? at::cat({units, mapF}, hasTimeDim ? 2 : 1) : units;
  if (hasTimeDim) {
    auto sz = combined.sizes().vec();
    sz.erase(sz.begin());
    sz.at(0) = -1;
    combined = combined.view(sz);
  }
  auto unitsF = convnet_->forward({combined})[0];
  if (hasTimeDim) {
    unitsF = unitsF.view({units.size(0), units.size(1), -1});
  } else {
    unitsF = unitsF.view({units.size(0), -1});
  }
  auto unitsE = cembed_->forward({unitsF})[0];

  ag::tensor_list min{unitsE, unitsBow, trunkF};
  if (use_fabs_) {
    min.push_back(fabsUnitsBow);
  }
  auto lstmIn = at::cat(min, trunkF.dim() - 1);
  ag::Variant x;
  if (hasTimeDim) {
    x = lstm_->forward({lstmIn, hidden});
  } else {
    x = lstm_->forward({lstmIn.unsqueeze(0), hidden});
    x[0] = x[0].squeeze(0);
  }
  auto newHidden = x[1];

  auto enemyRace = races.slice(races.dim() - 1, 1, 2).squeeze();
  masks_ = masks_.to(options().device());
  auto output = doHeads(vHeads_, x[0], enemyRace, masks_);
  output.getDict()["hidden"] = ag::tensor_list{newHidden};
  return output;
}

} // namespace bos
} // namespace cherrypi
