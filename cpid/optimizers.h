/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <autogradpp/autograd.h>
#include <gflags/gflags.h>

DECLARE_string(optim);
DECLARE_double(lr);
DECLARE_double(weight_decay);
DECLARE_double(momentum);
DECLARE_double(optim_eps);

// adagrad
DECLARE_double(adadgrad_lr_decay);

// adam
DECLARE_double(adam_beta1);
DECLARE_double(adam_beta2);
DECLARE_bool(adam_amsgrad);

// rmsprop
DECLARE_double(rmsprop_alpha);
DECLARE_bool(rmsprop_centered);

// sgd
DECLARE_double(sgd_dampening);
DECLARE_bool(sgd_nesterov);

// Although this header defines optimizers and flags for you, you can always
// set defaults for your own script, via this idiom at the beginning of main:
//  FLAGS_lr = 1e-3;
//  gflags::ParseCommandLineFlags(&argc, &argv, true);

namespace cpid {

ag::Optimizer selectOptimizer(std::shared_ptr<torch::nn::Module>);

std::map<std::string, std::string> optimizerFlags();

} // namespace cpid
