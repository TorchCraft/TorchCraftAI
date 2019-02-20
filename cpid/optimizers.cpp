/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "optimizers.h"
#include <memory>

DEFINE_string(optim, "sgd", "Optimiser: sgd|rmsprop|adam|adagrad");
DEFINE_double(lr, 0.1, "Learning rate");
DEFINE_double(weight_decay, 0, "Weight decay for optimizers");
DEFINE_double(momentum, 0, "Momentum, only for RMSprop and SGD");
DEFINE_double(optim_eps, 1e-8, "epsilon for adam and RMSprop");

// adagrad
DEFINE_double(adagrad_lr_decay, 0, "Adagrad learning rate decay");

// adam
DEFINE_double(adam_beta1, 0.9, "adam beta1");
DEFINE_double(adam_beta2, 0.999, "adam beta2");
DEFINE_bool(adam_amsgrad, false, "adam amsgrad correction");

// rmsprop
DEFINE_double(rmsprop_alpha, 0.99, "rmsprop alpha");
DEFINE_bool(rmsprop_centered, false, "rmsprop centered");

// sgd
DEFINE_double(sgd_dampening, 0, "sgd dampening");
DEFINE_bool(sgd_nesterov, false, "sgd nesterov momentum");

namespace cpid {

ag::Optimizer selectOptimizer(std::shared_ptr<torch::nn::Module> module) {
  using namespace torch::optim;
  auto optimizerName = FLAGS_optim;
  auto params = module->parameters();
  if (optimizerName == "sgd") {
    return std::make_shared<SGD>(
        params,
        SGDOptions(FLAGS_lr)
            .weight_decay(FLAGS_weight_decay)
            .momentum(FLAGS_momentum)
            .dampening(FLAGS_sgd_dampening)
            .nesterov(FLAGS_sgd_nesterov));
  } else if (optimizerName == "rmsprop") {
    return std::make_shared<RMSprop>(
        params,
        RMSpropOptions(FLAGS_lr)
            .weight_decay(FLAGS_weight_decay)
            .momentum(FLAGS_momentum)
            .alpha(FLAGS_rmsprop_alpha)
            .centered(FLAGS_rmsprop_centered)
            .eps(FLAGS_optim_eps));
  } else if (optimizerName == "adam") {
    return std::make_shared<Adam>(
        params,
        AdamOptions(FLAGS_lr)
            .weight_decay(FLAGS_weight_decay)
            .eps(FLAGS_optim_eps)
            .beta1(FLAGS_adam_beta1)
            .beta2(FLAGS_adam_beta2)
            .amsgrad(FLAGS_adam_amsgrad));
  } else if (optimizerName == "adagrad") {
    return std::make_shared<Adagrad>(
        params,
        AdagradOptions(FLAGS_lr)
            .weight_decay(FLAGS_weight_decay)
            .lr_decay(FLAGS_adagrad_lr_decay));
  }
  throw std::runtime_error(
      std::string("Unrecognized optimizer: " + optimizerName));
}

std::map<std::string, std::string> optimizerFlags() {
  std::map<std::string, std::string> flags;
  std::vector<gflags::CommandLineFlagInfo> config;
  gflags::GetAllFlags(&config);
  for (auto const& c : config) {
    if (c.filename == __FILE__) {
      flags[c.name] = c.current_value;
    }
  }
  return flags;
}

} // namespace cpid
