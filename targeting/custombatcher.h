/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#include "cpid/batcher.h"

class CustomBatcher : public cpid::AsyncBatcher {
 public:
  CustomBatcher(
      ag::Container model,
      int batchsize,
      int padValue = -1,
      bool stripOutput = true);

  std::vector<ag::Variant>
  unBatch(const ag::Variant& out, bool stripOutput, double stripValue) override;

  ag::Variant makeBatch(const std::vector<ag::Variant>& queries, double padValue = -1) override;
};
