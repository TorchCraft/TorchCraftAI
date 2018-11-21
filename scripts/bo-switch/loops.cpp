/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "loops.h"

#include "cherrypi.h"
#include "common/autograd.h"
#include "common/rand.h"
#include "fsutils.h"
#include "utils.h"
#include "zstdstream.h"

#include <cpid/distributed.h>

#include <fmt/format.h>
#include <glog/logging.h>
#include <nlohmann/json.hpp>
#include <prettyprint/prettyprint.hpp>

DEFINE_int32(plot_every, 250, "Plot model output every n updates");

namespace dist = cpid::distributed;
auto const vsopts = &visdom::makeOpts;
using namespace cherrypi::bos;

namespace cherrypi {

namespace {
constexpr auto kMaxGameIdLength = 24;
constexpr auto kProbExtraDecisionPoints = 0.01;

at::Tensor sumErrors(
    torch::Tensor output,
    torch::Tensor target,
    torch::Tensor mask = torch::Tensor()) {
  if (mask.defined()) {
    auto binaryMask = mask.ge(0.5f);
    auto predWon = output.masked_select(binaryMask).ge(0.5f);
    auto targetWon = target.masked_select(binaryMask).ge(0.5f);
    return predWon.ne(targetWon).to(torch::kFloat).sum();
  } else {
    auto predWon = output.view({-1}).ge(0.5f);
    auto targetWon = target.view({-1}).ge(0.5f);
    return predWon.ne(targetWon).to(torch::kFloat).sum();
  }
};

} // namespace

UpdateLoop::UpdateLoop(int batchSize, std::shared_ptr<visdom::Visdom> vs)
    : batchSize(batchSize), vs(vs) {
  // Map model heads to build orders
  if (indexToBo_.empty()) {
    auto const& boMap = bos::buildOrderMap();
    boNames_.resize(boMap.size());
    for (auto const& it : boMap) {
      indexToBo_[it.second] = it.first;
      boNames_[it.second] = it.first;
    }
  }
}

UpdateLoop::~UpdateLoop() {}

void UpdateLoop::flush() {
  if (!episodes.empty()) {
    preprocC_->enqueue(std::move(episodes));
    episodes = {};
  }
}

void UpdateLoop::wait() {
  preprocC_->wait();
  updateC_->wait();
  postWait();
}

void UpdateLoop::operator()(EpisodeSamples episode) {
  if (preprocC_ == nullptr) {
    preprocC_ = std::make_unique<decltype(preprocC_)::element_type>(
        8, [&](std::vector<EpisodeSamples>&& samples) {
          dist::setGPUToLocalRank();
          auto start = hires_clock::now();
          auto result = preproc(samples);
          auto duration = hires_clock::now() - start;
          auto ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(duration)
                  .count();
          VLOG_ALL(1) << "Preprocessing done in " << ms << "ms";
          updateC_->enqueue(std::move(result));
        });
  }

  if (updateC_ == nullptr) {
    updateC_ = std::make_unique<decltype(updateC_)::element_type>(
        2, [&](std::pair<ag::tensor_list, ag::tensor_list>&& d) {
          dist::setGPUToLocalRank();
          ag::tensor_list inputs = d.first;
          ag::tensor_list targets = d.second;
          auto start = hires_clock::now();
          if (train_) {
            update(inputs, targets);
            if (dist::globalContext()->rank == 0) {
              trainer->checkpoint();
            }
            numBatches++;
            if (numBatches % 10 == 0) {
              trainer->metricsContext()->dumpJson(
                  std::to_string(dist::globalContext()->rank) +
                  "_metrics.json");
            }
            if (saveModelInterval > 0 &&
                (numBatches % saveModelInterval == 0) &&
                dist::globalContext()->rank == 0) {
              ag::save(
                  fmt::format("model_u{:05d}.bin", numBatches),
                  trainer->model());
            }
          } else {
            torch::NoGradGuard ng;
            update(inputs, targets);
          }

          auto duration = hires_clock::now() - start;
          auto ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(duration)
                  .count();
          if (train_) {
            VLOG_ALL(1) << "Batch " << numBatches << " done in " << ms << "ms";
          }
        });
  }

  episodes.emplace_back(std::move(episode));
  if (episodes.size() == size_t(batchSize)) {
    preprocC_->enqueue(std::move(episodes));
    episodes = {};
  }
}

void UpdateLoop::allreduceGradients(bool hasGrads) {
  static float nWithGrads;
  for (auto& var : model->parameters()) {
    if (!var.grad().defined()) {
      var.grad() = torch::zeros_like(var, var.options());
    }
    dist::allreduce(var.grad());
    nWithGrads = hasGrads ? 1.0f : 0.0f;;
    dist::allreduce(&nWithGrads, 1);
    if (nWithGrads > 0) {
      var.grad().div_(nWithGrads);
    }
  }
}
void UpdateLoop::updatePlot(
    std::string const& title,
    std::string const& ytitle,
    std::vector<float> values,
    std::vector<std::string> legend) {
  if (vs == nullptr) {
    return;
  }

  vsWindows_[title] = vs->line(
      torch::from_blob(values.data(), {int64_t(values.size())})
          .view({1, int64_t(values.size())}),
      torch::tensor(float(numBatches)),
      vsWindows_[title],
      vsopts(
          {{"title", title},
           {"xtitle", "Batches"},
           {"ytitle", ytitle},
           {"legend", legend}}),
      vsWindows_[title].empty() ? visdom::UpdateMethod::None
                                : visdom::UpdateMethod::Append);
}

void UpdateLoop::postWait() {
  if (train_) {
    return;
  }

  auto metrics = trainer->metricsContext();

  // errors and value heads per opening/build and time step
  auto toFloatVec = [](auto v) {
    std::vector<float> fv(v.size());
    for (auto i = 0U; i < v.size(); i++) {
      fv[i] = v[i];
    }
    return fv;
  };
  for (auto const& it : vcounters_) {
    for (auto const& bit : indexToBo_) {
      auto boIdx = bit.first;
      auto const& boName = bit.second;
      metrics->pushEvents(
          fmt::format("{}/{}", it.first, boName), toFloatVec(it.second[boIdx]));
    }
  }

  if (vs != nullptr) {
    // Two versions: opening and current build
    std::vector<int> label[2];
    std::vector<float> idx[2];
    std::vector<float> valV[2];
    std::vector<float> errV[2];
    std::vector<float> nV[2];
    for (auto const& bit : indexToBo_) {
      auto boIdx = bit.first;
      auto nseen0 = vcounters_["open_nsamples"][boIdx];
      auto nseen1 = vcounters_["curb_nsamples"][boIdx];
      for (int i = 0U; i < validMaxLen_; i++) {
        if (nseen0[i] > 0) {
          label[0].push_back(boIdx + 1);
          idx[0].push_back(i);
          valV[0].push_back(vcounters_["open_value_mean"][boIdx][i]);
          errV[0].push_back(vcounters_["open_error_mean"][boIdx][i]);
          nV[0].push_back(nseen0[i]);
        }
        if (nseen1[i] > 0) {
          label[1].push_back(boIdx + 1);
          idx[1].push_back(i);
          valV[1].push_back(vcounters_["curb_value_mean"][boIdx][i]);
          errV[1].push_back(vcounters_["curb_error_mean"][boIdx][i]);
          nV[1].push_back(nseen1[i]);
        }
      }
    }

    auto doPlot = [&](
        std::string const& title, auto& data, int i, bool fixRange = true) {
      vs->scatter(
          at::stack(
              {
                  torch::from_blob(idx[i].data(), {int64_t(idx[i].size())}),
                  torch::from_blob(data[i].data(), {int64_t(data[i].size())}),
              },
              1),
          torch::from_blob(
              label[i].data(), {int64_t(label[i].size())}, torch::kI32),
          vsopts(
              {{"title", fmt::format("Valid@{} {}", numBatches, title)},
               {"legend", boNames_},
               {"markersize", 4},
               {"borderwidth", 0},
               {"xtitle", "Sample"},
               {"ytitle", "Value"},
               {fixRange ? "ytickmin" : "__ytickmin", 0.0},
               {fixRange ? "ytickmax" : "__ytickmax", 1.0}}));
    };
    if (!validCountsPlotted_) {
      doPlot("Sample Counts CurBuild", nV, 0, false);
      doPlot("Sample Counts Opening", nV, 1, false);
      validCountsPlotted_ = true;
    }
    doPlot("Mean Values CurBuild", valV, 0);
    doPlot("Mean Errors CurBuild", errV, 0);
    if (VLOG_IS_ON(1)) {
      doPlot("Mean Values Opening", valV, 1);
      doPlot("Mean Errors Opening", errV, 1);
    }
  }

  for (auto& it : vcounters_) {
    it.second.clear();
  }
  validMaxLen_ = 0;
}

BpttUpdateLoop::BpttUpdateLoop(
    int batchSize,
    int bptt,
    bool decisionsOnly,
    std::shared_ptr<visdom::Visdom> vs)
    : UpdateLoop(batchSize, vs), bptt(bptt), decisionsOnly(decisionsOnly) {}

std::pair<ag::tensor_list, ag::tensor_list> BpttUpdateLoop::preproc(
    std::vector<EpisodeSamples> episodes) {
  // Frist, remove empty episodes so we don't have to deal with them later on
  episodes.erase(
      std::remove_if(
          episodes.begin(),
          episodes.end(),
          [](EpisodeSamples& e) {
            // Ignore last frame (dummy)
            return e.size() < 2;
          }),
      episodes.end());

  torch::NoGradGuard ng;
  auto sampleFeatures = [&] {
    if (spatialFeatures && nonSpatialFeatures) {
      return std::vector<BosFeature>{BosFeature::Map,
                                     BosFeature::MapId,
                                     BosFeature::Race,
                                     BosFeature::Units,
                                     BosFeature::BagOfUnitCounts,
                                     BosFeature::BagOfUnitCountsAbs5_15_30,
                                     BosFeature::Resources5Log,
                                     BosFeature::TechUpgradeBits,
                                     BosFeature::PendingTechUpgradeBits,
                                     BosFeature::TimeAsFrame,
                                     BosFeature::ActiveBo,
                                     BosFeature::NextBo};
    } else if (spatialFeatures) {
      return std::vector<BosFeature>{BosFeature::Map,
                                     BosFeature::Race,
                                     BosFeature::Units,
                                     BosFeature::Resources5Log,
                                     BosFeature::TechUpgradeBits,
                                     BosFeature::PendingTechUpgradeBits,
                                     BosFeature::TimeAsFrame,
                                     BosFeature::ActiveBo,
                                     BosFeature::NextBo};
    } else if (nonSpatialFeatures) {
      return std::vector<BosFeature>{BosFeature::BagOfUnitCounts,
                                     BosFeature::BagOfUnitCountsAbs5_15_30,
                                     BosFeature::MapId,
                                     BosFeature::Race,
                                     BosFeature::Resources5Log,
                                     BosFeature::TechUpgradeBits,
                                     BosFeature::PendingTechUpgradeBits,
                                     BosFeature::TimeAsFrame,
                                     BosFeature::ActiveBo,
                                     BosFeature::NextBo};
    } else {
      throw std::runtime_error("No features defined");
    }
  }();
  auto isConstantInTime = [](BosFeature feature) {
    return (feature == BosFeature::Map || feature == BosFeature::Race);
  };

  int64_t numEpisodes = episodes.size();
  int64_t maxLength = 0;
  for (auto const& episode : episodes) {
    maxLength = std::max(int64_t(episode.size()) - 1, maxLength);
  }

  auto inputsForSample = [&](
      ag::tensor_list& buffers,
      bos::Sample const& sample,
      int64_t idxT,
      int64_t idxB) {
    if (buffers.empty()) {
      // Do initial featurization to allocate buffers
      auto inputs = sample.featurize(sampleFeatures);
      for (auto i = 0U; i < inputs.size(); i++) {
        auto sizes = inputs[i].sizes().vec();
        auto timeConst = isConstantInTime(sampleFeatures[i]);
        sizes.insert(sizes.begin(), numEpisodes); // batch dimension
        sizes.insert(
            sizes.begin(), timeConst ? 1 : maxLength); // time dimension
        auto buf = inputs[i].clone().resize_(sizes).fill_(0);
        buf[timeConst ? 0 : idxT][idxB].copy_(inputs[i]);
        buffers.push_back(buf);
      }
    } else {
      // Featurize into respective buffer position
      for (auto i = 0U; i < sampleFeatures.size(); i++) {
        auto timeConst = isConstantInTime(sampleFeatures[i]);
        if (!timeConst || idxT == 0) {
          auto dest = buffers.at(i)[idxT][idxB];
          auto t = sample.featurize(sampleFeatures[i], dest);
          if (dest.data_ptr() != t.data_ptr()) {
            throw std::runtime_error("Featurization changed underlying buffer");
          }
        }
      }
    }
  };

  auto inputsForEpisode = [&](
      ag::tensor_list& buffers, EpisodeSamples const& episode, int64_t idxB) {
    // Skip last frame (dummy)
    for (auto i = 0U; i < episode.size() - 1; i++) {
      inputsForSample(buffers, episode[i], i, idxB);
    }
  };

  auto targetsForEpisode =
      [&](EpisodeSamples const& episode) -> ag::tensor_list {
    // Skip last frame (dummy)
    auto& sdata = episode[0].staticData;
    auto length = int64_t(episode.size()) - 1;

    ag::tensor_list targets;
    // Value (game outcome)
    if (sdata->won) {
      targets.push_back(torch::ones({maxLength}).unsqueeze(1));
    } else {
      targets.push_back(torch::zeros({maxLength}).unsqueeze(1));
    }

    // Length mask
    auto lenMask = torch::zeros({maxLength});
    lenMask.slice(0, 0, length).fill_(1.0f);
    targets.push_back(lenMask.unsqueeze(1));

    // Mask with decision points
    auto decisionPoints = torch::zeros({maxLength});
    auto acc = decisionPoints.accessor<float, 1>();
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    bool sawSwitch = false;
    auto start = 0U;
    for (auto i = start; i < length; i++) {
      if (episode[i].switched) {
        sawSwitch = true;
        acc[i] = 1.0f;
      } else if (
          !sawSwitch && initialNonDecisionSamples &&
          common::Rand::sample(dist) < kProbExtraDecisionPoints) {
        acc[i] = 1.0f;
      }
    }
    targets.push_back(decisionPoints.unsqueeze(1));

    // Game ID -- we'll treat this as a target for now
    auto gameId = torch::zeros({kMaxGameIdLength}, torch::kByte);
    strcpy((char*)&(gameId.accessor<uint8_t, 1>()[0]), sdata->gameId.c_str());
    targets.push_back(gameId.unsqueeze(0));
    return targets;
  };

  ag::tensor_list inputs;
  std::vector<ag::tensor_list> allTargets;
  int64_t idxB = 0;
  for (auto const& episode : episodes) {
    inputsForEpisode(inputs, episode, idxB++);
    auto tgt = targetsForEpisode(episode);
    allTargets.resize(tgt.size());
    for (auto i = 0U; i < tgt.size(); i++) {
      allTargets[i].push_back(tgt[i]);
    }
  }

  // TODO: We should really switch to named input/outputs for models and these
  // functions...
  ag::tensor_list targets;
  for (auto& tgt : allTargets) {
    if (tgt[0].size(0) == 1) {
      // Constant over time
      targets.push_back(at::cat(tgt, 0).unsqueeze(0));
    } else {
      targets.push_back(at::stack(tgt, 1));
    }
  }

  // Push one addditional target that counts how many decision points still
  // reman in the batch starting from a given time
  auto masks = targets[2];
  auto smask = masks.sum(1).squeeze(1);
  auto seqRemain = smask.flip(0).cumsum(0).flip(0).gt(0).to(torch::kFloat);
  targets.push_back(seqRemain);

  return std::make_pair(inputs, targets);
}

void BpttUpdateLoop::update(ag::tensor_list inputs, ag::tensor_list targets) {
  auto device = model->options().device();
  auto metrics = trainer->metricsContext();

  /// XXX Foolish attempt to protect against CUDNN errors in LSTM backward --
  // make the whole update loop mutually exclusive with potential forwards
  auto lock = trainer->modelWriteLock();

  auto nTargets = targets.size();
  // The actual targets (win/lose)
  auto values = targets.at(0);
  // One mask indicates sequence lengths -- we'll use this for training if
  // !decisionsOnly, and otherwise use them in validation mode
  auto lenMask = targets.at(1);
  auto decisionMask = targets.at(2);
  auto lossMask = decisionsOnly ? decisionMask : lenMask;
  auto vlossMask = decisionsOnly ? lenMask : decisionMask;
  // For early stopping in the bptt loop in case lossMask is zero (if
  // decisionsOnly)
  auto seqRemain = targets.at(nTargets - 1);

  // We can send some small and constant stuff to the model device already
  for (auto& it : inputs) {
    if (it.size(0) == 1) { // constant over time
      it = it.to(device);
    }
  }
  values = values.to(device);
  auto lossMaskD = lossMask.to(device); // keep originals around
  auto vlossMaskD = vlossMask.to(device);

  // We'll sum up gradients over the whole sequences -- zero them before
  if (train_) {
    optim->zero_grad();
  }

  // Collect model output throughout the episode for later visualization
  ag::tensor_list selHeads;
  ag::tensor_list allHeads;
  auto srAcc = seqRemain.accessor<float, 1>();

  // Track per-batch losses and errors
  // "losses" and "errors" are the ones we optimize for wrt decisionsOnly
  // "lossesV" and "errorsV" are the ones we *don't* optimize for but still want
  // to track
  ag::tensor_list losses, lossesV;
  ag::tensor_list errors, errorsV;
  ag::tensor_list numLosses; // entries of loss mask for bptt window

  // Let's go: bptt loop
  ag::tensor_list lastHidden;
  int maxLen = targets[0].size(0);
  for (auto tS = 0; tS < maxLen; tS += bptt) {
    if (tS > 0 && decisionsOnly && train_ && srAcc[tS] < 1.0f) {
      // Early stopping
      break;
    }
    auto tE = tS + std::min(maxLen - tS, bptt);

    // Model input
    ag::tensor_list batchIn;
    for (auto const& in : inputs) {
      if (in.size(0) > 1) {
        batchIn.push_back(in.slice(0, tS, tE).to(device));
      } else {
        // Already on device (see above)
        batchIn.push_back(in);
      }
    }
    auto nextBo = batchIn.back();
    batchIn.pop_back();
    auto input = ag::VariantDict{{"features", batchIn}};
    if (!lastHidden.empty()) {
      input["hidden"] = lastHidden;
    }
    auto output = model->forward(input);
    auto batchTarget = values.slice(0, tS, tE);
    auto heads = output["vHeads"];
    auto switchedTo = heads.gather(heads.dim() - 1, nextBo);

    // Check if we have any loss in this bptt window; otherwise there's no need
    // to do a backward
    auto mask = lossMaskD.slice(0, tS, tE);
    auto msum = mask.sum();
    if (msum.item<float>() > 0) {
      // Losses are summed up and normlized after the bptt loop
      auto loss = torch::binary_cross_entropy(
          switchedTo,
          batchTarget.view(switchedTo.sizes()),
          mask,
          Reduction::Sum);
      if (train_) {
        loss.backward();
      }

      losses.push_back(loss.detach());
      errors.push_back(sumErrors(switchedTo, batchTarget, mask));
      numLosses.push_back(msum);
    }

    // During validation, we want to track losses on both decision points and
    // whole sequences, so let's compute them for the validation mask.
    if (!train_) {
      auto vmask = vlossMaskD.slice(0, tS, tE);
      auto vmsum = vmask.sum().item<float>();
      if (vmsum > 0) {
        auto mloss = torch::binary_cross_entropy(
            switchedTo,
            batchTarget.view(switchedTo.sizes()),
            vmask,
            Reduction::Sum);
        lossesV.push_back(mloss.detach());
        errorsV.push_back(sumErrors(switchedTo, batchTarget, vmask));
      }
    }

    // Keep output that we want to keep
    selHeads.push_back(switchedTo.detach());
    allHeads.push_back(output["vHeads"].detach());
    lastHidden = output.getDict()["hidden"].getTensorList();
    // Detach hidden states from autograd graph
    for (auto& h : lastHidden) {
      if (h.defined()) {
        h.detach_();
      }
    }
  }

  // Normalize gradients by total loss signals and perform optimization step
  if (train_) {
    auto n = lossMaskD.sum();
    for (auto& var : model->parameters()) {
      if (!var.grad().defined()) {
        continue;
      }
      var.grad().div_(n);
    }

    allreduceGradients();
    optim->step();
  }

  // Keep track of losses and errors for individual bptt windows
  // Note that these are wrt the training signal -- i.e. decision points
  // (decisionsOnly) or whole sequences
  for (auto i = 0U; i < losses.size(); i++) {
    auto n = numLosses[i].item<float>();
    auto li = losses[i].item<float>() / n;
    auto ei = errors[i].item<float>() / n;
    VLOG_ALL(1) << fmt::format(
        "{:05d}/T{:04d} bptt loss {:.05f} error {:.05f}",
        numBatches,
        i * bptt,
        li,
        ei);
    metrics->pushEvent(fmt::format("T{}_loss", i * bptt), li);
    metrics->pushEvent(fmt::format("T{}_error", i * bptt), ei);
    if (VLOG_IS_ON(1)) {
      updatePlot(
          fmt::format("Training T{} Loss", i * bptt),
          "Loss/Error",
          {li, ei},
          {"loss", "error"});
    }
  }

  auto batchN = lossMask.sum().item<float>();
  auto batchLoss =
      batchN > 0.0f ? torch::stack(losses).sum().item<float>() / batchN : 0.0f;
  auto batchError =
      batchN > 0.0f ? torch::stack(errors).sum().item<float>() / batchN : 0.0f;
  if (train_) {
    VLOG_ALL(0) << fmt::format(
        "{:05d} batch loss {:.05f} error {:.05f}",
        numBatches,
        batchLoss,
        batchError);
    updatePlot(
        "Training Loss", "Loss", {batchLoss, batchError}, {"loss", "error"});
    metrics->pushEvent("loss", batchLoss);
    metrics->pushEvent("error", batchError);
  } else {
    // Validation: report losses and errors for both decision points and for all
    // samples
    auto batchVN = vlossMask.sum().item<float>();
    auto batchVLoss = batchVN > 0.0f
        ? torch::stack(lossesV).sum().item<float>() / batchVN
        : 0.0f;
    auto batchVError = batchVN > 0.0f
        ? torch::stack(errorsV).sum().item<float>() / batchVN
        : 0.0f;
    VLOG_ALL(1) << fmt::format(
        "{:05d} batch loss {:.05f} m {:.05f} error {:.05f} m {:.05f}",
        numBatches,
        decisionsOnly ? batchVLoss : batchLoss,
        decisionsOnly ? batchLoss : batchVLoss,
        decisionsOnly ? batchVError : batchError,
        decisionsOnly ? batchError : batchVError);
    metrics->pushEvent("loss", decisionsOnly ? batchVLoss : batchLoss);
    metrics->pushEvent("error", decisionsOnly ? batchVError : batchError);
    metrics->pushEvent("mloss", decisionsOnly ? batchLoss : batchVLoss);
    metrics->pushEvent("merror", decisionsOnly ? batchError : batchVError);
  }

  auto selOut = torch::cat(selHeads, 0);
  auto seqOut = torch::cat(allHeads, 0);

  // In validation mode, accumulate errors by head and by time
  if (!train_) {
    updateValidationMetrics(inputs, targets, {selOut, seqOut});
  }

  // In traininig mode, show pretty pictures
  if (train_ && vs != nullptr && FLAGS_plot_every > 0 &&
      numBatches % FLAGS_plot_every == 0) {
    showPlots(inputs, targets, {selOut, seqOut}, 0);
  }
}

void BpttUpdateLoop::postWait() {
  if (train_) {
    UpdateLoop::postWait();
    return;
  }

  // Update validation plots
  auto metrics = trainer->metricsContext();
  auto means = metrics->getMeanEventValues();
  updatePlot(
      "Validation Loss",
      "Loss/Error",
      {means["loss"], means["error"]},
      {"loss", "error"});
  updatePlot(
      "Validation Loss Masked",
      "Loss/Error",
      {means["mloss"], means["merror"]},
      {"loss", "error"});

  UpdateLoop::postWait();
}

void BpttUpdateLoop::updateValidationMetrics(
    ag::tensor_list inputs,
    ag::tensor_list targets,
    ag::tensor_list outputs) {
  auto batchSize = inputs.at(0).size(1);
  auto maxLen = targets.at(0).size(0);
  auto lmasks = targets.at(targets.size() - 3);

  // Keep track of mean errors and predictions, indexed by time and either the
  // current build order or the opening. This assumes that samples are taken at
  // equal times, i.e. sequences align wrt time stamps.
  auto nbuilds = bos::buildOrderMap().size();
  for (auto const& key : {"error", "value"}) {
    for (auto const& what : {"open", "curb"}) {
      vcounters_[fmt::format("{}_{}_mean", what, key)].resize(nbuilds);
      vcounters_[fmt::format("{}_{}_mean", what, key)].resize(nbuilds);
    }
  }
  vcounters_["open_nsamples"].resize(nbuilds);
  vcounters_["curb_nsamples"].resize(nbuilds);
  validMaxLen_ = std::max(maxLen, validMaxLen_);
  for (auto i = 0U; i < nbuilds; i++) {
    for (auto& it : vcounters_) {
      it.second[i].resize(validMaxLen_, 0.0);
    }
  }

  auto selOutP = outputs.at(0).transpose(0, 1).squeeze().to(torch::kCPU);
  // inputs/targets may have more T elements than selOutP because we might
  // stopped the bptt loop early if decisionsOnly is true
  // inputs[8] or 10 is index of active build
  auto aboIndex = (spatialFeatures && nonSpatialFeatures ? 10 : 8);
  auto activeP = inputs.at(aboIndex)
                     .transpose(0, 1)
                     .squeeze()
                     .resize_(selOutP.sizes())
                     .to(torch::kCPU);
  auto targetP = targets.at(0)
                     .transpose(0, 1)
                     .squeeze()
                     .resize_(selOutP.sizes())
                     .ge(0.5f)
                     .to(torch::kCPU);
  auto errorsP = selOutP.ge(0.5f).ne(targetP);
  auto lmaskP = lmasks.sum(0).squeeze().to(torch::kCPU);

  auto sopa = selOutP.accessor<float, 2>();
  auto errpa = errorsP.accessor<uint8_t, 2>();
  auto actpa = activeP.accessor<int64_t, 2>();
  auto length = lmaskP.accessor<float, 1>();
  auto gameIds = targets[3].accessor<uint8_t, 3>();
  for (auto b = 0U; b < batchSize; b++) {
    std::vector<float> predictions;
    char buf[kMaxGameIdLength];
    strcpy(buf, (char*)&(gameIds[0][b][0]));
    std::string gameId(buf);

    auto sopab = sopa[b];
    auto actpab = actpa[b];
    auto errpab = errpa[b];
    auto opening = actpab[0];
    for (auto t = 0U; t < length[b]; t++) {
      auto build = actpab[t];

      auto updateMean = [](double n, double& m, double v) {
        m = m + (v - m) / n;
      };
      auto err = errpab[t] ? 1.0 : 0.0;
      auto val = sopab[t];
      predictions.push_back(sopab[t]);
      {
        auto& n = vcounters_["open_nsamples"][opening][t];
        n += 1.0;
        updateMean(n, vcounters_["open_error_mean"][opening][t], err);
        updateMean(n, vcounters_["open_value_mean"][opening][t], val);
      }
      {
        auto& n = vcounters_["curb_nsamples"][build][t];
        n += 1.0;
        updateMean(n, vcounters_["curb_error_mean"][build][t], err);
        updateMean(n, vcounters_["curb_value_mean"][build][t], val);
      }
    }

    if (dumpPredictions) {
      auto jd = nlohmann::json{
          {"game", gameId}, {"pred", predictions},
      };
      std::cout << jd << std::endl;
    }
  }
}

void BpttUpdateLoop::showPlots(
    ag::tensor_list inputs,
    ag::tensor_list targets,
    ag::tensor_list outputs,
    int index) {
  if (vs == nullptr) {
    return;
  }
  auto lmasks = targets.at(1);

  // Visualize a sequence from the batch
  auto ilen = int64_t(lmasks.slice(1, index, index + 1).sum().item<float>());
  int won = targets.at(0).slice(1, index, index + 1).sum().item<float>() > 0;
  // outputs[1] is all heads
  auto iheads = outputs.at(1)
                    .slice(0, 0, ilen)
                    .slice(1, index, index + 1)
                    .squeeze()
                    .to(torch::kCPU);
  // ouputs[0] is selected head (for next build)
  auto iactive = outputs.at(0)
                     .slice(0, 0, ilen)
                     .slice(1, index, index + 1)
                     .squeeze()
                     .to(torch::kCPU);
  // Due to early stopping in the bptt loop we may not have obtained output for
  // all time steps
  ilen = std::min(ilen, iheads.size(0));
  auto ibuild = torch::zeros_like(iheads);
  // inputs[8] or 10 is index of active build
  auto aboIndex = (spatialFeatures && nonSpatialFeatures ? 10 : 8);
  auto bos =
      inputs.at(aboIndex).slice(1, index, index + 1).squeeze().to(torch::kCPU);
  for (auto i = 0; i < ilen; i++) {
    ibuild[i][bos[i]] = 1;
  }
  vs->line(
      iheads,
      torch::arange(0, ilen),
      vsopts(
          {{"title", fmt::format("Train@{} Model Heads ({})", numBatches, won)},
           {"legend", boNames_},
           {"xtitle", "Sample"},
           {"ytitle", "Value"},
           {"ytickmin", 0.0},
           {"ytickmax", 1.0}}));
  vs->line(
      iactive,
      torch::arange(0, ilen),
      vsopts(
          {{"title", fmt::format("Train@{} Active Head ({})", numBatches, won)},
           {"xtitle", "Sample"},
           {"ytitle", "Value"},
           {"ytickmin", 0.0},
           {"ytickmax", 1.0}}));
  vs->heatmap(
      ibuild.t(),
      vsopts(
          {{"title",
            fmt::format("Train@{} Active Build ({})", numBatches, won)}}));
}

void MacroBatchUpdateLoop::update(
    ag::tensor_list inputs,
    ag::tensor_list targets) {
  if (dist::globalContext()->size > 1 && !trainer->isServer()) {
    throw std::runtime_error("I don't work on multi-GPU.... yet!");
  }

  auto device = model->options().device();
  auto metrics = trainer->metricsContext();

  auto batchSize = inputs.at(0).size(0);
  ag::tensor_list selHeads;
  ag::tensor_list allHeads;

  auto values = targets.at(0).to(device);
  auto mask = targets.at(1).to(device);

  // In train mode, iterate over a permutation of the concatenated games for
  // some more variety in the samples, assuming batchSize (#games) >
  // miniBatchSize
  auto miniBatches = (train_ ? torch::randperm(batchSize, at::kLong)
                             : torch::arange(0, batchSize, at::kLong))
                         .to(inputs.at(0).options().device())
                         .split(miniBatchSize, 0);

  // During training, we expect to only operate on relevant samples, i.e.
  // decision points. During validation, we run on full games, but we want to
  // keep track of both loss/error on decision points and on everything.
  // Hence, we keep track of losses wrt a supplied mask in lossesM and errorsM,
  // but otherwise always optimize on every sample.
  ag::tensor_list losses;
  ag::tensor_list lossesM;
  ag::tensor_list errors;
  ag::tensor_list errorsM;

  // Loop over all mini-batches in this batch of games
  for (auto& miniBatch : miniBatches) {
    ag::tensor_list batchIn;
    for (auto& input : inputs) {
      batchIn.push_back(input.index_select(0, miniBatch).to(device));
    }
    auto nextBo = batchIn.back();
    batchIn.pop_back();
    auto output = model->forward(ag::VariantDict{{"features", batchIn}});
    auto batchTarget = values.index_select(0, miniBatch).to(device);
    auto heads = output["vHeads"];
    auto switchedTo = heads.gather(heads.dim() - 1, nextBo);

    auto loss = torch::binary_cross_entropy(
        switchedTo, batchTarget.view(switchedTo.sizes()), {}, Reduction::Mean);
    if (train_) {
      optim->zero_grad();
      loss.backward();
      {
        auto lock = trainer->modelWriteLock();
        optim->step();
      }
      numUpdates++;
    } else {
      // Compute loss wrt mask as well in validation mode
      auto batchMask = mask.index_select(0, miniBatch);
      auto msum = batchMask.sum();
      if (msum.item<float>() > 0) {
        auto mloss = torch::binary_cross_entropy(
            switchedTo,
            batchTarget.view(switchedTo.sizes()),
            batchMask.view(switchedTo.sizes()),
            Reduction::Sum);
        lossesM.push_back(mloss.detach());
        errorsM.push_back(
            sumErrors(
                switchedTo.view(batchTarget.sizes()), batchTarget, batchMask));
      }
    }

    losses.push_back(loss.detach());
    errors.push_back(
        sumErrors(switchedTo.view(batchTarget.sizes()), batchTarget) /
        miniBatch.size(0));
    selHeads.push_back(switchedTo.detach());
    allHeads.push_back(output["vHeads"].detach());
  }

  if (VLOG_IS_ON(1)) {
    for (auto i = 0U; i < losses.size(); i++) {
      VLOG_ALL(1) << fmt::format(
          "{:05d}/{:03d} mini-batch loss {:.05f} error {:.05f}",
          numBatches,
          i,
          losses[i].item<float>(),
          errors[i].item<float>());
    }
  }
  auto batchLoss = torch::stack(losses).mean().item<float>();
  auto batchError = torch::stack(errors).mean().item<float>();
  if (train_) {
    VLOG_ALL(0) << fmt::format(
        "{:05d}/{:06d} batch loss {:.05f} error {:.05f}",
        numBatches,
        numUpdates,
        batchLoss,
        batchError);
    updatePlot(
        "Train Loss", "Loss/Error", {batchLoss, batchError}, {"loss", "error"});
  } else {
    // Evaluation
    auto batchMN = mask.sum().item<float>();
    auto batchMLoss = batchMN == 0.0
        ? 0.0
        : torch::stack(lossesM).sum().item<float>() / batchMN;
    auto batchMError = batchMN == 0.0
        ? 0.0
        : torch::stack(errorsM).sum().item<float>() / batchMN;
    VLOG_ALL(1) << fmt::format(
        "{:05d}/{:06d} batch loss {:.05f} m {:.05f} error {:.05f} m {:.05f}",
        numBatches,
        numUpdates,
        batchLoss,
        batchMLoss,
        batchError,
        batchMError);
    metrics->pushEvent("mloss", batchMLoss);
    metrics->pushEvent("merror", batchMError);
  }
  metrics->pushEvent("loss", batchLoss);
  metrics->pushEvent("error", batchError);

  // In validation mode, accumulate errors by head and by time
  if (!train_) {
    // XXX assuming in-order traversal in validation mode to keep things
    // simple...
    auto selOut = torch::cat(selHeads, 0);
    auto seqOut = torch::cat(allHeads, 0);
    updateValidationMetrics(inputs, targets, {selOut, seqOut});
  }
}

void MacroBatchUpdateLoop::postWait() {
  if (train_) {
    UpdateLoop::postWait();
    return;
  }

  // Update validation plots
  auto metrics = trainer->metricsContext();
  auto means = metrics->getMeanEventValues();
  updatePlot(
      "Validation Loss",
      "Loss/Error",
      {means["loss"], means["error"]},
      {"loss", "error"});
  updatePlot(
      "Validation Loss Masked",
      "Loss/Error",
      {means["mloss"], means["merror"]},
      {"loss", "error"});

  UpdateLoop::postWait();
}

void MacroBatchUpdateLoop::updateValidationMetrics(
    ag::tensor_list inputs,
    ag::tensor_list targets,
    ag::tensor_list outputs) {
  auto batchSize = inputs.at(0).size(0); // total number of samples
  auto gameIdx = targets.at(2).squeeze().to(torch::kCPU).to(torch::kI32);
  auto gameIds = targets.at(3).to(torch::kCPU);

  // Determine maximum game length in this batch
  auto gamea = gameIdx.accessor<int32_t, 1>();
  int maxLen = 0;
  int curLen = 0;
  int curGame = -1;
  for (auto b = 0U; b < batchSize; b++) {
    if (curGame < 0 || curGame != gamea[b]) {
      maxLen = std::max(maxLen, curLen);
      curLen = 0;
      curGame = gamea[b];
    }
    curLen++;
  }
  maxLen = std::max(maxLen, curLen);

  // Keep track of mean for errors and predictions, indexed by time and
  // either the current build order or the opening. This assumes that samples
  // are taken at equal times, i.e. sequences align wrt time stamps.
  auto nbuilds = bos::buildOrderMap().size();
  for (auto const& key : {"error", "value"}) {
    for (auto const& what : {"open", "curb"}) {
      vcounters_[fmt::format("{}_{}_mean", what, key)].resize(nbuilds);
      vcounters_[fmt::format("{}_{}_mean", what, key)].resize(nbuilds);
    }
  }
  vcounters_["open_nsamples"].resize(nbuilds);
  vcounters_["curb_nsamples"].resize(nbuilds);
  validMaxLen_ = std::max(int64_t(maxLen), validMaxLen_);
  for (auto i = 0U; i < nbuilds; i++) {
    for (auto& it : vcounters_) {
      it.second[i].resize(validMaxLen_, 0.0);
    }
  }

  auto selOutP = outputs.at(0).squeeze().to(torch::kCPU);
  // input[8] is active build
  auto activeP =
      inputs.at(8).squeeze().resize_(selOutP.sizes()).to(torch::kCPU);
  auto targetP =
      targets.at(0).squeeze().resize_(selOutP.sizes()).ge(0.5f).to(torch::kCPU);
  auto errorsP = selOutP.ge(0.5f).ne(targetP);

  auto sopa = selOutP.accessor<float, 1>();
  auto errpa = errorsP.accessor<uint8_t, 1>();
  auto actpa = activeP.accessor<int64_t, 1>();
  curGame = -1;
  int opening = -1;
  int t = 0;
  std::vector<float> predictions;
  std::string gameId;
  for (auto b = 0U; b < batchSize; b++) {
    if (curGame < 0 || gamea[b] != curGame) {
      curGame = gamea[b];
      opening = actpa[b];
      t = 0;

      if (dumpPredictions && !predictions.empty()) {
        auto jd = nlohmann::json{
            {"game", gameId}, {"pred", predictions},
        };
        std::cout << jd << std::endl;
      }
      predictions.clear();
      char buf[kMaxGameIdLength];
      strcpy(buf, (char*)(gameIds[curGame].data<uint8_t>()));
      gameId = std::string(buf);
    }
    auto build = actpa[b];

    auto updateMean = [](double n, double& m, double v) {
      m = m + (v - m) / n;
    };
    auto err = errpa[b] ? 1.0 : 0.0;
    auto val = sopa[b];
    predictions.push_back(val);
    {
      auto& n = vcounters_["open_nsamples"][opening][t];
      n += 1.0;
      updateMean(n, vcounters_["open_error_mean"][opening][t], err);
      updateMean(n, vcounters_["open_value_mean"][opening][t], val);
    }
    {
      auto& n = vcounters_["curb_nsamples"][build][t];
      n += 1.0;
      updateMean(n, vcounters_["curb_error_mean"][build][t], err);
      updateMean(n, vcounters_["curb_value_mean"][build][t], val);
    }
    t++;
  }
}

std::pair<ag::tensor_list, ag::tensor_list> LinearModelUpdateLoop::preproc(
    std::vector<EpisodeSamples> episodes) {
  // Frist, remove empty episodes so we don't have to deal with them later on
  episodes.erase(
      std::remove_if(
          episodes.begin(),
          episodes.end(),
          [](EpisodeSamples& e) {
            // Ignore last frame (dummy)
            return e.size() < 2;
          }),
      episodes.end());

  torch::NoGradGuard ng;
  auto sampleFeatures =
      std::vector<BosFeature>{BosFeature::BagOfUnitCounts,
                              BosFeature::BagOfUnitCountsAbs5_15_30,
                              BosFeature::MapId,
                              BosFeature::Race,
                              BosFeature::Resources5Log,
                              BosFeature::TechUpgradeBits,
                              BosFeature::PendingTechUpgradeBits,
                              BosFeature::TimeAsFrame,
                              BosFeature::ActiveBo,
                              BosFeature::NextBo};

  std::uniform_real_distribution<double> dist(0.0, 1.0);
  auto consideredSamples = [&](EpisodeSamples const& episode) {
    std::set<int> considered;
    bool sawSwitch = false;
    for (auto i = 0U; i < episode.size() - 1; i++) {
      // In training mode, skip any non-decision frames entirely if needed
      if (decisionsOnly && train_) {
        if (episode[i].switched) {
          considered.insert(i);
        } else if (
            !sawSwitch && initialNonDecisionSamples &&
            common::Rand::sample(dist) < kProbExtraDecisionPoints) {
          considered.insert(i);
        }
      } else {
        considered.insert(i);
      }
      sawSwitch |= episode[i].switched;
    }
    return considered;
  };

  std::vector<std::set<int>> considered;
  int64_t numSamples = 0; // in macro batch
  for (auto const& episode : episodes) {
    considered.push_back(consideredSamples(episode));
    numSamples += considered.back().size();
  }

  auto inputsForSample = [&](
      ag::tensor_list& buffers, bos::Sample const& sample, int64_t idx) {
    if (buffers.empty()) {
      // Do initial featurization to allocate buffers
      auto inputs = sample.featurize(sampleFeatures);
      for (auto i = 0U; i < inputs.size(); i++) {
        auto sizes = inputs[i].sizes().vec();
        sizes.insert(sizes.begin(), numSamples); // batch dimension
        auto buf = inputs[i].clone().resize_(sizes).fill_(0);
        buf[idx].copy_(inputs[i]);
        buffers.push_back(buf);
      }
    } else {
      // Featurize into respective buffer position
      for (auto i = 0U; i < sampleFeatures.size(); i++) {
        auto dest = buffers.at(i)[idx];
        auto t = sample.featurize(sampleFeatures[i], dest);
        // XXX Not sure if this is right after the pytorch update
        if (dest.data_ptr() != t.data_ptr()) {
          throw std::runtime_error("Featurization changed underlying buffer");
        }
      }
    }
  };

  auto inputsForEpisode = [&](
      ag::tensor_list& buffers,
      EpisodeSamples const& episode,
      int idxE,
      int64_t idx) -> int64_t {
    int64_t n = 0;
    // Skip last frame (dummy)
    for (auto i = 0U; i < episode.size() - 1; i++) {
      if (considered[idxE].find(i) != considered[idxE].end()) {
        inputsForSample(buffers, episode[i], idx + n);
        n++;
      }
    }
    return n;
  };

  int episodeIdx = 0;
  auto targetsForEpisode = [&](
      EpisodeSamples const& episode, int idxE) -> ag::tensor_list {
    // Skip last frame (dummy)
    auto& sdata = episode[0].staticData;
    int64_t effectiveLength = considered[idxE].size();

    ag::tensor_list targets;
    // Value (game outcome)
    if (sdata->won) {
      targets.push_back(torch::ones({effectiveLength}));
    } else {
      targets.push_back(torch::zeros({effectiveLength}));
    }

    // Mask with decision points
    if (train_ && decisionsOnly) {
      // Every sample is a decision point...
      targets.push_back(torch::ones({effectiveLength}));
    } else {
      auto decisionPoints = torch::zeros({effectiveLength});
      auto acc = decisionPoints.accessor<float, 1>();
      int64_t idx = 0;
      for (auto i = 0U; i < episode.size() - 1; i++) {
        if (considered[idxE].find(i) != considered[idxE].end()) {
          acc[idx++] = episode[i].switched ? 1.0f : 0.0f;
        }
      }
      targets.push_back(decisionPoints);
    }

    // Game index
    targets.push_back(
        torch::empty({effectiveLength}, torch::kI32).fill_(episodeIdx));
    episodeIdx++;

    // Game ID -- we'll treat this as a target for now
    auto gameId = torch::zeros({kMaxGameIdLength}, torch::kByte);
    strcpy((char*)&(gameId.accessor<uint8_t, 1>()[0]), sdata->gameId.c_str());
    targets.push_back(gameId.unsqueeze(0));
    return targets;
  };

  ag::tensor_list inputs;
  std::vector<ag::tensor_list> allTargets;
  int idxE = 0;
  int64_t idxB = 0;
  for (auto const& episode : episodes) {
    auto n = inputsForEpisode(inputs, episode, idxE, idxB);
    auto tgt = targetsForEpisode(episode, idxE);
    allTargets.resize(tgt.size());
    for (auto i = 0U; i < tgt.size(); i++) {
      allTargets[i].push_back(tgt[i]);
    }
    idxE++;
    idxB += n;
  }

  // TODO: We should really switch to named input/outputs for models and these
  // functions...
  ag::tensor_list targets;
  for (auto& tgt : allTargets) {
    targets.push_back(at::cat(tgt, 0));
  }

  // The update part for this loop is fast, we'll better send stuff to the
  // device already
  auto device = model->options().device();
  for (auto& it : inputs) {
    it = it.to(device);
  }
  for (auto& it : targets) {
    it = it.to(device);
  }
  return std::make_pair(inputs, targets);
}

void IdleUpdateLoop::operator()(EpisodeSamples episode) {
  return;
}

std::pair<ag::tensor_list, ag::tensor_list> IdleUpdateLoop::preproc(
    std::vector<EpisodeSamples> episodes) {
  throw std::runtime_error("function IdleLoop::preproc not implemented!");
  return make_pair(ag::tensor_list(), ag::tensor_list());
}

void IdleUpdateLoop::update(ag::tensor_list inputs, ag::tensor_list targets) {
  throw std::runtime_error("function IdleLoop::update not implemented!");
  return;
}

} // namespace cherrypi
