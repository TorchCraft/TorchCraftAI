/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "cherrypi.h"

#include "utils.h"
#include <autogradpp/autograd.h>

// TODO use torch::Sequential to simplify the code where possible once we don't
// need layer-by-layer comparisons.
// TODO do nonlins in place (another advantage of making them into modules)

namespace cherrypi {
namespace defogger {

using nonlin_type = std::function<torch::Tensor(torch::Tensor)>;

// Type of a function which creates a container (morally, some kind of
// convolution), whose parameters are input_size, output_size, kernel_size,
// stride, padding and no_bias.
using conv_builder =
    std::function<ag::Container(uint32_t, uint32_t, int, int, int, bool)>;

// Simple wrapper for ag::Conv2d.
ag::Container conv2dBuilder(
    uint32_t input_size,
    uint32_t output_size,
    int convsize,
    int stride,
    int padding,
    bool no_bias);

AUTOGRAD_CONTAINER_CLASS(MapRaceFeaturize) {
  // This puts the StarCraft map at the same pooling (kernel_size and stride)
  // as the features coming from the featurizer, and concatenates with inputs.
 public:
  TORCH_ARG(int, map_embsize) = 64;
  TORCH_ARG(int, race_embsize) = 8;
  TORCH_ARG(int, kernel_size) = 128;
  TORCH_ARG(int, stride) = 32;

  void reset() override;

  ag::Variant forward(ag::Variant input) override;

 protected:
  ag::Container conv1_;
  ag::Container conv2_;
  ag::Container conv3_;
  ag::Container embedR_;
};

AUTOGRAD_CONTAINER_CLASS(Convnet) {
 public:
  Convnet(
      conv_builder conv,
      nonlin_type nonlin,
      int convsize_0,
      int convsize,
      int padding_0,
      int padding,
      int input_size,
      int interm_size,
      int output_size)
      : conv_(conv),
        nonlin_(nonlin),
        convsize_0_(convsize_0),
        convsize_(convsize),
        padding_0_(padding),
        padding_(padding),
        input_size_(input_size),
        interm_size_(interm_size),
        output_size_(output_size){};

  TORCH_ARG(int, depth) = 2;
  TORCH_ARG(int, stride_0) = 1;
  TORCH_ARG(int, stride) = 1;

  void reset() override;

  ag::Variant forward(ag::Variant input) override;

 protected:
  ag::Container conv0_;
  std::vector<ag::Container> convS_;
  ag::Container conv_output_;

  conv_builder conv_;
  nonlin_type nonlin_;
  int convsize_0_;
  int convsize_;
  int padding_0_;
  int padding_;
  int input_size_;
  int interm_size_;
  int output_size_;
};

// Simply a wrapper over Convnet with some defaults.
Convnet SimpleConvnet(
    conv_builder conv,
    nonlin_type nonlin,
    int convsize,
    int padding,
    int input_size,
    int output_size,
    int depth = 2,
    int stride = 1);

AUTOGRAD_CONTAINER_CLASS(Decoder) {
 public:
  Decoder(
      conv_builder conv,
      nonlin_type nonlin,
      int convsize_0,
      int convsize,
      int input_size,
      int interm_size,
      int output_size)
      : conv_(conv),
        nonlin_(nonlin),
        convsize_0_(convsize_0),
        convsize_(convsize),
        input_size_(input_size),
        interm_size_(interm_size),
        output_size_(output_size){};

  TORCH_ARG(int, depth) = 2;
  TORCH_ARG(int, stride_0) = 1;
  TORCH_ARG(int, stride) = 1;

  void reset() override;

  ag::Variant forward(ag::Variant input) override;

 protected:
  ag::Container convnet_;

  conv_builder conv_;
  nonlin_type nonlin_;
  int convsize_0_;
  int convsize_;
  int input_size_;
  int interm_size_;
  int output_size_;
};

AUTOGRAD_CONTAINER_CLASS(DefoggerModel) {
  // Multi level LSTM model from starcraft_defogger.
 public:
  DefoggerModel(
      conv_builder conv,
      nonlin_type nonlin,
      int kernel_size,
      int n_inp_feats,
      int stride)
      : conv_(conv),
        nonlin_(nonlin),
        kernel_size_(kernel_size),
        n_inp_feats_(n_inp_feats),
        stride_(stride){};

  // lstm kwargs
  TORCH_ARG(int, map_embsize) = 64;
  TORCH_ARG(int, race_embsize) = 8;
  TORCH_ARG(int, dec_convsize) = 3;
  TORCH_ARG(int, dec_depth) = 3;
  TORCH_ARG(int, dec_embsize) = 128;
  TORCH_ARG(int, hid_dim) = 256;
  TORCH_ARG(float, lstm_dropout) = 0;

  // simple kwargs
  TORCH_ARG(bool, bypass_encoder) = false;
  TORCH_ARG(int, enc_convsize) = 3;
  TORCH_ARG(int, enc_embsize) = 256;
  TORCH_ARG(int, enc_depth) = 3;
  TORCH_ARG(int, inp_embsize) = 256;
  TORCH_ARG(std::string, top_pooling) = "mean"; // TODO change to enum?

  TORCH_ARG(bool, predict_delta) = false;

  // multilvl_lstm kwargs
  TORCH_ARG(int, midconv_kw) = 3;
  TORCH_ARG(int, midconv_stride) = 2;
  TORCH_ARG(int, midconv_depth) = 2;
  TORCH_ARG(int, n_lvls) = 2;
  TORCH_ARG(utils::UpsampleMode, upsample) = utils::UpsampleMode::Bilinear;
  TORCH_ARG(std::string, model_name) = "multilvl_lstm";

  void reset() override;

  // Reset the hidden state (to call before each game)
  void zero_hidden();

  ag::Variant forward(ag::Variant input) override;

  // Load all parameters from the python ones.
  void load_parameters(std::string const& path_to_npz);

 protected:
  void repackage_hidden();

  torch::Tensor encode(torch::Tensor x);
  torch::Tensor do_rnn_middle(torch::Tensor x, at::IntList sz, int i);
  torch::Tensor pooling(torch::Tensor x, std::string method = "");
  ag::tensor_list trunk_encode_pool(ag::tensor_list input);
  torch::Tensor do_rnn(
      torch::Tensor x, at::IntList size, torch::Tensor & hidden);
  ag::tensor_list do_heads(torch::Tensor x);
  ag::tensor_list forward_rest(ag::tensor_list input);

  conv_builder conv_;
  nonlin_type nonlin_;

  ag::Container trunk_; // Map/race featurizer
  ag::Container sum_pool_embed_;
  ag::Container conv1x1_;
  std::vector<ag::Container> midnets_;
  std::vector<ag::Container> midrnns_;
  ag::Container rnn_;
  ag::Container decoder_;
  ag::Container regression_head_;
  ag::Container unit_class_head_;
  ag::Container bldg_class_head_;
  ag::Container opbt_class_head_;

  ag::tensor_list append_to_decoder_input_;
  std::vector<torch::Tensor> hidden_;

  at::IntList input_sz_;
  int lstm_nlayers_;
  int kernel_size_;
  int n_inp_feats_;
  int stride_;

 public:
// Global variable holding the activations of the python model for easy
// comparisons everywhere in the code. Ugly but only temporary (and avoids
// modifying all function signatures).
#ifndef WITHOUT_POSIX
  static std::unique_ptr<cnpy::npz_t> layers;
#endif
  // Global variable used by external containers when comparing activations.
  static std::string prefix;
};

} // namespace defogger
} // namespace cherrypi
