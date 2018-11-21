/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "models.h"
#include "debug.h"
#include "operations.h"

namespace common {

void MLP::reset() {
  auto seq = ag::Sequential();
  for (auto i = 0; i < nLayers_; i++) {
    bool isLastLayer = i == nLayers_ - 1;
    auto nIn = i == 0 ? nIn_ : nHid_;
    auto nOut = i == nLayers_ - 1 ? nOut_ : nHid_;
    auto linear = ag::Linear(nIn, nOut).make();

    if (zeroLastLayer_ && isLastLayer) {
      for (auto& p : linear->parameters()) {
        p.detach().zero_();
      }
    }

    seq.append(linear);
    if (!isLastLayer) {
      seq.append(ag::Functional(nonlinearity_).make());
    }
  }
  seq_ = add(seq.make(), "seq_");
}

ag::Variant MLP::forward(ag::Variant x) {
  return seq_->forward(x);
}

void ConvBlock::reset() {
  if (bottleNeck_ && nLayers_ < 2) {
    throw std::runtime_error("Need at least 2 layers to make a bottleneck");
  } else if (nLayers_ < 1) {
    throw std::runtime_error("Need at least 1 layer");
  }

  auto trunk = ag::Sequential();

  int intermSize = bottleNeck_ ? nOutFeats_ / 4 : nOutFeats_;

  int currentSize = nInFeats_;
  for (int i = 0; i < nLayers_ - 1; ++i) {
    // stride and dilation only occur on first layer
    int curStride = (i == 0) ? stride_ : 1;
    int curDilation = (i == 0) ? dilation_ : 1;
    int curPadding = (curDilation * (kernelSize_ - 1) / 2);
    addLayer(
        trunk,
        ag::Conv2d(currentSize, intermSize, kernelSize_)
            .stride(curStride)
            .padding(curPadding)
            .dilation(curDilation)
            .transposed(deconv_)
            .with_bias(bias_)
            .make(),
        intermSize,
        i + 1);
    currentSize = intermSize;
  }

  int lastStride = (nLayers_ == 1) ? stride_ : 1;
  int lastDilation = (nLayers_ == 1) ? dilation_ : 1;
  int lastPadding = (lastDilation * (kernelSize_ - 1) / 2);
  ag::Conv2d curConv = ag::Conv2d(currentSize, nOutFeats_, kernelSize_)
                           .stride(lastStride)
                           .padding(lastPadding)
                           .dilation(lastDilation)
                           .transposed(deconv_)
                           .with_bias(bias_);

  if (gated_) {
    addLayer(trunk, GatedConv(curConv).make(), nOutFeats_, nLayers_);
  } else {
    addLayer(trunk, curConv.make(), nOutFeats_, nLayers_);
  }

  seq_ = add(trunk.make(), "trunk");
  if (residual_ && (stride_ != 1 || nInFeats_ != nOutFeats_)) {
    // if we want a skip connections and in/out shapes don't match, we use a 1x1
    // conv layer to reshape the input
    auto resample = ag::Sequential();
    resample.append(
        ag::Conv2d(nInFeats_, nOutFeats_, 1)
            .stride(stride_)
            .transposed(deconv_)
            .make(),
        "resampleConv");
    if (batchNorm_) {
      resample.append(
          ag::BatchNorm(nOutFeats_).stateful(true).make(), "resampleBN");
    }
    resample_ = add(resample.make(), "resample");
  }
}

void ConvBlock::addLayer(
    ag::Sequential& trunk,
    ag::Container layer,
    int nOut,
    int id) {
  trunk.append(layer, "conv" + std::to_string(id));
  if (batchNorm_) {
    trunk.append(
        ag::BatchNorm(nOut).stateful(true).make(), "bn" + std::to_string(id));
  }
  trunk.append(ag::Functional(nonlinearity_).make());
}

ag::Variant ConvBlock::forward(ag::Variant x) {
  torch::Tensor res;
  if (x.isTensorList()) {
    if (x.getTensorList().size() != 1) {
      throw std::runtime_error(
          "Malformed model input: " + std::to_string(x.getTensorList().size()) +
          " inputs");
    }
    res = x.getTensorList()[0];
  } else if (x.isTensor()) {
    res = x.get();
  } else {
    throw std::runtime_error("Forward received unsupported type");
  }
  auto out = seq_->forward(res).getTensorList()[0];
  if (residual_) {
    if (resample_) {
      res = resample_->forward({res}).getTensorList()[0];
    }
    assertSize("out", out, res.sizes());
    out = out + res;
  }
  return {out};
}

void EncoderDecoder::reset() {
  if (decodeType_ == DecodeType::Deconv && concatInput_ != ConcatType::None) {
    throw std::runtime_error(
        "Transposed convolution decoding doesn't support concatenation");
  }
  if (stride_ != 1 && decodeType_ != DecodeType::None &&
      upsampling_ == UpsamplingType::None) {
    throw std::runtime_error("Stride > 1 and concatenation require upsampling");
  }

  if (inShape_.size() != 3) {
    throw std::runtime_error("Expected input shape as [c, h, w]");
  }

  // For easy computation of the shapes of the in/out tensors at each point of
  // the net, we do a forward pass along the way.
  auto dummyInput = torch::zeros(inShape_).unsqueeze(0);

  int curSize = inShape_[0];
  std::vector<std::pair<int, int>> shapes{
      {dummyInput.size(2), dummyInput.size(3)}};

  VLOG(4) << "init shape = " << shapes.back().first << " "
          << shapes.back().second;

  for (int i = 0; i < numBlocks_; ++i) {
    int outSize = intermSize_;
    if (i == numBlocks_ - 1 && decodeType_ == DecodeType::None) {
      outSize = nOutFeats_;
    }
    int curDilation = 1;
    switch (dilationType_) {
      case DilationScheme::Linear:
        curDilation = (i + 1);
        break;
      case DilationScheme::Exponential:
        curDilation = 1 << i;
        break;
      case DilationScheme::None:
      default:
        curDilation = 1;
    }
    encodingLayers_.push_back(
        add(ConvBlock()
                .nInFeats(curSize)
                .nOutFeats(outSize)
                .nonlinearity(nonlinearity_)
                .deconv(false)
                .kernelSize(kernelSize_)
                .stride(stride_)
                .dilation(curDilation)
                .residual(residual_)
                .batchNorm(batchNorm_)
                .bottleNeck(bottleNeck_)
                .nLayers(nInnerLayers_)
                .bias(bias_)
                .gated(gated_)
                .make(),
            "encoding_" + std::to_string(i)));
    curSize = outSize;
    // Now we do the forward. We do in eval to avoid with batchnorm's running
    // stats
    encodingLayers_.back()->eval();
    dummyInput = encodingLayers_.back()->forward({dummyInput})[0];
    encodingLayers_.back()->train();
    shapes.emplace_back(dummyInput.size(2), dummyInput.size(3));
    VLOG(4) << "shape " << shapes.back().first << " " << shapes.back().second;
  }
  if (decodeType_ == DecodeType::None) {
    VLOG(4) << "skipping decode";
    return;
  } else {
    VLOG(4) << "decode happenning";
  }
  for (int i = 0; i < numBlocks_; ++i) {
    int curDilation = 1;
    switch (dilationType_) {
      case DilationScheme::Linear:
        curDilation = numBlocks_ - i;
        break;
      case DilationScheme::Exponential:
        curDilation = 1 << (numBlocks_ - i - 1);
        break;
      case DilationScheme::None:
      default:
        curDilation = 1;
    }
    int inChannels = 0;
    if (concatInput_ == ConcatType::Input ||
        (concatInput_ == ConcatType::Mirror && i == numBlocks_ - 1)) {
      inChannels = inShape_[0];
    } else if (concatInput_ == ConcatType::Mirror) {
      inChannels = intermSize_;
    }
    const int curIn = curSize + inChannels;
    auto trunkResampler = ag::Sequential();
    auto skipResampler = ag::Sequential();
    if (decodeType_ == DecodeType::Conv) {
      // We want the output of this layer to have the same shape as the input of
      // the mirror one in the encoding
      std::pair<int, int> targetShape = shapes[numBlocks_ - i - 1];
      std::pair<int, int> skipShape;
      if (concatInput_ == ConcatType::Input) {
        skipShape = shapes[0];
      } else if (
          concatInput_ == ConcatType::Mirror ||
          concatInput_ == ConcatType::None) {
        skipShape = shapes[numBlocks_ - 1 - i];
      } else {
        throw std::runtime_error("ConcatType not implemented");
      }
      std::pair<int, int> inShape = shapes[numBlocks_ - i];
      VLOG(4) << "inShape " << inShape.first << " " << inShape.second;
      VLOG(4) << "skipShape " << skipShape.first << " " << skipShape.second;
      VLOG(4) << "targetShape " << targetShape.first << " "
              << targetShape.second;
      add_resample(skipResampler, inChannels, skipShape, targetShape);
      add_resample(trunkResampler, curSize, inShape, targetShape);
    }
    trunkResampling_.push_back(
        add(trunkResampler.make(), "trunkResampler_" + std::to_string(i)));
    skipResampling_.push_back(
        add(skipResampler.make(), "skipResampler_" + std::to_string(i)));

    const int outSize = (i == numBlocks_ - 1) ? nOutFeats_ : intermSize_;
    const bool transposed = (decodeType_ == DecodeType::Deconv);
    const int curStride = (transposed) ? stride_ : 1;
    auto block = ag::Sequential();
    block.append(
        ConvBlock()
            .nInFeats(curIn)
            .nOutFeats(outSize)
            .nonlinearity(nonlinearity_)
            .deconv(transposed)
            .kernelSize(kernelSize_)
            .stride(curStride)
            .dilation(curDilation)
            .residual(residual_)
            .batchNorm(batchNorm_)
            .bottleNeck(bottleNeck_)
            .nLayers(nInnerLayers_)
            .bias(bias_)
            .make());
    if (transposed) {
      // in the case of deconvolution in decoding, we make sure that the size in
      // the output matches the size of the mirror layer
      std::pair<int, int> targetShape = shapes[numBlocks_ - i - 1];
      std::pair<int, int> inShape{dummyInput.size(2), dummyInput.size(3)};
      add_padding_if_needed(block, curIn, inShape, targetShape);

      // Now we do the forward (we only need when decoding with deconvolutions).
      // We do in eval to avoid with batchnorm's running stats
      block.eval();
      dummyInput = block.forward({dummyInput})[0];
      block.train();
    }

    decodingLayers_.push_back(
        add(block.make(), "DecodeBlock_" + std::to_string(i)));
    curSize = outSize;
  }
}

ag::Variant EncoderDecoder::forward(ag::Variant x) {
  if (!x.isTensor() && (!x.isTensorList() || x.getTensorList().size() != 1)) {
    throw std::runtime_error("EncoderDecoder: malformed model input");
  }
  std::vector<torch::Tensor> encodings{x[0]};
  auto res = x[0];
  // do the encoding
  VLOG(4) << "init size " << res.size(2) << " " << res.size(3);
  for (auto& lay : encodingLayers_) {
    res = lay->forward({res})[0];
    encodings.push_back(res);
    VLOG(4) << "encoded size " << res.size(2) << " " << res.size(3);
  }
  // do the decoding
  int i = 0;
  for (auto& lay : decodingLayers_) {
    VLOG(4) << "before resampling" << res.size(2) << " " << res.size(3);
    res = trunkResampling_[i]->forward({res})[0];
    VLOG(4) << "after resampling" << res.size(2) << " " << res.size(3);
    switch (concatInput_) {
      case ConcatType::None:
        // nothing to do
        break;
      case ConcatType::Input:
        VLOG(4) << "concat size" << encodings[0].size(2) << " "
                << encodings[0].size(3);
        res = at::cat({res, skipResampling_[i]->forward({encodings[0]})[0]}, 1);
        break;
      case ConcatType::Mirror:
        res = at::cat(
            {res,
             skipResampling_[i]->forward({encodings[numBlocks_ - i - 1]})[0]},
            1);
        break;
      default:
        throw std::runtime_error("Not implemented");
    }
    res = lay->forward({res})[0];
    ++i;
  }

  return {res, encodings.back()};
}

void EncoderDecoder::add_padding_if_needed(
    ag::Sequential& module,
    int size,
    std::pair<int, int> inShape,
    std::pair<int, int> targetShape) {
  // we do a fake pass in the module to see if the output shape is targetShape.
  // if not, we add padding.
  VLOG(4) << "constructing " << 1 << " " << size << " " << inShape.first << " "
          << inShape.second;
  auto dummyIn = torch::zeros({1, size, inShape.first, inShape.second});
  auto dummyOut = module.forward({dummyIn})[0];
  std::pair<int, int> outShape{dummyOut.size(2), dummyOut.size(3)};
  if (outShape != targetShape) {
    VLOG(4) << "shapes out" << outShape.first << " " << outShape.second
            << " target " << targetShape.first << " " << targetShape.second;
    int deltaH = targetShape.first - outShape.first;
    int deltaW = targetShape.second - outShape.second;
    VLOG(4) << "before " << deltaW / 2 << " " << deltaW - deltaW / 2 << " "
            << deltaH / 2 << " " << deltaH - deltaH / 2;
    std::vector<int64_t> pad{
        deltaW / 2, deltaW - deltaW / 2, deltaH / 2, deltaH - deltaH / 2};
    VLOG(4) << "padding " << pad[0] << " " << pad[1] << " " << pad[2] << " "
            << pad[3];
    module.append(ag::Functional(pad2d, pad).make(), "padder");
  }
  // check
  dummyOut = module.forward({dummyIn})[0];
  if (dummyOut.size(2) != targetShape.first ||
      dummyOut.size(3) != targetShape.second) {
    throw std::runtime_error("Error constructing padding");
  }
}

void EncoderDecoder::add_resample(
    ag::Sequential& module,
    int curSize,
    std::pair<int, int> inShape,
    std::pair<int, int> targetShape) const {
  if (inShape != targetShape) {
    switch (upsampling_) {
      case UpsamplingType::Bilin: {
        auto func = [targetShape](torch::Tensor in) {
          VLOG(4) << "forwarding upsample " << targetShape.first << "  "
                  << targetShape.second;
          return upsample(
              in,
              UpsampleMode::Bilinear,
              {targetShape.first, targetShape.second});
        };
        module.append(ag::Functional(func).make(), "bilin_upsample");
        break;
      }
      case UpsamplingType::Deconv: {
        bool deconv = targetShape.first >= inShape.first;
        int curStride = std::max(targetShape.first, inShape.first) /
            std::min(targetShape.first, inShape.first);
        curStride = std::max(1, curStride);
        module.append(
            ag::Conv2d(curSize, curSize, 1)
                .transposed(deconv)
                .stride(curStride)
                .make(),
            "deconv_upsample");
        break;
      }
      case UpsamplingType::None:
      default:
        throw std::runtime_error("Unsupported upsampling type");
    }
    add_padding_if_needed(module, curSize, inShape, targetShape);
  }
}
} // namespace common
