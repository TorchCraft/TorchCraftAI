/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "gasmicromodule.h"

#include "common/autograd.h"
#include "cpid/batcher.h"
#include "cpid/distributed.h"
#include "cpid/estrainer.h"
#include "cpid/evaluator.h"
#include "cpid/sampler.h"
#include "cpid/zeroordertrainer.h"
#include "modules/cherryvisdumper.h"

#include "utils.h"

#include "common.h"
#include "flags.h"
#include "gasmodelpf.h"
#include "model.h"

#include <visdom/visdom.h>

using namespace cherrypi;

namespace microbattles {

GasMicroModule::GasMicroModule(
    std::shared_ptr<TrainingSetup> setup,
    std::shared_ptr<cpid::Trainer> trainer,
    std::unique_ptr<Reward>&& reward)
    : MicroModule(setup, trainer, std::move(reward)) {
  numGroups_ = std::pow(2, FLAGS_max_lod);
  std::random_device rd;
  rngEngine_.seed(rd());
  setName("GasMicroLearner");
}

std::pair<torch::Tensor, torch::Tensor> GasMicroModule::twoMeans(torch::Tensor locs, std::pair<int,int> lod_grp) {
  torch::Tensor means;
  if (groupMeans_.find(lod_grp) == groupMeans_.end() || !FLAGS_gas_reuse_centroids) {
    // XXX first and last units to make deterministic
    means = torch::stack({locs[0], locs[locs.size(0) - 1]}, 0);
  }
  else{
    means = groupMeans_[lod_grp].clone();
  }
  auto assignments = torch::zeros({locs.size(0)}, torch::kInt);
  for (size_t iterations=0; iterations < 10; ++iterations) {
    // If cluster mean is 0 reassign to first units to make deterministic and
    // non-empty
    if(means[0].sum().item<double>() == 0.0) {
      means = torch::stack({locs[0], means[1]}, 0);
    }
    if(means[1].sum().item<double>() == 0.0) {
      means = torch::stack({means[0], locs[0]}, 0);
    }
    assignments = torch::zeros({locs.size(0)}, torch::kInt);
    for (int u=0; u < locs.size(0); u++) {
      double best_distance = std::numeric_limits<double>::max();
      int best_cluster = 0;
      for (int cluster=0; cluster < 2; ++cluster) {
        double distance = (locs[u] - means[cluster]).pow(2).sum().item<double>();
        if (distance < best_distance) {
          best_distance = distance;
          best_cluster = cluster; 
        }
      }
      assignments[u] = best_cluster;
    }

    torch::Tensor new_means = torch::zeros_like(means);
    torch::Tensor counts = torch::zeros({2}, torch::kInt);
    for (int u=0; u < locs.size(0); u++) {
      auto cluster = assignments[u];
      new_means[cluster] += locs[u];
      counts[cluster] += 1;
    }
    // If group is unoccupied return mean=0
    counts = torch::max(counts, torch::ones_like(counts));
    means = new_means / counts.unsqueeze(1);
  }
  groupMeans_[lod_grp] = means;
  return std::make_pair(assignments, means);
}

void GasMicroModule::act(State* state) {
  if (currentFrame_ == 0 || (currentFrame_ - 1) % FLAGS_frame_skip != 0) {
    return;
  }
  if (state->unitsInfo().myUnits().size() == 0) {
    VLOG(-4) << "MY UNITS EMPTY!!";
  }
  if (state->unitsInfo().enemyUnits().size() == 0) {
    VLOG(-4) << "ENEMY UNITS EMPTY!!";
  }

  lines_.clear();
  circles_.clear();
  torch::NoGradGuard g_;
  auto stateTensor = featurizer_->featurize(state);
  plotHeatmaps(state, stateTensor, 4);
  // XXX: maybe refactor this for all featurizers/models
  ag::tensor_list& stateTensorList = stateTensor.getTensorList();
  ag::Variant stateDict = ag::VariantDict{{kMapFeatsKey, stateTensorList[0]},
                                          {kOurLocsKey, stateTensorList[1]},
                                          {kOurFeatsKey, stateTensorList[2]},
                                          {kNmyLocsKey, stateTensorList[3]},
                                          {kNmyFeatsKey, stateTensorList[4]}};

  auto ourLocs = FLAGS_group_w_unittype ? stateTensorList[5] : stateDict[kOurLocsKey]; // our_units x 2 -> contains both positions and unit type

  auto maskedSelect2D = [](torch::Tensor t, torch::Tensor inds) {
    auto t1D = t.masked_select(inds.unsqueeze(1));
    return t1D.view({-1, t.size(1)});
  };
  auto means = torch::zeros({numGroups_, ourLocs.size(1)});
  auto oldAssignments = torch::zeros({ourLocs.size(0)}, torch::kInt);
  for (uint64_t lod = 0; lod < FLAGS_max_lod; lod++) {
    auto newAssignments = torch::zeros_like(oldAssignments);
    for (int grp = std::pow(2, lod) - 1; grp >= 0; grp--) {
      auto grpMask = oldAssignments.eq(grp);
      if (grpMask.eq(0).all().item<uint8_t>()) {
        means[2 * grp] = 0;
        means[2 * grp + 1] = 0;
        continue;
      }
      torch::Tensor splitAssignments, splitMeans;
      std::pair<int, int> lod_grp = std::make_pair(int(lod),grp);
      std::tie(splitAssignments, splitMeans) = twoMeans(maskedSelect2D(ourLocs, grpMask), lod_grp);
      newAssignments.masked_scatter_(
          grpMask,
          2 * oldAssignments.masked_select(grpMask) + splitAssignments);
      means[2 * grp] = splitMeans[0];
      means[2 * grp + 1] = splitMeans[1];
    }
    oldAssignments = newAssignments;
  }
  VLOG(3) << "final asgn " << oldAssignments;
  stateDict[kGrpAssignments] = oldAssignments;

  auto& ourUnits = state->unitsInfo().myUnits();
  std::vector<cp::tc::BW::Color> colors{tc::BW::Color::Green,
                                        tc::BW::Color::Blue,
                                        tc::BW::Color::Yellow,
                                        tc::BW::Color::Red,
                                        tc::BW::Color::Cyan,
                                        tc::BW::Color::Purple,
                                        tc::BW::Color::Grey,
                                        tc::BW::Color::White
                                      };
  for (auto u_idx = 0L; u_idx < oldAssignments.size(0); u_idx++) {
    auto grp = oldAssignments[u_idx].item<int32_t>();
    addCircle(ourUnits[u_idx], 10, colors[grp % colors.size()]);
  }
  stateDict[kLodKey] = torch::tensor(actLod_, torch::kLong);
  stateDict = common::applyTransform(stateDict, [](torch::Tensor const& t) {
    return t.to(defaultDevice());
  });

  auto input = ag::VariantDict{{kStateKey, stateDict}};
  // Perform batch forward pass and assign all actions
  auto modelOut = trainer_->forward(input, handle_);
  plotHeatmaps(state, modelOut, 4);

  torch::Tensor grpCommands;
  std::vector<PFMicroActionModel::PFMicroAction> actions;
  if (actionRepeatCounter_ == 0 ||
      actionRepeatCounter_ >= FLAGS_action_repeat) {
    if (FLAGS_a2c) {
      auto gasTrainerA2C = std::dynamic_pointer_cast<cpid::GasTrainerA2C>(setup_->trainer);
      double lodScheduled = gasTrainerA2C->getLod();
      input[kStateKey].getDict()[kLodProbKey] = torch::ones(1) * lodScheduled;
      double baseLod, pGrowLod;
      pGrowLod = std::modf(lodScheduled, &baseLod);
      actLod_ = (int)baseLod + std::bernoulli_distribution(pGrowLod)(rngEngine_);
      if (trainer_->is<cpid::Evaluator>()) {
        epsilon_ = 0.0;
      } else {
        epsilon_ = 1.0;
      }
    }
    std::tie(grpCommands, actions) =
      std::dynamic_pointer_cast<GASMicroActionModel>(setup_->model)
        ->decodeGasOutput(state, input, modelOut, actLod_, epsilon_);
    lastGrpCommands_ = grpCommands;
    lastActions_ = actions;
    actionRepeatCounter_ = 1;
  } else {
    grpCommands = lastGrpCommands_;
    actions = lastActions_;
  }
  actionRepeatCounter_++;
  modelOut[kActionKey] = grpCommands.to(torch::kLong);
  if (FLAGS_a2c) {
    // passing these around in state for convenience (batching/gpu move stuff done)
    std::vector<torch::Tensor> muLogits;
    if (FLAGS_max_lod == 0) {
      muLogits.push_back(modelOut[kAllQKey]);
    } else {
      muLogits = modelOut.getDict()[kAllQKey].getTensorList();
    }
    input[kStateKey].getDict()[kPActionKey] = muLogits;
  }

  if (setup_->trainerTakesPreviousActionAndState) {
    trainerStep(state, false);
    lastFeatures_ = input;
    lastModelOut_ = modelOut;
  } else {
    lastFeatures_ = input;
    lastModelOut_ = modelOut;
    trainerStep(state, false);
  }
  if (auto tracer = state->board()->getTraceDumper()) {
    if (setup_->modelProvidesValueKey) {
      auto qTaken = modelOut[kQKey]
                        .to(torch::kCPU)
                        .gather(1, modelOut[kActionKey].unsqueeze(1))
                        .mean();
      tracer->dumpGameValue(state, "predicted value", qTaken.item<float>());
    }
    tracer->dumpGameValue(state, "frame reward", frameReward_);
  }

  for (auto& action : actions) {
    auto upc = actionToUPC(action);
    if (upc != nullptr) {
      state->board()->postUPC(std::move(upc), kRootUpcId, this);
    }
  }
}

void GasMicroModule::onGameStart(State* state) {
  MicroModule::onGameStart(state);
  auto gasTrainer = std::dynamic_pointer_cast<cpid::GasTrainer>(trainer_);
  if (gasTrainer == nullptr) {
    epsilon_ = 0.0;
    actLod_ = (float)FLAGS_max_lod;
    return;
  }
  if (gasTrainer->isTrain()) {
    epsilon_ = gasTrainer->getEpsilon();
  } else {
    epsilon_ = 0.0;
  }
  double lodScheduled = gasTrainer->getLod();
  double baseLod, pGrowLod;
  pGrowLod = std::modf(lodScheduled, &baseLod);
  actLod_ = (int)baseLod + std::bernoulli_distribution(pGrowLod)(rngEngine_);
}
} // namespace microbattles
