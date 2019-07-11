/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "model.h"

namespace microbattles {

class DummyModel : public ag::Container_CRTP<DummyModel>,
                   public PFMicroActionModel {
 public:
  void reset() override{};
  ag::Variant forward(ag::Variant inp) override {
    return ag::tensor_list();
  }
  virtual std::shared_ptr<MicroFeaturizer> getFeaturizer() override {
    return std::make_shared<MicroFeaturizer>();
  };
  std::vector<PFMicroAction>
  decodeOutput(cherrypi::State*, ag::Variant, ag::Variant) override;
};

} // namespace microbattles
