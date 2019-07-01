
/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "gas_trainer_impala.h"
#include "common.h"
#include "common/autograd.h"
#include "cpid/batcher.h"
#include "cpid/sampler.h"
#include "model.h"
#include <thread>
#include <math.h> 

namespace cpid {
GasTrainerA2C::GasTrainerA2C(
    ag::Container model,
    ag::Optimizer optim,
    std::unique_ptr<BaseSampler> sampler,
    std::unique_ptr<AsyncBatcher> batcher,
    int returnsLength,
    int trainerBatchSize,
    float maxGradientNorm,
    float discount,
    float valueLossCoef,
    float entropyLossCoef,
    bool overlappingUpdates,
    bool memoryEfficient)
    : SyncTrainer(
          model,
          optim,
          std::move(sampler),
          std::move(batcher),
          returnsLength,
          1,
          trainerBatchSize,
          overlappingUpdates,
          false, //forceOnPolicy
          memoryEfficient,
          true,
          maxGradientNorm),
      discount_(discount),
      valueLossCoef_(valueLossCoef),
      entropyLossCoef_(entropyLossCoef) {
  VLOG(3) << "force on policy " << forceOnPolicy_;
  VLOG(3) << "overlap updates " << overlappingUpdates_;
  for (int l = 0; l <= FLAGS_max_lod; l++) {
      auto lodIdx = torch::arange(0, std::pow(2, FLAGS_max_lod), std::pow(2, FLAGS_max_lod - l)).to(torch::kLong);
      if (model_->options().device().is_cuda()) {
        lodIdx = lodIdx.to(torch::kCUDA);
      }
      lodIndices_.push_back(lodIdx);
  }
}

void GasTrainerA2C::doUpdate(
    const std::vector<std::shared_ptr<SyncFrame>>& seq,
    torch::Tensor terminal) {
  optim_->zero_grad();
  bool isCuda = model_->options().device().is_cuda();
  int batchSize = terminal.size(1);
  common::assertSize("terminal", terminal, {returnsLength_, batchSize});

  auto notTerminal = (1 - terminal);
  notTerminal = notTerminal.to(at::kFloat).set_requires_grad(false);

  torch::Tensor totValueLoss = torch::zeros({1});
  torch::Tensor totMeanQ = torch::zeros({1});
  torch::Tensor totRegLoss = torch::zeros({1});
  torch::Tensor totPolicyLoss = torch::zeros({1});
  torch::Tensor totEntropyLoss = torch::zeros({1});

  if (isCuda) {
    notTerminal = notTerminal.to(torch::kCUDA);
    totValueLoss = totValueLoss.to(torch::kCUDA);
    totMeanQ = totMeanQ.to(torch::kCUDA);
    totRegLoss = totRegLoss.to(torch::kCUDA);
    totPolicyLoss = totPolicyLoss.to(torch::kCUDA);
    totEntropyLoss = totEntropyLoss.to(torch::kCUDA);
  }

  BatchedFrame lastFrame;
  computeAllForwardModel(model_, seq, batchSize, notTerminal);
  lastFrame = *std::static_pointer_cast<BatchedFrame>(seq.back());

  ag::Variant& lastOut = lastFrame.forwarded_state;
  auto V = lastOut[kVKey].reshape({batchSize});

  auto nstepTarget = V.detach();
  auto nextV = V.detach().clone();
  auto target = nextV.clone();
  auto acc = torch::zeros_like(nextV);

  common::assertSize("notterminal", notTerminal, {returnsLength_, batchSize});
  for (int i = (int)seq.size() - 2; i >= 0; --i) {
    VLOG(2) << "TIMESTEP" << i;
    auto currentNotTerminal = notTerminal[i].unsqueeze(1);
    auto currentFrame = std::static_pointer_cast<BatchedFrame>(seq[i]);
    ag::Variant& currentOut = currentFrame->forwarded_state;
    auto currentV = currentOut[kVKey].reshape({batchSize});

    torch::Tensor currentActions = currentFrame->action;
    VLOG(2) << "action " << currentActions;
    auto allPiLogits = currentOut.getDict()[kAllQKey].getTensorList();

    // B x G x A. G is maxG in batch, missing are -42 from batcher
    auto allMuLogits = currentFrame->state.getDict()[kStateKey].getDict()[kPActionKey];
    auto frameLod = currentFrame->state.getDict()[kStateKey][kLodKey];

    torch::Tensor intrinsicLod = torch::ones_like(currentV) * (double)FLAGS_max_lod;
    for (int l = 1; l <= FLAGS_max_lod; l++) {
      auto actReshape = currentActions.reshape({batchSize, currentActions.size(1)/(std::pow(2,l)), -1});
      VLOG(2) << "lod " << l << " rs " << actReshape;
      auto lMask = (actReshape.slice(2, 0, 1).eq(actReshape)).all(1).all(1);
      VLOG(2) << "lmask " << lMask;
      intrinsicLod -= lMask.to(torch::kFloat);
    }
    VLOG(2) << "intr lod " << intrinsicLod;

    torch::Tensor lastLogPi, lastPi;
    double baseLodPi, pGrowLodPi;
    pGrowLodPi = std::modf(getLod(), &baseLodPi);
    auto lodMuScheduled = currentFrame->state.getDict()[kStateKey][kLodProbKey].squeeze(1);
    auto muLodFloor = torch::floor(lodMuScheduled);
    auto muLodCeil = torch::ceil(lodMuScheduled);
    auto piTaken = torch::zeros_like(intrinsicLod);
    auto muTaken = torch::zeros_like(intrinsicLod);
    //calculate logpi, logmu from group probs. do entropy and match loss on the way
    for (int l = 0; l <= FLAGS_max_lod; l++) {
      auto lMask = intrinsicLod.le(l).to(torch::kFloat);
      VLOG(2) << "lmask " << lMask;
      auto lodTakenMask = frameLod.eq(l).to(torch::kFloat);
      auto lodActions = currentActions.index_select(1, lodIndices_[l]);

      auto lodPiLogits = allPiLogits[l];
      auto lodMuLogits = allMuLogits[l];
      auto logPi = torch::log_softmax(lodPiLogits, 2);
      //auto logMu = torch::log_softmax(lodMuLogits, 2);

      auto pi = torch::softmax(lodPiLogits, 2);
      auto mu = torch::softmax(lodMuLogits, 2);

      // B x LG
      auto piTakenLod = pi.gather(2, lodActions.unsqueeze(2)).squeeze(2);
      auto muTakenLod = mu.gather(2, lodActions.unsqueeze(2)).squeeze(2);

      double pLodPi = 0.0;
      if (l == baseLodPi) {
        pLodPi = 1.0 - pGrowLodPi;
      } else if (l == baseLodPi + 1) {
        pLodPi = pGrowLodPi;
      }
      VLOG(2) << "plodpi " << pLodPi;
      auto pLodPiTensor = torch::ones_like(currentV) * pLodPi;
      auto pLodMu = torch::zeros_like(currentV);
      pLodMu.masked_scatter_(muLodFloor.eq(l), muLodCeil - lodMuScheduled);
      pLodMu.masked_scatter_(muLodCeil.eq(l), lodMuScheduled - muLodFloor);
      pLodMu.masked_fill_(lodMuScheduled.eq(l), 1.0);
      VLOG(2) << "plodmu " << pLodMu;

      piTaken = piTaken + (piTakenLod.prod(1) * pLodPiTensor) * lMask;
      muTaken = muTaken + (muTakenLod.prod(1) * pLodMu) * lMask;

      // NB this is a conditional entropy conditioned on lod selection
      totEntropyLoss = totEntropyLoss + ((logPi*pi).sum(2).sum(1) * pLodPiTensor).sum(0);

      // NB only matching from first to second level for now
      // if (l == int(baseLodPi)) {
      if (l == 0) {
        lastLogPi = logPi.clone();
        lastPi = pi.clone();
      }
      // if (l > int(baseLodPi) && FLAGS_match_loss_coef > 0.0) {
      if (l >= 1 && int(baseLodPi) == 0) {
        auto lastLogPiRepeat = lastLogPi.unsqueeze(2).repeat({1,1,logPi.size(1)/lastPi.size(1),1}).reshape(logPi.sizes());
        VLOG(3) << "logpi_repeat " << lastLogPiRepeat;
        auto lastPiRepeat = lastPi.unsqueeze(2).repeat({1,1,logPi.size(1)/lastPi.size(1),1}).reshape(logPi.sizes());
        // double matchWeight = (l == int(baseLodPi + 1)) ? (1.0 - pGrowLodPi) : 0.0;
        double matchWeight = (1.0 - pGrowLodPi);
        totRegLoss = totRegLoss + (lastPiRepeat * (lastLogPiRepeat - logPi)).sum(2).sum(1).sum(0) * matchWeight;
        VLOG(3) << "rl " << totRegLoss;
      }
      // if (l == int(baseLodPi + 1)) {
      //   lastLogPi = lastLogPi.detach();
      //   lastPi = lastPi.detach();
      // }
    }
    VLOG(2) << "pi taken " << piTaken;
    VLOG(2) << "mu taken " << muTaken;
    auto logPiTaken = torch::log(piTaken);
    auto logMuTaken = torch::log(muTaken);

    auto rho = torch::exp(logPiTaken - logMuTaken).clamp_max(1.0);
    VLOG(2) << "rho " << rho;
    auto c = rho;

    auto deltaV = rho * (currentFrame->reward + discount_ * notTerminal[i] * nextV) - currentV;
    VLOG(2) << "deltaV " << deltaV;

    acc = deltaV + discount_ * notTerminal[i] * c * acc;
    VLOG(2) << "acc " << acc;
    auto adv = currentFrame->reward + discount_ * notTerminal[i] * target - currentV;
    VLOG(2) << "adv " << adv;
    target = (currentV + acc).detach();
    VLOG(2) << "target " << target;
    auto pgAdv = (rho * adv).detach();

    if (currentNotTerminal.sum().item<int32_t>() != batchSize) {
      VLOG(2) << "step with terminal";
    }

    auto valueLoss = at::smooth_l1_loss(currentV, target, false).sum(0);
    VLOG(2) << "vloss " << valueLoss;
    totValueLoss = totValueLoss + valueLoss;
    auto policyLoss = - (pgAdv * logPiTaken).sum(0);
    VLOG(2) << "ploss " << policyLoss;
    totPolicyLoss = totPolicyLoss + policyLoss;

    nextV = currentV;
  }
  totValueLoss /= ((float)(seq.size() - 1) * (float)FLAGS_batch_size);
  totPolicyLoss /= ((float)(seq.size() - 1) * (float)FLAGS_batch_size);
  totEntropyLoss /= ((float)(seq.size() - 1) * (float)FLAGS_batch_size);
  totRegLoss /= ((float)(seq.size() - 1) * (float)FLAGS_batch_size);
  if (getUpdateCount() % 10 == 0) {
    metricsContext_->pushEvent("value_loss", totValueLoss.item<float>());
    metricsContext_->pushEvent("policy_loss", totPolicyLoss.item<float>());
    metricsContext_->pushEvent("reg_loss", totRegLoss.item<float>());
    metricsContext_->pushEvent("entropy", totEntropyLoss.item<float>());
    metricsContext_->pushEvent("batch_size", batchSize);
  }
  auto totLoss = totValueLoss * valueLossCoef_ + totPolicyLoss + totEntropyLoss * entropyLossCoef_
    + totRegLoss * FLAGS_match_loss_coef;
  VLOG(2) << "loss " << totLoss.item<float>();
  totLoss.backward();
  doOptimStep();
}

void GasTrainerA2C::step(
    EpisodeHandle const& handle,
    std::shared_ptr<ReplayBufferFrame> v,
    bool isDone) {
  auto& uid = handle.gameID();
  {
    std::lock_guard<priority_mutex> lk(stepMutex_);
    auto frame = std::static_pointer_cast<SingleFrame>(v);
    if (cumRewards_.count(uid) == 0) {
      cumRewards_[uid] = 0;
    }
    cumRewards_[uid] += frame->reward;
    // cumRewards is computed in the derived class
    // if (isDone) {
    //   metricsContext_->pushEvent("cumulated_reward", cumRewards_[uid]);
    //   cumRewards_[uid] = 0;
    // }
  }
  SyncTrainer::step(handle, v, isDone);
}

float GasTrainerA2C::getEpsilon() {
  return 0.0;
}

float GasTrainerA2C::getLod() {
  if (FLAGS_only_train_max_lod) {
    return (float)FLAGS_max_lod;
  } else if (FLAGS_gas_on_plateau > 0) {
    throw std::invalid_argument("Can't GAS on plateau with A2C");
  } else if (FLAGS_lod_growth_length == 0) {
    return FLAGS_min_lod;
  } else {
    return std::min(
        (float)FLAGS_max_lod,
        (float)FLAGS_min_lod +
        std::max(0.0f, (float)getUpdateCount() - FLAGS_lod_lead_in)
         / (float)FLAGS_lod_growth_length);
  }
}

} // namespace cpid
