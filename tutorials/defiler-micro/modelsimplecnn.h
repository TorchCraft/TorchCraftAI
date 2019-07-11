/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "common.h"
#include "model.h"

#include <autogradpp/autograd.h>

namespace microbattles {

class SimpleCNNModel : public ag::Container_CRTP<SimpleCNNModel>,
                       public PFMicroActionModel {
 public:
  void reset() override;
  ag::Variant forward(ag::Variant inp) override;
  virtual std::shared_ptr<MicroFeaturizer> getFeaturizer() override {
    return std::make_shared<MicroFeaturizer>();
  };
  std::vector<PFMicroAction>
  decodeOutput(cherrypi::State*, ag::Variant, ag::Variant) override;

 protected:
  /// Embeds unit features
  ag::Container units_;

  /// Convolves the (unit + map) features
  std::vector<ag::Container> convLayers_;

  /// "Attack" command head
  /// Provides a value for each friendly unit attacking each enemy unit
  ag::Container attacks_;

  /// "Move" command head
  /// Provides a value for each friendly unit moving to possible positions
  ag::Container moves_;

  /// Action type head
  /// Chooses between the commands like Attack and Move
  ag::Container actions_;
};

} // namespace microbattles
