/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <autogradpp/autograd.h>

/*
 * Useful helpers for neural networks expressed with Torch.
 */
namespace common {

/**
 * Simple MLP of nLayers layers, with hidden size all being the same.
 * Optionally, we can zero the last layer, which is useful if the output is
 * suppose to be a probability distribution since values will be uniform after
 * softmax
 */
AUTOGRAD_CONTAINER_CLASS(MLP) {
 public:
  TORCH_ARG(int, nIn);
  TORCH_ARG(int, nHid);
  TORCH_ARG(int, nOut);
  TORCH_ARG(int, nLayers) = 1;
  TORCH_ARG(bool, zeroLastLayer);
  // Type of relu is (at::Tensor (&)(const at::Tensor&))
  TORCH_ARG(std::function<decltype(torch::relu)>, nonlinearity) = torch::relu;
  ag::Container seq_;

  void reset() override;
  ag::Variant forward(ag::Variant x) override;
};

AUTOGRAD_CONTAINER_CLASS(GatedConv) {
  ag::Container conv_;
  ag::Conv2d convBase_;
  GatedConv(ag::Conv2d conv) : convBase_(conv) {
    convBase_.output_channels(convBase_.output_channels_ * 2);
  }
  void reset() override {
    conv_ = add(convBase_.make(), "conv_");
  }
  ag::Variant forward(ag::Variant inp) override {
    auto chunked = conv_->forward(inp)[0].chunk(2, 1);
    return {chunked.at(0) * chunked.at(1).sigmoid_()};
  }
};

/**
 * Simple convolutional block, with optional residual connection
 * From a user perspective, the convolution parameters behave the same as if the
 * block was a single conv layer. For example if the stride is 2, the output
 * will be twice smaller than the input, irrespective of the number of inner
 * layers. In practice the stride and dilation are only applied to the first
 * layer.
 * The block also applies padding to compensate for the kernel size and
 * the dilation. That means that if the input is size hxw, the output will be
 * h'xw' with h' = (h - 1)/stride + 1 and w' = (w-1)/stride + 1
 */
AUTOGRAD_CONTAINER_CLASS(ConvBlock) {
 public:
  /// Number of feature channels in the input
  TORCH_ARG(int, nInFeats);
  /// Number of feature channels in the output
  TORCH_ARG(int, nOutFeats);
  /// Non linearity inserted between each convolution
  // Type of relu is (at::Tensor (&)(const at::Tensor&))
  TORCH_ARG(std::function<decltype(torch::relu)>, nonlinearity) = torch::relu;
  /// If true, the module performs transposed convolutions instead
  TORCH_ARG(bool, deconv) = false;
  /// Size of the convolution kernels (we use kernelSize X kernelSize)
  TORCH_ARG(int, kernelSize) = 3;
  /// Stride of the convolutions
  TORCH_ARG(int, stride) = 1;
  /// Dilation of the convolutions
  TORCH_ARG(int, dilation) = 1;
  /// Add a residual convolution when true
  TORCH_ARG(bool, residual) = true;
  /// Add a batchNorm layers where appropriate, if true
  TORCH_ARG(bool, batchNorm) = true;
  /// If true, the intermediate convolutions will have 4 times less features
  /// than the output
  TORCH_ARG(bool, bottleNeck) = false;
  /// Number of convolution layers
  TORCH_ARG(int, nLayers) = 2;
  /// Bias in the convolutions
  TORCH_ARG(bool, bias) = false;
  /// Whether to use gated convolutions
  TORCH_ARG(bool, gated) = false;

  void reset() override;

  ag::Variant forward(ag::Variant x) override;

  ag::Container seq_, resample_;

 protected:
  void addLayer(ag::Sequential & trunk, ag::Container layer, int nOut, int id);
};

enum class ConcatType {
  None, /// No concatenation
  Input, /// Always concatenate input
  Mirror /// Concatenate input of mirror layer
};
enum class UpsamplingType {
  None, /// No upsampling
  Bilin, /// Bilinear upsampling (fixed)
  Deconv /// Learnt upsampling (transposed convolution)
};
enum class DecodeType {
  None, /// No decoding
  Conv, /// Decode with convolutions
  Deconv /// Decode with transposed convolutions
};
enum class DilationScheme {
  None, /// No dilation
  Linear, /// The dilation increases linearly at each layer
  Exponential /// The dilation increases exponentially
};
AUTOGRAD_CONTAINER_CLASS(EncoderDecoder) {
 public:
  /// Shape of the input, given as [c,h,w], where c is the number of channels, h
  /// is the height and w the width
  TORCH_ARG(at::IntList, inShape);
  /// Number of feature channels in the intermediate layers
  TORCH_ARG(int, intermSize);
  /// Number of feature channels in the output
  TORCH_ARG(int, nOutFeats);
  /// Non linearity inserted between each convolution
  // Type of relu is (at::Tensor (&)(const at::Tensor&))
  TORCH_ARG(std::function<decltype(torch::relu)>, nonlinearity) = torch::relu;
  /// Strategy for concatening previous layers during decoding
  TORCH_ARG(ConcatType, concatInput) = ConcatType::None;
  /// Strategy for upsampling, when needed
  TORCH_ARG(UpsamplingType, upsampling) = UpsamplingType::None;
  /// Strategy for decoding
  TORCH_ARG(DecodeType, decodeType) = DecodeType::None;
  /// Strategy for dilation
  TORCH_ARG(DilationScheme, dilationType) = DilationScheme::None;
  /// Size of the convolution kernels (we use kernelSize X kernelSize)
  TORCH_ARG(int, kernelSize) = 3;
  /// Stride of the convolutions
  TORCH_ARG(int, stride) = 1;
  /// Add a residual convolution when true
  TORCH_ARG(bool, residual) = true;
  /// Add a batchNorm layers where appropriate, if true
  TORCH_ARG(bool, batchNorm) = true;
  /// If true, the intermediate convolutions will have 4 times less features
  /// than the output
  TORCH_ARG(bool, bottleNeck) = false;
  /// Number of Convolutional blocks in the encoding (if there is decoding, it
  /// will contain the same amount of blocks)
  TORCH_ARG(int, numBlocks) = 2;
  /// Number of convolution layers in each block
  TORCH_ARG(int, nInnerLayers) = 2;
  /// Bias in the convolutions
  TORCH_ARG(bool, bias) = false;
  /// Whether to use gated convolutions
  TORCH_ARG(bool, gated) = false;

  void reset() override;

  ag::Variant forward(ag::Variant x) override;

  std::vector<ag::Container> encodingLayers_, decodingLayers_;
  std::vector<ag::Container> trunkResampling_, skipResampling_;

 protected:
  static void add_padding_if_needed(
      ag::Sequential & module,
      int size,
      std::pair<int, int> inShape,
      std::pair<int, int> targetShape);

  void add_resample(
      ag::Sequential & module,
      int size,
      std::pair<int, int> inShape,
      std::pair<int, int> targetShape) const;
};

} // namespace common
