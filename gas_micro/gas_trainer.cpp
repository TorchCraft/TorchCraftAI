
/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "gas_trainer.h"
#include "common.h"
#include "common/autograd.h"
#include "cpid/batcher.h"
#include "cpid/sampler.h"
#include "model.h"
#include <thread>
#include <math.h> 

DEFINE_bool(q_learn, true, "use q learning instead of sarsa");
DEFINE_bool(double_q, true, "use double q learning");
DEFINE_bool(iql, false, "use IQL loss, else VDN");
DEFINE_double(delta_reg_coef, 0.0, "coef on regularisation of value deltas");
DEFINE_bool(use_target_net, true, "Use target network");
DEFINE_int32(
    target_update_interval,
    200,
    "number of model updates between target net udpates");
DEFINE_string(
    q_weighting,
    "binary",
    "Q weighting mode: binary, num_units, TODO:score");
DEFINE_bool(epsilon_per_thread, false, "each thread trains with a different epsilon (no decay) (uses FLAGS_epsilon_max as epsilon from formula accoring to apex paper)");
DEFINE_double(alpha, 7, "alpha value in cacluation for determining thread epsilon as per apex paper");
DEFINE_bool(on_actionspace, false, "Only update the values for the current level of detail (true) vs all level of detail <= current (false)"); //makes most sense as an arg, but flag is convinient


namespace cpid {
GasTrainer::GasTrainer(
    ag::Container model,
    ag::Optimizer optim,
    std::unique_ptr<BaseSampler> sampler,
    std::unique_ptr<AsyncBatcher> batcher,
    int returnsLength,
    int trainerBatchSize,
    float maxGradientNorm,
    float discount,
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
          false,
          memoryEfficient,
          true,
          maxGradientNorm),
      discount_(discount) {
  if (FLAGS_use_target_net) {
    updateTargetModel();
  }
}

void GasTrainer::doUpdate(
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

  if (isCuda) {
    notTerminal = notTerminal.to(torch::kCUDA);
    totValueLoss = totValueLoss.to(torch::kCUDA);
    totMeanQ = totMeanQ.to(torch::kCUDA);
    totRegLoss = totRegLoss.to(torch::kCUDA);
  }

  BatchedFrame lastFrame;
  if (FLAGS_use_target_net) {
    // can do only last state with target net because non-recurrent
    std::vector<std::shared_ptr<SyncFrame>> targetFrame(seq.end() - 1, seq.end());
    computeAllForwardModel(targetModel_, targetFrame, batchSize, notTerminal);
    lastFrame = *std::static_pointer_cast<BatchedFrame>(targetFrame.back());
  } else {
    computeAllForwardModel(model_, seq, batchSize, notTerminal);
    lastFrame = *std::static_pointer_cast<BatchedFrame>(seq.back());
  }

  if (FLAGS_use_target_net) {
    computeAllForwardModel(model_, seq, batchSize, notTerminal);
  }

  std::vector<torch::Tensor> allTargets;
  if (FLAGS_q_learn) {
    auto lastQs = lastFrame.forwarded_state.getDict()[kAllQKey];
    if (FLAGS_double_q) {
      //max current model
      //gather on target model
      auto lastFrameNonTarget = std::static_pointer_cast<BatchedFrame>(seq.back());
      auto lastQsCurrent = lastFrameNonTarget->forwarded_state.getDict()[kAllQKey];
      for (int lod = 0; lod <= FLAGS_max_lod; lod++) {
        auto maxActionsCurrent =
            std::get<1>(lastQsCurrent[lod].max(2)).unsqueeze(2);
        allTargets.push_back(lastQs[lod]
                                 .gather(2, maxActionsCurrent)
                                 .squeeze(2)
                                 .detach()
                                 .set_requires_grad(false));
      }
    } else {
      //max target model
      for (int lod = 0; lod <= FLAGS_max_lod; lod++) {
        allTargets.push_back(
            std::get<0>(lastQs[lod].max(2)).detach().set_requires_grad(false));
      }
    }
  } else {
    // SARSA
    // gather on target model
  }
  // loop targets and max over them, or just get highest res
  for (int lod = 0; lod <= FLAGS_max_lod; lod++) {
    if (!FLAGS_iql) {
      // not independent. for now, try VDN (mean value over groups)
      allTargets[lod] = allTargets[lod].mean(1).unsqueeze(1);
    }
  }
  VLOG(3) << "targets " << allTargets;

  std::vector<torch::Tensor> maxedTargets;
  if (FLAGS_gas_max_targets) {
    maxedTargets.push_back(allTargets[0]);
    for (int lod = 1; lod <= FLAGS_max_lod; lod++) {
      maxedTargets.push_back(torch::max(maxedTargets[lod - 1], allTargets[lod]));
    }
  } else {
    maxedTargets = allTargets;
  }

  common::assertSize("notterminal", notTerminal, {returnsLength_, batchSize});
  for (int i = (int)seq.size() - 2; i >= 0; --i) {
    if (FLAGS_recurrent_burnin > 0 && i < FLAGS_recurrent_burnin) {
      break;
    }
    auto currentFrame = std::static_pointer_cast<BatchedFrame>(seq[i]);
    ag::Variant& currentOut = currentFrame->forwarded_state;
    // B x G
    std::vector<torch::Tensor> currentQ;
    for (uint64_t lod = 0; lod <= FLAGS_max_lod; lod++) {
      currentQ.push_back(currentOut.getDict()[kAllQKey]
                             .getTensorList()[lod]
                             .gather(2, currentFrame->action.unsqueeze(2))
                             .squeeze(2));
      if (!FLAGS_iql) {
        currentQ[lod] = currentQ[lod].mean(1).unsqueeze(1);
      }
    }

    auto currentNotTerminal = notTerminal[i].unsqueeze(1);
    auto currentReward = currentFrame->reward.unsqueeze(1);
    // break the chain for terminal state, otherwise decay
    for (auto& target : maxedTargets) {
      target = (target * discount_ * currentNotTerminal) + currentReward;
    }
    int firstTrainLvl = FLAGS_only_train_max_lod ? maxedTargets.size()-1 : 0;
    auto frameLod = currentFrame->state.getDict()[kStateKey][kLodKey];
    for (int i=firstTrainLvl; i<maxedTargets.size(); i++) {
      auto lodTaken = frameLod.eq(i).to(torch::kFloat);
      auto mask = FLAGS_on_actionspace ? lodTaken : frameLod.le(i).to(torch::kFloat);
      auto valueLoss = at::smooth_l1_loss(currentQ[i]*mask, maxedTargets[i]*mask);
      torch::Tensor meanQ;
      if (lodTaken.sum().gt(0).item<uint8_t>()) {
        meanQ = (currentQ[i] * lodTaken).sum() / lodTaken.sum();
        totMeanQ = totMeanQ + meanQ;
      }
      totValueLoss = totValueLoss + valueLoss;
    }
  }
  totValueLoss /= (float)(seq.size() - 1);
  totMeanQ /= (float)(seq.size() - 1);
  totRegLoss /= (float)(seq.size() - 1);
  if (!FLAGS_only_train_max_lod) {
    totValueLoss /= (float)maxedTargets.size();
    totRegLoss /= (float)maxedTargets.size();
  }
  if (getUpdateCount() % 10 == 0) {
    metricsContext_->pushEvent("value_loss", totValueLoss.item<float>());
    metricsContext_->pushEvent("reg_loss", totRegLoss.item<float>());
    metricsContext_->pushEvent("q_taken", totMeanQ.item<float>());
    metricsContext_->pushEvent("batch_size", batchSize);
  }
  totValueLoss = totValueLoss + totRegLoss * FLAGS_delta_reg_coef;
  VLOG(2) << "loss " << totValueLoss.item<float>();
  totValueLoss.backward();
  doOptimStep();

  if (FLAGS_use_target_net &&
      (getUpdateCount() - lastUpdatedTargetT_ >=
       FLAGS_target_update_interval)) {
    VLOG(0) << "updating target net after " << getUpdateCount() << "updates "
            << " last at " << lastUpdatedTargetT_;
    updateTargetModel();
    lastUpdatedTargetT_ = getUpdateCount();
  }
}

void GasTrainer::step(
    EpisodeHandle const& handle,
    std::shared_ptr<ReplayBufferFrame> v,
    bool isDone) {
  auto& uid = handle.gameID();
  auto& k = handle.episodeKey();
  {
    std::lock_guard<priority_mutex> lk(stepMutex_);
    auto frame = std::static_pointer_cast<SingleFrame>(v);
    if (cumRewards_.count(uid) == 0) {
      cumRewards_[uid] = 0;
    }
    cumRewards_[uid] += frame->reward;
  }
  SyncTrainer::step(handle, v, isDone);
}

void GasTrainer::updateTargetModel() {
  targetModel_ = ag::clone(model_);
}

float GasTrainer::getEpsilon() {
  if (!FLAGS_epsilon_per_thread) {
    return FLAGS_epsilon_min +
        std::max(
               0.0,
               (FLAGS_epsilon_max - FLAGS_epsilon_min) *
                   (1.0 -
                    ((float)getUpdateCount() /
                     (float)FLAGS_epsilon_decay_length)));
  }
  else {
     std::thread::id this_id = std::this_thread::get_id();
     if (threadIdMap_.find(this_id) == threadIdMap_.end()) {
       std::lock_guard<std::mutex> updateLock(threadIdNumMutex_);
       {
         threadIdMap_[this_id] = nextThreadi_;
         nextThreadi_ ++;
       }
     }
     int i = threadIdMap_[this_id];
    return pow(FLAGS_epsilon_max, 1.0 + FLAGS_alpha*(float(i)/(FLAGS_num_threads -1)));
  }
}

void GasTrainer::updateBestMetric(float metric) {
  if (metric > lastBestMetric_) {
    lastBestMetric_ = metric;
    lastBestLod_ = curLod_;
    lastBestUpdate_ = getUpdateCount();
    VLOG(0) << "new best metric " << lastBestMetric_;
  } else if ((getUpdateCount() > (lastBestUpdate_ + FLAGS_gas_on_plateau)) &&
             (curLod_ == lastBestLod_)) {
    if (curLod_ < FLAGS_max_lod) {
      curLod_++;
      VLOG(0) << "no improvement on plateau at " << lastBestMetric_ 
        << ", increasing lod to " << curLod_;
    }
  }
}

float GasTrainer::getLod() {
  if (FLAGS_only_train_max_lod) {
    return (float)FLAGS_max_lod;
  } else if (FLAGS_gas_on_plateau > 0) {
    return curLod_;
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
