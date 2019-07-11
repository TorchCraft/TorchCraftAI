/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "micromodule.h"
#include "modules.h"
#include "modules/squadcombat.h"
#include "rule_module.h"

DEFINE_string(
    defiler_behavior,
    "",
    "Which MicroBehaviors to use for Defiler: model|rules|noop|{empty}");

DEFINE_bool(
    defiler_fill,
    false,
    "Fill Defiler energy at the start of each episode");

DEFINE_bool(
    defiler_refill,
    false,
    "Automatically refill Defiler energy each frame");

namespace microbattles {

namespace {
class SquadDefilerRuleHelperModule : public cherrypi::SquadCombatModule {
  virtual cherrypi::BehaviorList makeDeleteBehaviors() override {
    using namespace cherrypi;

    auto behaviors = SquadCombatModule::makeDeleteBehaviors();

    if (FLAGS_defiler_behavior == "model") {
      squadcombat::removeAll<BehaviorAsDefiler>(behaviors);
      squadcombat::insertBefore<BehaviorFormation>(
          behaviors, std::make_shared<BehaviorAsDefilerConsumeOnly>());
    } else if (FLAGS_defiler_behavior == "noop") {
      squadcombat::removeAll<BehaviorML>(behaviors);
      squadcombat::removeAll<BehaviorAsDefiler>(behaviors);
      squadcombat::insertBefore<BehaviorFormation>(
          behaviors, std::make_shared<BehaviorAsDefilerConsumeOnly>());
    } else if (FLAGS_defiler_behavior != "rules") {
      throw std::runtime_error(
          std::string("Unexpected Defiler behaviors: ") +
          FLAGS_defiler_behavior);
    }

    return behaviors;
  }
};
} // namespace

// This function is used to load defiler micro module inside SquadCombat
auto addFullGameDefilerModules(
    std::shared_ptr<MicroModule> defilerMicroModule) {
  using namespace cherrypi;

  auto refillDefilersThisFrame = [](State* state) {
    for (auto& defiler :
         state->unitsInfo().myUnitsOfType(buildtypes::Zerg_Defiler)) {
      state->board()->postCommand(
          torchcraft::Client::Command(
              torchcraft::BW::Command::CommandOpenbw,
              torchcraft::BW::OpenBWCommandType::SetUnitEnergy,
              defiler->id,
              200),
          cp::kRootUpcId);
    }
  };

  auto refillDefilersOnce = [&refillDefilersThisFrame](State* state) {
    constexpr auto kFilledDefilersOnce = "FilledDefilersOnce";
    if (!state->board()->hasKey(kFilledDefilersOnce) &&
        !state->unitsInfo().myUnitsOfType(buildtypes::Zerg_Defiler).empty()) {
      state->board()->post(kFilledDefilersOnce, 1);
      refillDefilersThisFrame(state);
    }
  };

  std::vector<std::shared_ptr<Module>> modules;

  auto squadCombat = FLAGS_defiler_rule
      ? Module::make<SquadCombatModule>()
      : Module::make<SquadDefilerRuleHelperModule>();

  if (!FLAGS_defiler_rule) {
    squadCombat->enqueueModel(defilerMicroModule, "defilerModel");
  }

  modules.push_back(Module::make<DummyTacticsModule>());
  modules.push_back(squadCombat);

  if (FLAGS_defiler_fill) {
    modules.push_back(
        std::make_shared<LambdaModule>(refillDefilersOnce, "FillDefilers"));
  }
  if (FLAGS_defiler_refill) {
    modules.push_back(std::make_shared<LambdaModule>(
        refillDefilersThisFrame, "RefillDefilers"));
  }

  return modules;
}

} // namespace microbattles