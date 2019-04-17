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

class ABBOtvpjoyorush : public ABBOBase {
 public:
  using ABBOBase::ABBOBase;

  bool readyToAttack = false;
  virtual void preBuild2(autobuild::BuildState& bst) override {
    readyToAttack =
        readyToAttack || countUnits(bst, Terran_Siege_Tank_Tank_Mode) > 2;
    postBlackboardKey(Blackboard::kMinScoutFrameKey, 24 * 2 * 60);
    postBlackboardKey("TacticsAttack", readyToAttack);
  }

  virtual void buildStep2(autobuild::BuildState& bst) override {
    // Crude follow-up
    buildN(Terran_Barracks, 4 * bases);
    buildN(Terran_Machine_Shop, 2 * bases);
    buildN(Terran_Factory, 2 * bases);
    upgrade(Terran_Vehicle_Weapons_3) && upgrade(Terran_Vehicle_Plating_3);
    upgrade(U_238_Shells);
    upgrade(Terran_Infantry_Weapons_3) && upgrade(Terran_Infantry_Armor_3);
    buildN(
        Terran_Command_Center, 1 + countUnits(bst, Terran_SCV) / 18, nextBase);
    buildN(Terran_Barracks, 2);
    buildN(Terran_Factory, 3);
    buildN(Terran_Command_Center, 2, nextBase);

    // JoyO rush vs. Protoss: https://liquipedia.net/starcraft/JoyO_Rush
    // Selected because it doesn't require much Terran-specific micro skill
    build(Terran_Siege_Tank_Tank_Mode);
    buildN(Terran_Machine_Shop, 2);
    buildN(
        Terran_Vulture,
        enemyZealotCount > 0 && !has(bst, Terran_Machine_Shop) ? 1 : 0);
    buildN(Terran_SCV, 22 * bases);
    build(Terran_Marine);
    buildN(Terran_Factory, 2);
    buildN(Terran_SCV, 19);
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

REGISTER_SUBCLASS_3(ABBOBase, ABBOtvpjoyorush, UpcId, State*, Module*);
} // namespace cherrypi
