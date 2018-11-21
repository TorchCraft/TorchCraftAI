/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "modules.h"

#include "models/bos/models.h"

#include "state.h"

#include <common/autograd/utils.h>
#include <common/rand.h>

#include <fmt/format.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <prettyprint/prettyprint.hpp>

DECLARE_string(build); // from strategy.cpp

DEFINE_int32(
    exp_bos_interval,
    5 * 24,
    "Interval for BOS model inference in frames");
DEFINE_string(
    exp_bos_model,
    "bwapi-data/AI/exp_bos_model.bin",
    "Path to build order switch model");
DEFINE_string(
    exp_bos_start,
    "5",
    "Game time from which on BOS will be effective, in minutes (firstenemy|N)");
DEFINE_bool(
    exp_bos_start_vs_rush,
    false,
    "Whether to start BOS when proxies or rushes are detected");
DEFINE_double(
    exp_bos_min_advantage,
    cherrypi::kfEpsilon,
    "Threshold for switching to a more advantegeous build");

namespace cherrypi {

REGISTER_SUBCLASS_0(Module, BosModule);

bool isEnabled(State* state) {
  if (!state->board()->hasKey(Blackboard::kBuildOrderSwitchEnabledKey)) {
    return true;
  }
  return state->board()->get<bool>(Blackboard::kBuildOrderSwitchEnabledKey);
}

void BosModule::onGameStart(State* state) {
  nextSelectionFrame_ = 0;
  sawEnoughEnemyUnits_ = false;
  startTime_ = 0.0f;
  if (FLAGS_exp_bos_start != "firstenemy") {
    startTime_ = std::stof(FLAGS_exp_bos_start) * 60;
  }

  output_ = ag::Variant();
  nextForwardFrame_ = 0;
  ag::Container model;
  try {
    model = bos::modelMakeFromCli();
  } catch (std::exception const& ex) {
    LOG(WARNING) << "Error constructing BOS model: " << ex.what();
    model = nullptr;
  }

  if (model) {
    try {
      ag::load(FLAGS_exp_bos_model, model);
      if (common::gpuAvailable()) {
        model->to(torch::kCUDA);
      }
      model->eval();
    } catch (std::exception const& ex) {
      LOG(WARNING) << "Error loading BOS model from " << FLAGS_exp_bos_model
                   << ": " << ex.what();
      model = nullptr;
    }
  }

  if (model) {
    try {
      runner_ = bos::makeModelRunner(model, FLAGS_bos_model_type);
    } catch (std::exception const& ex) {
      LOG(WARNING) << "Error constructing BOS model runner: " << ex.what();
      runner_ = nullptr;
      model = nullptr;
    }
  }

  // For now, disbale BOS on random race opponents as we haven't seen them
  // during training. A workaround would be to buffer all samples and then do
  // the remaining forwards
  auto race = state->board()->get<int>(Blackboard::kEnemyRaceKey);
  switch (race) {
    case +tc::BW::Race::Zerg:
    case +tc::BW::Race::Terran:
    case +tc::BW::Race::Protoss:
      canRunBos_ = true;
      break;
    default:
      canRunBos_ = false;
      VLOG(0) << "Disabling BOS against opponent playing "
              << tc::BW::Race::_from_integral(race)._to_string();
      break;
  }
}

void BosModule::step(State* state) {
  if (!isEnabled(state)) {
    return;
  }
  if (!canRunBos_) {
    return;
  }
  if (state->currentFrame() >= nextForwardFrame_) {
    output_ = forward(state);
    nextForwardFrame_ = state->currentFrame() + FLAGS_exp_bos_interval;
  }

  if (!sawEnoughEnemyUnits_) {
    auto& enemies = state->unitsInfo().enemyUnits();
    // We saw more than one enemy unit
    sawEnoughEnemyUnits_ |= enemies.size() > 1;
    // We saw a non-worker unit
    sawEnoughEnemyUnits_ |=
        (enemies.size() > 0 && !enemies.front()->type->isWorker);
  }

  if (FLAGS_exp_bos_start == "firstenemy") {
    if (!sawEnoughEnemyUnits_) {
      return;
    }
  } else if (state->currentGameTime() < startTime_) {
    if (FLAGS_exp_bos_start_vs_rush) {
      for (Unit* u : state->unitsInfo().enemyUnits()) {
        // If they proxy or attack, start bos immediately
        if (!u->type->isWorker && !u->type->supplyProvided &&
            !u->type->isRefinery) {
          float baseDistance = kfInfty;
          for (Position pos :
               state->areaInfo().candidateEnemyStartLocations()) {
            float d = state->areaInfo().walkPathLength(u->pos(), pos);
            if (d < baseDistance) {
              baseDistance = d;
            }
          }
          float myBaseDistance = state->areaInfo().walkPathLength(
              u->pos(), state->areaInfo().myStartLocation());
          if (myBaseDistance < baseDistance * 2.0f) {
            startTime_ = state->currentGameTime();
            break;
          }
        }
      }
    }
    if (state->currentGameTime() < startTime_) {
      return;
    }
  }

  if (state->currentFrame() >= nextSelectionFrame_) {
    auto curBuild =
        state->board()->get<std::string>(Blackboard::kBuildOrderKey);
    try {
      std::string build = selectBuild(state);
      if (!build.empty()) {
        if (build == curBuild) {
          VLOG(0) << "Keeping build " << build;
        } else {
          VLOG(0) << "Switching builds from " << curBuild << " to " << build;
          state->board()->post(Blackboard::kBuildOrderKey, build);
        }
      }
    } catch (std::exception const& ex) {
      VLOG(0) << "Error selecting build, keeping build " << curBuild;
    }

    nextSelectionFrame_ = state->currentFrame() + FLAGS_exp_bos_interval;
  }
}

ag::Variant BosModule::forward(State* state) {
  if (runner_ == nullptr) {
    return {};
  }

  torch::NoGradGuard ng;
  auto output = runner_->forward(runner_->takeSample(state));
  if (VLOG_IS_ON(1)) {
    auto heads = output["vHeads"].squeeze().to(at::kCPU);
    auto probs = std::map<std::string, float>();
    for (auto const& it : bos::buildOrderMap()) {
      auto p = heads[it.second].item<float>();
      if (p > 0.0f) {
        probs[it.first] = p;
      }
    }
    VLOG(1) << probs;
  }
  return output;
}

std::string BosModule::selectBuild(State* state) {
  if (!output_.isDict()) {
    return {};
  }

  auto build = output_.getDict()["build"].getString();
  auto buildPrefix = bos::addRacePrefix(
      build, state->board()->get<int>(Blackboard::kEnemyRaceKey));
  auto pbuild = output_["pwin"].item<float>();
  auto adv = output_["advantage"].item<float>();
  if (adv <= 0) {
    return {};
  }
  if (adv < FLAGS_exp_bos_min_advantage) {
    VLOG(1) << fmt::format(
        "Advantage of {} {} too small, current value {}",
        buildPrefix,
        adv,
        pbuild - adv);
    return {};
  }

  VLOG(0) << fmt::format(
      "Selected {} with v {} A {}", buildPrefix, pbuild, adv);
  return build;
}

} // namespace cherrypi
