/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "base.h"

namespace cherrypi {

using namespace buildtypes;
using namespace autobuild;

class ABBOtvtz2portwraith : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  virtual void preBuild2(autobuild::BuildState& bst) override {}

  virtual void buildStep2(autobuild::BuildState& bst) override {
    build(Terran_Barracks);
    build(Terran_Marine);
    buildN(
        Terran_Command_Center,
        1 + countPlusProduction(bst, Terran_SCV) / 16,
        nextBase);
    buildN(Terran_Starport, 2 * bases);
    build(Terran_Wraith);
    if (countPlusProduction(bst, Terran_Wraith) > 24) {
      build(Terran_Battlecruiser);
    }
    buildN(Terran_Starport, 2);
    build(Terran_Vulture);
    buildN(Terran_SCV, std::min(85, 22 * bases));
    buildN(Terran_Factory, 1);
    buildN(Terran_SCV, 16);
    buildN(Terran_Supply_Depot, 2);
    buildN(Terran_SCV, 13);
    buildN(Terran_Refinery, bases);
    buildN(Terran_SCV, 12);
    buildN(Terran_Barracks, 1);
    buildN(Terran_SCV, 11);
    buildN(Terran_Supply_Depot, 1);
    buildN(Terran_SCV, 9);
  }
};

REGISTER_SUBCLASS_3(ABBOBase, ABBOtvtz2portwraith, UpcId, State*, Module*);
} // namespace cherrypi
