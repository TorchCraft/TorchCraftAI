/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <functional>

#include "common.h"
#include "flags.h"

#include "cpid/batcher.h"
#include "cpid/metrics.h"
#include "cpid/zeroordertrainer.h"
#include "upc.h"

#include <autogradpp/autograd.h>

#include <fmt/ostream.h>
#include <prettyprint/prettyprint.hpp>

#define PARAM(x, expr) x = add(expr, #x)

/**
 * Potential field (PF) and neural network (NN) components/models.
 */
namespace microbattles {

AUTOGRAD_CONTAINER_CLASS(CONV2D) {
 public:
  TORCH_ARG(int, nIn);
  TORCH_ARG(int, nHid);
  TORCH_ARG(int, nOut);
  TORCH_ARG(int, nLayers) = 1;
  TORCH_ARG(int, nKernel) = 1;
  TORCH_ARG(int, nPadding) = 0;
  TORCH_ARG(bool, zeroLastLayer);
  ag::Container seq_;

  void reset() override {
    auto seq = ag::Sequential();
    for (auto i = 0; i < nLayers_; i++) {
      bool isLastLayer = i == nLayers_ - 1;
      auto nIn = i == 0 ? nIn_ : nHid_;
      auto nOut = i == nLayers_ - 1 ? nOut_ : nHid_;
      auto conv = ag::Conv2d(nIn, nOut, nKernel_).padding(nPadding_).make();

      if (zeroLastLayer_ && isLastLayer) {
        for (auto& p : conv->parameters()) {
          p.detach().zero_();
        }
      }

      seq.append(conv);
      if (!isLastLayer) {
        seq.append(ag::Functional(torch::relu).make());
      }
    }
    PARAM(seq_, seq.make());
  }

  ag::Variant forward(ag::Variant x) override {
    return seq_->forward(x);
  }
};

template <int TSize>
struct BoundingBox {
  static_assert(TSize % 2 == 1, "Bounding box size should be odd");
  static constexpr int kSize = TSize;
  static constexpr int kPadding = kSize - 1;
  static constexpr int kOffset = kPadding / 2;
  static constexpr int kHeight = kMapHeight + kPadding;
  static constexpr int kWidth = kMapWidth + kPadding;
};

struct MicroFeaturizer {
  virtual ~MicroFeaturizer() = default;
  virtual int mapPadding() {
    return 0;
  }
  virtual int mapOffset() {
    return 0;
  }

  static int kNumUnitChannels;
  static constexpr int kMapFeatures = 9;

  virtual ag::Variant featurize(cherrypi::State* state);
};

/**
 * train_micro.cpp expects a MicroModel, and when you implement a new action,
 * it should be implemented in train_micro what to do with that action.
 **/
struct PFMicroActionModel : public virtual ag::ContainerImpl {
  virtual ~PFMicroActionModel() = default;
  struct PFMicroAction {
    enum Action { Attack, Move, None, Plague, DarkSwarm } action;
    cherrypi::Unit* unit;
    cherrypi::Unit* target_u;
    cherrypi::Position target_p;
  };

  virtual int mapPadding() {
    return 0;
  }
  virtual int mapOffset() {
    return 0;
  }

  static int kNumUnitChannels;
  static constexpr int kMapFeatures = 9;

  // State, input, output
  virtual std::vector<PFMicroAction>
  decodeOutput(cherrypi::State*, ag::Variant input, ag::Variant output) = 0;
  virtual std::shared_ptr<MicroFeaturizer> getFeaturizer() = 0;
  virtual std::unique_ptr<cpid::AsyncBatcher> createBatcher(size_t batchSize) {
    return nullptr;
  }
};

} // namespace microbattles
