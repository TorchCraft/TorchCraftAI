/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "models/defogger.h"

#include <glog/logging.h>

namespace cherrypi {
namespace defogger {

ag::Container conv2dBuilder(
    uint32_t input_size,
    uint32_t output_size,
    int convsize,
    int stride,
    int padding,
    bool no_bias) {
  return ag::Conv2d(input_size, output_size, convsize)
      .stride(stride)
      .padding(padding)
      .with_bias(!no_bias)
      .make();
}

#ifndef WITHOUT_POSIX
std::unique_ptr<cnpy::npz_t> DefoggerModel::layers;
#endif

// Ugly function with a global parameter to compare activations.
std::string DefoggerModel::prefix;
void compare(std::string name, ag::tensor_list got) {
#ifdef WITHOUT_POSIX
  throw std::runtime_error("Canont use numpy on windows");
#else // WITHOUT_POSIX
  torch::NoGradGuard guard;
  if (DefoggerModel::layers == nullptr) {
    return;
  }

  auto layers = *DefoggerModel::layers;
  for (auto i = 0U; i < got.size(); i++) {
    std::string vname = name + "_" + std::to_string(i);
    auto it = layers.find(vname);
    if (it != layers.end()) {
      auto expected_i = utils::tensorFromNpyArray(
          it->second, torch::dtype(torch::kFloat).device(torch::kCUDA));
      auto error = (got[i] - expected_i).pow(2).max();
      auto range = expected_i.abs().max();
      LOG(ERROR) << "Layer " << vname << ": error " << error << " (range "
                 << range << ")";
    } else {
      LOG(ERROR) << "Layer " << vname << ": not found!";
    }
  }
#endif
}

void MapRaceFeaturize::reset() {
  conv1_ =
      add(ag::Conv2d(4, map_embsize_, 4).stride(2).padding(1).make(), "conv1");
  conv2_ =
      add(ag::Conv2d(map_embsize_, map_embsize_, kernel_size_ / 2)
              .stride(stride_ / 2)
              .make(),
          "conv2");
  conv3_ =
      add(ag::Conv2d(map_embsize_, map_embsize_, 3).padding(1).make(), "conv3");

  embedR_ = add(ag::Embedding(3, race_embsize_).make(), "embedR");
}

ag::Variant MapRaceFeaturize::forward(ag::Variant in) {
  ag::tensor_list& input = in.getTensorList();
  if (input.size() != 3) {
    throw std::runtime_error(
        "Malformed model input: " + std::to_string(input.size()) + " inputs");
  }

  auto scmap = input[0];
  auto race = input[1];
  auto features = input[2];
  auto bsz = features.size(0);
  auto H = features.size(2);
  auto W = features.size(3);

  auto map_features = scmap;
  map_features = conv1_->forward({map_features})[0];
  compare("mrft/module0", {map_features});
  map_features = at::elu(map_features);
  compare("mrft/module1", {map_features});
  map_features = conv2_->forward({map_features})[0];
  compare("mrft/module2", {map_features});
  map_features = at::elu(map_features);
  compare("mrft/module3", {map_features});
  map_features = conv3_->forward({map_features})[0]; // 8 x H x W
  compare("mrft/module4", {map_features});

  auto race_features = race;
  race_features = embedR_->forward({race_features})[0]; // 1 x 2 x
  // R=race_embsize_

  map_features = map_features.expand({bsz, map_embsize_, H, W});
  race_features = race_features.view({1, 2 * race_embsize_, 1, 1})
                      .expand({bsz, 2 * race_embsize_, H, W});

  return {at::cat({features, map_features, race_features}, 1)};
}

void Convnet::reset() {
  if (depth_ > 0) {
    // The condition is important, so that this convnet always has depth_ + 1
    // layers;
    conv0_ =
        add(conv_(
                input_size_,
                interm_size_,
                convsize_0_,
                stride_0_,
                padding_0_,
                false),
            "conv0");
  }
  for (auto i = 1; i < depth_; i++) { // depth_-1 layers here
    convS_.push_back(add(
        conv_(interm_size_, interm_size_, convsize_, stride_, padding_, false),
        "conv" + std::to_string(i)));
  }
  conv_output_ =
      add(conv_(interm_size_, output_size_, 1, 1, 0, false), "conv_output");
}

ag::Variant Convnet::forward(ag::Variant in) {
  ag::tensor_list& input = in.getTensorList();
  if (input.size() != 1) {
    throw std::runtime_error(
        "Malformed model input: " + std::to_string(input.size()) + " inputs");
  }

  auto j = 0;
  auto x = input[0];
  auto comp = [&]() {
    if (DefoggerModel::prefix != "") {
      compare(DefoggerModel::prefix + std::to_string(j), {x});
      j++;
    }
  };
  if (depth_ > 0) {
    // Same consideration as above.
    x = conv0_->forward({x})[0];
    comp();
    x = nonlin_(x);
    comp();
  }
  for (auto i = 0U; i < convS_.size(); i++) {
    x = convS_[i]->forward({x})[0];
    comp();
    x = nonlin_(x);
    comp();
  }
  x = conv_output_->forward({x})[0];
  comp();
  return {x};
}

Convnet SimpleConvnet(
    conv_builder conv,
    nonlin_type nonlin,
    int convsize,
    int padding,
    int input_size,
    int output_size,
    int depth,
    int stride) {
  return Convnet(
             conv,
             nonlin,
             convsize,
             convsize,
             padding,
             padding,
             input_size,
             input_size,
             output_size)
      .depth(depth - 1)
      .stride_0(stride)
      .stride(stride);
}

void Decoder::reset() {
  auto padding_0 = (convsize_0_ - 1) / 2;
  auto padding = (convsize_ - 1) / 2;
  convnet_ =
      add(Convnet(
              conv_,
              nonlin_,
              convsize_0_,
              convsize_,
              padding_0,
              padding,
              input_size_,
              interm_size_,
              output_size_)
              .depth(depth_)
              .stride_0(stride_0_)
              .stride(stride_)
              .make(),
          "convnet");
}

ag::Variant Decoder::forward(ag::Variant input) {
  return convnet_->forward(input);
}

void DefoggerModel::reset() {
  // Error checks

  if (n_lvls_ <= 0) {
    throw std::runtime_error("n_lvls must be at least 1");
  }

  if (dec_convsize_ % 2 != 1) {
    throw std::runtime_error(
        "ERROR: the size of the decoder "
        "convolution is not odd");
  }

  // Dimensions and stuff

  lstm_nlayers_ = 1;

  auto rnn_input_size = enc_embsize_ + (bypass_encoder_ ? enc_embsize_ : 0);

  conv_builder convmod = conv_;

  auto midconv_padding = (midconv_kw_ - 1) / 2;

  auto nfeat = n_inp_feats_ * 2;
  auto nchannel = nfeat + race_embsize_ * 2 + map_embsize_;
  auto decoder_input_size = nchannel + hid_dim_ + enc_embsize_ * n_lvls_;

  // TODO get these dynamically instead of hardcoding the values?
  auto num_our_units_inds = 59;
  auto num_our_bldgs_inds = 58;
  auto num_nmy_bldgs_inds = 58;

  hidden_ = std::vector<torch::Tensor>(n_lvls_ + 1);

  // Containers

  // Featurizers

  if (bypass_encoder_) {
    sum_pool_embed_ =
        add(ag::Linear(nfeat, enc_embsize_).make(), "sum_pool_embed");
  }

  trunk_ =
      add(MapRaceFeaturize()
              .map_embsize(map_embsize_)
              .race_embsize(race_embsize_)
              .kernel_size(kernel_size_)
              .stride(stride_)
              .make(),
          "trunk");

  // Convolution

  conv1x1_ = add(ag::Conv2d(nchannel, inp_embsize_, 1).make(), "conv1x1");

  // Encoder: spatially-replicated LSTMs and convolutions

  for (auto i = 0; i < n_lvls_; i++) {
    auto isize = enc_embsize_;
    auto osize = enc_embsize_;
    if (i == 0) {
      isize = inp_embsize_;
    }

    midnets_.push_back(
        add(ag::Sequential()
                .append(SimpleConvnet(
                            convmod,
                            nonlin_,
                            midconv_kw_,
                            midconv_padding,
                            isize,
                            osize,
                            midconv_depth_ - 1,
                            1)
                            .make())
                .append(convmod(isize, osize, 3, 2, 1, false))
                .make(),
            "midnet" + std::to_string(i)));

    midrnns_.push_back(
        add(ag::LSTM(osize, osize).layers(1).dropout(lstm_dropout_).make(),
            "midrnn" + std::to_string(i)));
  }

  // Recurrent unit

  rnn_ =
      add(ag::LSTM(rnn_input_size, hid_dim_)
              .layers(lstm_nlayers_)
              .dropout(lstm_dropout_)
              .make(),
          "rnn");

  // Decoder

  decoder_ =
      add(Decoder(
              convmod,
              nonlin_,
              dec_convsize_,
              dec_convsize_,
              decoder_input_size,
              dec_embsize_,
              dec_embsize_)
              .depth(dec_depth_)
              .make(),
          "decoder");

  // Heads

  regression_head_ =
      add(ag::Conv2d(dec_embsize_, nfeat, 1).make(), "regression_head");
  unit_class_head_ =
      add(ag::Conv2d(dec_embsize_, 2 * num_our_units_inds, 1).make(),
          "units_class_head");
  bldg_class_head_ =
      add(ag::Conv2d(dec_embsize_, 2 * num_our_bldgs_inds, 1).make(),
          "bldgs_class_head");
  opbt_class_head_ = add(
      ag::Linear(dec_embsize_, num_nmy_bldgs_inds).make(), "opbt_class_head");
}

void DefoggerModel::repackage_hidden() {
  for (auto i = 0U; i < hidden_.size(); i++) {
    hidden_[i] = hidden_[i].detach();
  }
}

torch::Tensor DefoggerModel::encode(torch::Tensor x) {
  append_to_decoder_input_.clear();

  // prefix are used so that the activations can be compared with the proper
  // ones (ugly but temporary).
  for (auto i = 0U; i < midnets_.size(); i++) {
    prefix = "midnet" + std::to_string(i) + "/0";
    x = nonlin_(midnets_[i]->forward({x})[0]);
    compare("midnet" + std::to_string(i), {x});
    x = do_rnn_middle(x, input_sz_, i);
    compare("midrnn" + std::to_string(i), {x});
    append_to_decoder_input_.push_back(x);
  }
  return x;
}

torch::Tensor
DefoggerModel::do_rnn_middle(torch::Tensor x, at::IntList sz, int i) {
  auto xs2 = x.size(2);
  auto xs3 = x.size(3);

  auto bsz = sz[0];
  auto H = sz[2];
  auto W = sz[3];
  auto I = (i == 0 ? inp_embsize_ : enc_embsize_);

  x = x.view({bsz, I, -1}).transpose(1, 2);
  auto y = midrnns_[i]->forward({x, hidden_[i]});
  hidden_[i] = y[1];

  auto output = y[0];
  output =
      output.transpose(1, 2).contiguous().view({bsz, enc_embsize_, xs2, xs3});
  output = utils::upsample(output, upsample_, {H, W});
  return output;
}

#define POOL(fn, x) fn(fn(x, 3), 2)
torch::Tensor DefoggerModel::pooling(torch::Tensor x, std::string method) {
  if (method == "") {
    method = top_pooling_;
  }
  if (method == "mean") {
    x = POOL(at::mean, x);
  } else if (method == "max") {
    x = POOL(
        [](torch::Tensor x, int dim) { return std::get<0>(at::max(x, dim)); },
        x);
  } else if (method == "sum") {
    x = POOL(at::sum, x);
  } else {
    throw std::runtime_error("unknown pooling method: " + method);
  }
  return x;
}

ag::tensor_list DefoggerModel::trunk_encode_pool(ag::tensor_list input) {
  // scmap: 1xCxHxW features about our game map
  // race: 1x2 (my race, their race)
  // features: TxFxHxW, with feature dim F and time
  // dim T
  auto scmap = input[0];
  auto race = input[1];
  auto features = input[2];

  input_sz_ = features.sizes();

  torch::Tensor bypass;
  if (bypass_encoder_) {
    bypass = sum_pool_embed_->forward({POOL(at::sum, features)})[0];
    bypass = bypass.unsqueeze(1);
  }

  features = trunk_->forward(input)[0].contiguous();
  compare("mrft", {features});
  auto x = conv1x1_->forward({features})[0];
  compare("conv1x1", {x});
  x = encode(x);
  x = pooling(x).unsqueeze(1);

  if (bypass_encoder_) {
    x = at::cat({x, bypass}, 2);
  }

  return {features, x};
}

torch::Tensor DefoggerModel::do_rnn(
    torch::Tensor x,
    at::IntList size,
    torch::Tensor& hidden) {
  auto bsz = size[0];
  auto H = size[2];
  auto W = size[3];

  auto y = rnn_->forward({x, hidden});
  auto output = y[0];
  hidden = y[1];

  auto featsize = hid_dim_;

  output = output.transpose(1, 2).unsqueeze(3).expand({bsz, featsize, H, W});
  return output;
}

ag::tensor_list DefoggerModel::do_heads(torch::Tensor x) {
  auto reg = regression_head_->forward({x})[0];
  compare("reg", {reg});
  auto uni = unit_class_head_->forward({x})[0];
  compare("uni", {uni});
  auto bui = bldg_class_head_->forward({x})[0];
  compare("bui", {bui});

  auto opbt = opbt_class_head_->forward({pooling(x, "max")})[0];
  if (!predict_delta_) {
    reg = at::relu(reg); // TODO inplace?
  }
  compare("opbt", {opbt});
  return {reg, uni, bui, opbt};
}

ag::tensor_list DefoggerModel::forward_rest(ag::tensor_list input) {
  auto input_0 = input[0];
  auto embed = input[1];

  auto rnn_output = do_rnn(embed, input_sz_, hidden_[hidden_.size() - 1]);
  compare("rnn", {rnn_output});

  std::vector<torch::Tensor> to_concat = {input_0, rnn_output};

  to_concat.insert(
      to_concat.end(),
      append_to_decoder_input_.begin(),
      append_to_decoder_input_.end());
  auto decoder_input = at::cat(to_concat, 1);
  prefix = "";
  auto decoder_output = decoder_->forward({decoder_input})[0];
  compare("decoder", {decoder_output});
  return do_heads(decoder_output);
}

ag::Variant DefoggerModel::forward(ag::Variant in) {
  ag::tensor_list& input = in.getTensorList();
  if (input.size() != 3) {
    throw std::runtime_error(
        "Malformed model input: " + std::to_string(input.size()) + " inputs");
  }

  if (hidden_.size() > 0 && hidden_[0].defined() &&
      hidden_[0].options().device_index() != options().device_index()) {
    for (auto& p : hidden_) {
      p = p.to(options().device());
    }
  }

  input = trunk_encode_pool(input);
  compare("tec", input);
  return forward_rest(input);
}

// Loads parameters from the pytorch model.  This relies on the fact that
// although they don't necessary have the same names the alphabetical order is
// the same.
void DefoggerModel::load_parameters(std::string const& path_to_npz) {
#ifdef WITHOUT_POSIX
  throw std::runtime_error("Canont use numpy on windows");
#else // WITHOUT_POSIX
  auto npz = cnpy::npz_load(path_to_npz);
  auto params = named_parameters();
  std::map<std::string, torch::Tensor> orderedParams;
  for (auto& p : params) {
    orderedParams[p.key()] = p.value();
  }

  if (params.size() != npz.size()) {
    std::stringstream ss;
    auto params_it = orderedParams.begin();
    auto npz_it = npz.begin();
    ss << fmt::format("{:<39} {:<39}\n", "npz", "c++");
    while (params_it != orderedParams.end() || npz_it != npz.end()) {
      ss << fmt::format(
          "{:<39} {:<39}\n",
          npz_it == npz.end() ? "" : npz_it->first,
          params_it == orderedParams.end() ? "" : params_it->first);
      if (params_it != orderedParams.end()) {
        params_it++;
      }
      if (npz_it != npz.end()) {
        npz_it++;
      }
    }

    throw std::runtime_error(fmt::format(
        "Different number of parameters: {} != {}\n{}",
        params.size(),
        npz.size(),
        ss.str()));
  }

  auto params_it = orderedParams.begin();
  for (auto& x : npz) {
    auto& old_name = params_it->first;
    auto& old_param = params_it->second;
    auto new_name = x.first;
    auto new_param = utils::tensorFromNpyArray(x.second, old_param.options());

    LOG(ERROR) << "About to replace " << old_name << " with " << new_name;
    if (!new_param.sizes().equals(old_param.sizes())) {
      LOG(ERROR) << new_param.sizes() << " != " << old_param.sizes();
      throw std::runtime_error("Inconsistent parameters sizes");
    }

    old_param.detach().copy_(new_param);
    params_it++;
  }
#endif
}

void DefoggerModel::zero_hidden() {
  hidden_ = std::vector<torch::Tensor>(n_lvls_ + 1);
}

} // namespace defogger
} // namespace cherrypi
