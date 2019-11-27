/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#include "features/jitter.h"
#include "features/unitsfeatures.h"

namespace cherrypi {

struct SimpleUnitFeaturizer : UnitAttributeFeaturizer {
  static int kNumChannels;

  SimpleUnitFeaturizer();

  static void init();

 protected:
  virtual void extractUnit(TensorDest, Unit*) override;

  int mapType(int unitType) const {
    return typemap_->at(unitType);
  }
  int unmapType(int mappedType) const {
    return itypemap_->at(mappedType);
  }
  std::array<int, 234>* typemap_;
  std::array<int, 234>* itypemap_;
};

} // namespace cherrypi
