/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "model.h"
#include "common.h"
#include "features/features.h"
#include "features/unitsfeatures.h"
#include "state.h"

namespace microbattles {

std::vector<torch::Tensor> MicroFeaturizer::featurize(cherrypi::State* state) {
  torch::NoGradGuard guard;
  auto unitFeaturizer = cherrypi::UnitStatFeaturizer();
  auto myUnitFeatures =
      unitFeaturizer.extract(state, state->unitsInfo().myUnits());
  auto nmyUnitFeatures =
      unitFeaturizer.extract(state, state->unitsInfo().enemyUnits());

  auto mapFeatures = featurizePlain(
      state,
      {cherrypi::PlainFeatureType::Walkability, // THIS SHOULD ALWAYS BE FIRST,
                                                // we rely on it in modelpf.cpp
       cherrypi::PlainFeatureType::Buildability,
       cherrypi::PlainFeatureType::OneHotGroundHeight,
       cherrypi::PlainFeatureType::FogOfWar},
      cherrypi::Rect(
          {-mapOffset(), -mapOffset()},
          {kMapHeight + mapOffset(), kMapWidth + mapOffset()}));
  auto mesh =
      at::stack(
          {torch::arange(0, kMapHeight, defaultDevice()).repeat({kMapWidth, 1}),
           torch::arange(0, kMapWidth, defaultDevice())
               .repeat({kMapHeight, 1})
               .t()},
          0)
          .toType(at::kFloat)
          .div_(512);
  auto xygrid = -1 *
      torch::ones({2, kMapHeight + mapPadding(), kMapWidth + mapPadding()});
  xygrid.slice(1, mapOffset(), kMapHeight + mapOffset())
      .slice(2, mapOffset(), kMapHeight + mapOffset())
      .copy_(mesh);
  auto mapTensor = torch::cat({mapFeatures.tensor, xygrid}, 0);
  assert(mapTensor.size(0) == kMapFeatures);
  return {
      mapTensor,
      myUnitFeatures.positions,
      myUnitFeatures.data,
      nmyUnitFeatures.positions,
      nmyUnitFeatures.data,
  };
}

int MicroFeaturizer::kNumUnitChannels =
    cherrypi::UnitStatFeaturizer::kNumChannels;

} // namespace microbattles
