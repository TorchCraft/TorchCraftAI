/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "parameters.h"
#include "common/rand.h"
#include "cpid/distributed.h"
#include "cpid/optimizers.h"
#include "flags.h"
#include <cmath>
#include <random>

using namespace cpid;
std::unordered_map<std::string, int> Parameters::int_params;
std::unordered_map<std::string, float> Parameters::float_params;

void Parameters::init() {
  if (distributed::globalContext()->rank == 0) {
    float_params["lr"] = FLAGS_lr;
    float_params["policy_ratio"] = FLAGS_policy_ratio;
    float_params["sigma"] = FLAGS_sigma;
    int_params["correlated_steps"] = FLAGS_correlated_steps;
  } else {
    std::uniform_real_distribution<float> lr(-6, -4);
    float_params["lr"] = pow(10., common::Rand::sample(lr));
    std::uniform_real_distribution<float> pr(-3, 3);
    float_params["policy_ratio"] = pow(10., common::Rand::sample(pr));
    std::uniform_real_distribution<float> sig(0.01, 3);
    float_params["sigma"] = common::Rand::sample(sig);
    std::uniform_int_distribution<int> cs(1, 10);
    int_params["correlated_steps"] = common::Rand::sample(cs);
  }
}

void Parameters::broadcast(int rank) {
  for (auto& p : float_params) {
    distributed::broadcast(&float_params[p.first], 1, rank).wait();
  }
  for (auto& p : int_params) {
    distributed::broadcast(&int_params[p.first], 1, rank).wait();
  }
}

void Parameters::perturbate() {
  int rank = distributed::globalContext()->rank;
  if (rank == 0) {
    return;
  }
  if (rank == 1) {
    // we always want to have one worker that decreases the lr, with all other
    // params fixed, to avoid divergence
    float_params["lr"] *= 0.8;
  } else {
    float coeffs[3] = {0.8, 1, 1.2};
    std::uniform_int_distribution<int> choice(0, 2);
    for (auto& p : float_params) {
      float_params[p.first] *= coeffs[common::Rand::sample(choice)];
    }

    int int_coeffs[3] = {-1, 0, 1};
    for (auto& p : int_params) {
      int_params[p.first] += int_coeffs[common::Rand::sample(choice)];
      int_params[p.first] = std::max(1, int_params[p.first]);
    }
  }
}

float Parameters::get_float(const std::string& key) {
  return float_params.at(key);
}

int Parameters::get_int(const std::string& key) {
  return int_params.at(key);
}
