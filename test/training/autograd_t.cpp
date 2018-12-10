/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"
#include <glog/logging.h>

#include <autogradpp/autograd.h>

CASE("autograd/test_if_it_works") {
  // Most of the tests belong in the autograd repo, this is just to see
  // if it builds and links correctly
  torch::randn({2, 3, 5, 5}).size(0);
  torch::randn({2, 3, 5, 5}).set_requires_grad(true).size(0);
  auto model = ag::Conv2d(3, 2, 3).stride(2).make();
  auto x = torch::randn({2, 3, 5, 5}).set_requires_grad(true);
  auto y = model->forward({x})[0];
  auto s = y.sum();

  s.backward();
  EXPECT(model->impl_->weight.grad().norm().item<float>() > 0);
}

CASE("autograd/variant_ref/dict") {
  ag::Variant test = ag::VariantDict{{"key", torch::zeros({4, 5})}};
  EXPECT(test["key"].size(0) == 4);
  EXPECT(test["key"].size(1) == 5);
  EXPECT(test["key"].view(-1).size(0) == 20);
  test["key"].fill_(1);
  EXPECT(test["key"].sum().item<int32_t>() == 20);
}

CASE("autograd/variant_ref/dict/singleton_list") {
  ag::Variant test =
      ag::VariantDict{{"key", ag::Variant{torch::zeros({4, 5})}}};
  EXPECT(test["key"].size(0) == 4);
  EXPECT(test["key"].size(1) == 5);
  EXPECT(test["key"].view(-1).size(0) == 20);
  test["key"].fill_(1);
  EXPECT(test["key"].sum().item<int32_t>() == 20);
}

CASE("autograd/variant_ref/list") {
  ag::Variant test = ag::Variant{torch::zeros({4, 5})};
  EXPECT(test[0].size(0) == 4);
  EXPECT(test[0].size(1) == 5);
  EXPECT(test[0].view(-1).size(0) == 20);
  test[0].fill_(1);
  EXPECT(test[0].sum().item<int32_t>() == 20);
}

CASE("autograd/variant/dict/insert") {
  ag::Variant test = ag::VariantDict{{"key", torch::zeros({4, 5})}};
  EXPECT(test.getDict().size() == 1u);
  test["key"] = torch::zeros({1, 2});
  EXPECT(test.getDict().size() == 1u);
  test["key2"] = torch::zeros({1, 2});
  EXPECT(test.getDict().size() == 2u);
  EXPECT(test["key3"].defined() == false);
}

CASE("autograd/variant/list/oob") {
  ag::Variant test =
      ag::tensor_list{torch::zeros({4, 5}), torch::zeros({1, 2})};
  EXPECT_THROWS(test[2]);
  EXPECT_THROWS(test[10]);
}

CASE("autograd/yay_cuda_is_working") {
  torch::randn({100}, torch::kCUDA).sum();
}
