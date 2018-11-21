/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "models/bos/sample.h"

#ifdef HAVE_CPID
#include <cpid/trainer.h>
#endif // HAVE_CPID

#include <unordered_map>

#include <autogradpp/autograd.h>

namespace cherrypi {
namespace bos {

/**
 * Helper class for running BOS models.
 * Once instantiated, the runner is valid for the current game only.
 */
struct ModelRunner {
#ifdef HAVE_CPID
  using GameUID = cpid::GameUID;
#else // HAVE_CPID
  using GameUID = std::string;
#endif // HAVE_CPID

  std::shared_ptr<StaticData> staticData = nullptr;
#ifdef HAVE_CPID
  std::shared_ptr<cpid::Trainer> trainer;
#endif // HAVE_CPID
  ag::Container model;
  std::unordered_map<int64_t, std::string> indexToBo;
  std::string modelType;
  torch::Tensor boMask;

#ifdef HAVE_CPID
  ModelRunner(std::shared_ptr<cpid::Trainer> trainer);
#endif // HAVE_CPID
  ModelRunner(ag::Container model);
  virtual ~ModelRunner() = default;

  Sample takeSample(State* state) const;
  virtual ag::Variant makeInput(Sample const& sample) const;
  ag::Variant forward(Sample const& sample, GameUID const& gameId = GameUID());
  ag::Variant forward(
      ag::Variant input,
      Sample const& sample,
      GameUID const& gameId = GameUID());

  void blacklistBuildOrder(std::string buildOrder);

 protected:
  virtual ag::Variant modelForward(ag::Variant input);
#ifdef HAVE_CPID
  virtual ag::Variant trainerForward(ag::Variant input, GameUID const& gameId);
#endif // HAVE_CPID
};

#ifdef HAVE_CPID
std::unique_ptr<ModelRunner> makeModelRunner(
    std::shared_ptr<cpid::Trainer> trainer,
    std::string modelType);
#endif // HAVE_CPID
std::unique_ptr<ModelRunner> makeModelRunner(
    ag::Container model,
    std::string modelType);

} // namespace bos
} // namespace cherrypi
