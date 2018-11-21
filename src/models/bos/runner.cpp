/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "runner.h"

#include "common/autograd.h"

#ifdef HAVE_CPID
using namespace cpid;
#endif // HAVE_CPID

namespace cherrypi {
namespace bos {

namespace {

std::unordered_map<int64_t, std::string> boIndex() {
  std::unordered_map<int64_t, std::string> index;
  auto const& boMap = buildOrderMap();
  for (auto const& it : boMap) {
    index[it.second] = it.first;
  }
  return index;
}

struct FfwdModelRunner : ModelRunner {
  using ModelRunner::ModelRunner;

  ag::Variant makeInput(Sample const& sample) const override {
    auto features = sample.featurize(
        {BosFeature::BagOfUnitCounts,
         BosFeature::BagOfUnitCountsAbs5_15_30,
         BosFeature::MapId,
         BosFeature::Race,
         BosFeature::Resources5Log,
         BosFeature::TechUpgradeBits,
         BosFeature::PendingTechUpgradeBits,
         BosFeature::TimeAsFrame,
         BosFeature::ActiveBo});
    return ag::VariantDict{{"features", features}};
  }

  ag::Variant modelForward(ag::Variant input) override {
    torch::NoGradGuard ng;
    input.getDict()["features"] = common::applyTransform(
        input.getDict()["features"], [&](torch::Tensor x) {
          // Model expects extra batch dimension
          return x.to(model->options().device()).unsqueeze(0);
        });
    return model->forward(input);
  }

#ifdef HAVE_CPID
  ag::Variant trainerForward(ag::Variant input, GameUID const& gameId)
      override {
    torch::NoGradGuard ng;
    input.getDict()["features"] = common::applyTransform(
        input.getDict()["features"], [&](torch::Tensor x) {
          // Model expects extra batch dimension
          return x.to(trainer->model()->options().device()).unsqueeze(0);
        });
    return trainer->forward(input, gameId);
  }
#endif // HAVE_CPID
};

struct RecurrentModelRunner : ModelRunner {
  using ModelRunner::ModelRunner;

  ag::tensor_list hidden;

  virtual ag::Variant makeInput(Sample const& sample) const override {
    auto features = [&]() -> ag::tensor_list {
      if (modelType == "mclstm") {
        return sample.featurize(
            {BosFeature::Map,
             BosFeature::Race,
             BosFeature::Units,
             BosFeature::Resources5Log,
             BosFeature::TechUpgradeBits,
             BosFeature::PendingTechUpgradeBits,
             BosFeature::TimeAsFrame,
             BosFeature::ActiveBo});
      } else if (modelType == "celstm") {
        return sample.featurize(
            {BosFeature::Map,
             BosFeature::MapId,
             BosFeature::Race,
             BosFeature::Units,
             BosFeature::BagOfUnitCounts,
             BosFeature::BagOfUnitCountsAbs5_15_30,
             BosFeature::Resources5Log,
             BosFeature::TechUpgradeBits,
             BosFeature::PendingTechUpgradeBits,
             BosFeature::TimeAsFrame,
             BosFeature::ActiveBo});
      } else {
        return sample.featurize(
            {BosFeature::BagOfUnitCounts,
             BosFeature::BagOfUnitCountsAbs5_15_30,
             BosFeature::MapId,
             BosFeature::Race,
             BosFeature::Resources5Log,
             BosFeature::TechUpgradeBits,
             BosFeature::PendingTechUpgradeBits,
             BosFeature::TimeAsFrame,
             BosFeature::ActiveBo});
      }
    }();

    return ag::VariantDict{{"features", features}, {"hidden", hidden}};
  }

  ag::Variant modelForward(ag::Variant input) override {
    torch::NoGradGuard ng;
    input.getDict()["features"] = common::applyTransform(
        input.getDict()["features"], [&](torch::Tensor x) {
          // Model expects extra batch dimension
          return x.to(model->options().device()).unsqueeze(0);
        });
    auto output = model->forward(input);
    hidden = output.getDict()["hidden"].getTensorList();
    return output;
  }

#ifdef HAVE_CPID
  ag::Variant trainerForward(ag::Variant input, GameUID const& gameId)
      override {
    torch::NoGradGuard ng;
    input.getDict()["features"] = common::applyTransform(
        input.getDict()["features"], [&](torch::Tensor x) {
          // Model expects extra batch dimension
          return x.to(trainer->model()->options().device()).unsqueeze(0);
        });
    auto output = trainer->forward(input, gameId);
    hidden = output.getDict()["hidden"].getTensorList();
    return output;
  }
#endif // HAVE_CPID
};

} // namespace

#ifdef HAVE_CPID
ModelRunner::ModelRunner(std::shared_ptr<Trainer> trainer)
    : trainer(trainer), indexToBo(boIndex()) {
  boMask = torch::ones({int64_t(indexToBo.size())});
}
#endif // HAVE_CPID

ModelRunner::ModelRunner(ag::Container model)
    : model(model), indexToBo(boIndex()) {
  boMask = torch::ones({int64_t(indexToBo.size())});
}

Sample ModelRunner::takeSample(State* state) const {
  auto sample = Sample(state, 32, 32, staticData);
  std::const_pointer_cast<StaticData>(staticData) = sample.staticData;
  return sample;
}

ag::Variant ModelRunner::makeInput(Sample const&) const {
  return {};
}

ag::Variant ModelRunner::forward(Sample const& sample, GameUID const& gameId) {
  return forward(makeInput(sample), sample, gameId);
}

ag::Variant ModelRunner::forward(
    ag::Variant input,
    Sample const& sample,
    GameUID const& gameId) {
  ag::Variant output;
#ifdef HAVE_CPID
  if (trainer) {
    output = trainerForward(input, gameId);
    boMask = boMask.to(trainer->model()->options().device());
    output["vHeads"] = output["vHeads"] * boMask;
    output = trainer->sample(output);
  } else if (model) {
    output = modelForward(input);
    boMask = boMask.to(model->options().device());
    output["vHeads"] = output["vHeads"] * boMask;
    output.getDict()["action"] =
        ag::Variant(std::get<1>(output["vHeads"].max(1)));
  }
#else // HAVE_CPID
  (void)gameId;
  output = modelForward(input);
  boMask = boMask.to(model->options().device());
  output["vHeads"] = output["vHeads"] * boMask;
  output.getDict()["action"] =
      ag::Variant(std::get<1>(output["vHeads"].max(1)));
#endif // HAVE_CPID

  // Post-processing: add index and name of sampled build as well as
  // advantage over current build.
  auto curBuild = sample.buildOrder;
  int cid = buildOrderId(curBuild);
  auto aid = output["action"].item<int32_t>();
  output.getDict()["build"] = stripRacePrefix(indexToBo[aid]);
  output.getDict()["pwin"] = output["vHeads"].squeeze()[aid].to(torch::kCPU);
  output.getDict()["advantage"] =
      (output["vHeads"].squeeze()[aid] - output["vHeads"].squeeze()[cid])
          .to(torch::kCPU);
  return output;
}

ag::Variant ModelRunner::modelForward(ag::Variant) {
  return {};
}

#ifdef HAVE_CPID
ag::Variant ModelRunner::trainerForward(ag::Variant, GameUID const&) {
  return {};
}
#endif // HAVE_CPID

void ModelRunner::blacklistBuildOrder(std::string buildOrder) {
  auto const& map = buildOrderMap();
  auto it = map.find(buildOrder);
  if (it == map.end()) {
    throw std::runtime_error("Unknown build: " + buildOrder);
  }
  boMask[it->second] = 0.0f;
}

#ifdef HAVE_CPID
std::unique_ptr<ModelRunner> makeModelRunner(
    std::shared_ptr<cpid::Trainer> trainer,
    std::string modelType) {
  std::unique_ptr<ModelRunner> r;
  if (modelType == "idle") {
    r = std::make_unique<ModelRunner>(trainer);
  } else if (modelType == "linear" || modelType == "mlp") {
    r = std::make_unique<FfwdModelRunner>(trainer);
  } else if (modelType == "lstm" || modelType == "celstm") {
    r = std::make_unique<RecurrentModelRunner>(trainer);
  } else {
    throw std::runtime_error("Unsupported model type: " + modelType);
  }
  r->modelType = std::move(modelType);
  return r;
}
#endif // HAVE_CPID

std::unique_ptr<ModelRunner> makeModelRunner(
    ag::Container model,
    std::string modelType) {
  std::unique_ptr<ModelRunner> r;
  if (modelType == "idle") {
    r = std::make_unique<ModelRunner>(model);
  } else if (modelType == "linear" || modelType == "mlp") {
    r = std::make_unique<FfwdModelRunner>(model);
  } else if (
      modelType == "lstm" || modelType == "mclstm" || modelType == "celstm") {
    r = std::make_unique<RecurrentModelRunner>(model);
  } else {
    throw std::runtime_error("Unsupported model type: " + modelType);
  }
  r->modelType = std::move(modelType);
  return r;
}

} // namespace bos
} // namespace cherrypi
