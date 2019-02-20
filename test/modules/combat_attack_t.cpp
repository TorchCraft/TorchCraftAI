/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <cstdlib>
#include <fstream>

#include "gameutils/scenario.h"
#include "test.h"

#include <glog/logging.h>
#include <nlohmann/json.hpp>

#include "cherrypi.h"
#include "common/rand.h"
#include "modules.h"
#include "player.h"
#include "utils.h"

using namespace cherrypi;
using nlohmann::json;

DECLARE_double(rtfactor);

namespace {

class MockTacticsModule : public Module {
 public:
  MockTacticsModule() : Module() {}
  void step(State* state) override {
    auto board = state->board();
    if (state->currentFrame() == 9) {
      VLOG(0) << " My units: "
              << utils::unitsString(state->unitsInfo().myUnits());
      VLOG(0) << " Their units: "
              << utils::unitsString(state->unitsInfo().enemyUnits());
    }
    if (board->hasKey("target_posted") && board->get<bool>("target_posted")) {
      return;
    }

    // Post UPC for attacking enemy start location with all units
    auto units = utils::filterUnits(
        state->unitsInfo().myUnits(),
        [](Unit const* u) { return u->active() && !u->type->isBuilding; });
    if (units.size() == 0) {
      return;
    }

    UPCTuple::UnitMap map;
    for (Unit* e : state->unitsInfo().enemyUnits()) {
      map[e] = 1;
    }

    postUpc(state, 1, units, map);
    board->post("target_posted", true);
  }

  void postUpc(
      State* state,
      int srcUpcId,
      std::vector<Unit*> const& units,
      UPCTuple::UnitMap targets) {
    auto upc = std::make_shared<UPCTuple>();
    for (Unit* u : units) {
      upc->unit[u] = 1.0f / units.size();
    }
    upc->position = targets;
    upc->command[Command::Delete] = 0.5;
    upc->command[Command::Move] = 0.5;

    state->board()->postUPC(std::move(upc), srcUpcId, this);
  }
};

void microScenario(
    lest::env& lest_env,
    std::string map,
    std::string race,
    void moduleFunc(Player&),
    int kMaxFrames = 100000,
    int prevMyAvg = -1,
    int prevTheirAvg = -1) {
  auto scenario = Scenario(map, race);
  Player bot(scenario.makeClient());
  bot.setRealtimeFactor(FLAGS_rtfactor);
  moduleFunc(bot);

  bot.addModule(Module::make<TopModule>());
  bot.addModule(Module::make<MockTacticsModule>());
  bot.addModule(Module::make<SquadCombatModule>());
  bot.addModule(Module::make<UPCToCommandModule>());

  bot.init();
  auto state = bot.state();
  do {
    bot.step();
  } while (!state->gameEnded() && state->currentFrame() < kMaxFrames);

  // I'm using stderr here because VLOG(0) is skipped if the test fails...
  if (prevMyAvg >= 0 && prevTheirAvg >= 0) {
    std::cerr << lest_env.testing << " >> "
              << "My/Their units left: " << state->unitsInfo().myUnits().size()
              << "/" << state->unitsInfo().enemyUnits().size()
              << ", should be approx " << prevMyAvg << "/" << prevTheirAvg
              << "\n";
  } else {
    std::cerr << lest_env.testing << " >> "
              << "My/Their units left: " << state->unitsInfo().myUnits().size()
              << "/" << state->unitsInfo().enemyUnits().size() << "\n";
  }
  EXPECT(state->currentFrame() <= kMaxFrames);
  EXPECT(!state->unitsInfo().myUnits().empty());
}

} // namespace

SCENARIO("combat/6_zerglings_vs_base") {
  microScenario(
      lest_env,
      "test/maps/6-zerglings-vs-base.scm",
      "Terran",
      [](Player& bot) {},
      5000,
      5,
      0);
}

// Scenario setup: we should be able to beat the built-in AI by burrowing
// Lurkers and blocking Marines' retreat with Zerglings
SCENARIO("combat/zerglings_lurkers_marines_medics[.dev]") {
  microScenario(
      lest_env,
      "test/maps/micro-empty.scm",
      "Zerg",
      [](Player& bot) {
        using namespace tc::BW;
        bot.addModule(OnceModule::makeWithSpawns(
            {
                {12, UnitType::Zerg_Zergling, 104, 132},
                {2, UnitType::Zerg_Lurker, 102, 132},
                {3, UnitType::Zerg_Scourge, 2, 2},
            },
            "MySpawns"));
        bot.addModule(OnceModule::makeWithEnemySpawns(
            {
                {12, UnitType::Terran_Marine, 146, 132},
                {3, UnitType::Terran_Medic, 146, 132},
                {1, UnitType::Terran_Science_Vessel, 146, 132},
            },
            "EnemySpawns"));
      },
      1000);
}

// Scenario setup: we should be able to beat the built-in AI:
// focus fire w/o spending time moving around, retreat harmed/hurt
SCENARIO("combat/zerglings_6v6[.dev]") {
  microScenario(
      lest_env,
      "test/maps/micro-empty.scm",
      "Zerg",
      [](Player& bot) {
        using namespace tc::BW;
        bot.addModule(OnceModule::makeWithSpawns(
            {
                {6, UnitType::Zerg_Zergling, 104, 132},
            },
            "MySpawns"));
        bot.addModule(OnceModule::makeWithEnemySpawns(
            {
                {6, UnitType::Zerg_Zergling, 136, 132},
            },
            "EnemySpawns"));
      },
      1000);
}

// Scenario setup: we should be able to beat the built-in AI:
// focus fire w/o spending time moving around, retreat harmed/hurt
SCENARIO("combat/split_scourge[.dev]") {
  microScenario(
      lest_env,
      "test/maps/micro-empty.scm",
      "Zerg",
      [](Player& bot) {
        using namespace tc::BW;
        bot.addModule(OnceModule::makeWithSpawns(
            {
                {15, UnitType::Zerg_Scourge, 104, 132},
                {1, UnitType::Zerg_Zergling, 104, 132},
            },
            "MySpawns"));
        bot.addModule(OnceModule::makeWithEnemySpawns(
            {
                {3, UnitType::Protoss_Arbiter, 136, 132},
            },
            "EnemySpawns"));
      },
      1000,
      4,
      0);
}

// TODO: works maybe 30% of cases right now
SCENARIO("combat/split_scourge2[.dev]") {
  microScenario(
      lest_env,
      "test/maps/micro-empty.scm",
      "Zerg",
      [](Player& bot) {
        using namespace tc::BW;
        bot.addModule(OnceModule::makeWithSpawns(
            {
                {3, UnitType::Zerg_Scourge, 104, 132},
                {1, UnitType::Zerg_Mutalisk, 104, 132},
            },
            "MySpawns"));
        bot.addModule(OnceModule::makeWithEnemySpawns(
            {
                {3, UnitType::Zerg_Mutalisk, 136, 132},
            },
            "EnemySpawns"));
      },
      1000,
      1,
      0);
}

// Scenario setup: scourge splitting.
SCENARIO("combat/mutas_6v6[.dev][0.8]") {
  microScenario(
      lest_env,
      "test/maps/micro-empty.scm",
      "Zerg",
      [](Player& bot) {
        using namespace tc::BW;
        bot.addModule(OnceModule::makeWithSpawns(
            {
                {6, UnitType::Zerg_Mutalisk, 104, 132},
            },
            "MySpawns"));
        bot.addModule(OnceModule::makeWithEnemySpawns(
            {
                {6, UnitType::Zerg_Mutalisk, 136, 132},
            },
            "EnemySpawns"));
      },
      1000,
      3,
      0);
}

// Scenario setup: we should be able to beat the built-in AI with
// kiting (move away the targetted hydralisk).
SCENARIO("combat/hydras_zealot[.dev]") {
  microScenario(
      lest_env,
      "test/maps/micro-empty.scm",
      "Zerg",
      [](Player& bot) {
        using namespace tc::BW;
        bot.addModule(OnceModule::makeWithSpawns(
            {
                {2, UnitType::Zerg_Hydralisk, 104, 132},
            },
            "MySpawns"));
        bot.addModule(OnceModule::makeWithEnemySpawns(
            {
                {1, UnitType::Protoss_Zealot, 108, 132},
            },
            "EnemySpawns"));
      },
      1000);
}

// Scenario setup: we should be able to beat the built-in AI with proper
// spreading of the zerglings to avoid too much splash
SCENARIO("combat/zerglings_tanks[hide]") {
  microScenario(
      lest_env,
      "test/maps/micro-empty.scm",
      "Zerg",
      [](Player& bot) {
        using namespace tc::BW;
        bot.addModule(OnceModule::makeWithSpawns(
            {
                {12, UnitType::Zerg_Zergling, 10, 132},
            },
            "MySpawns"));
        bot.addModule(OnceModule::makeWithEnemySpawns(
            {
                {1, UnitType::Terran_Siege_Tank_Siege_Mode, 160, 132},
                {1, UnitType::Terran_Siege_Tank_Siege_Mode, 160, 126},
                {1, UnitType::Terran_Siege_Tank_Siege_Mode, 174, 132},
                {1, UnitType::Terran_Siege_Tank_Siege_Mode, 160, 138},
            },
            "EnemySpawns"));
      },
      1000);
}

// Scenario setup: The zerglings should be able to overwhelm the m&m ball
SCENARIO("combat/zerglings_mnm[.dev]") {
  microScenario(
      lest_env,
      "test/maps/micro-empty.scm",
      "Zerg",
      [](Player& bot) {
        using namespace tc::BW;
        bot.addModule(OnceModule::makeWithSpawns(
            {
                {40, UnitType::Zerg_Zergling, 10, 132},
            },
            "MySpawns"));
        bot.addModule(OnceModule::makeWithEnemySpawns(
            {
                {5, UnitType::Terran_Marine, 160, 132},
                {5, UnitType::Terran_Marine, 160, 126},
                {5, UnitType::Terran_Marine, 174, 132},
                {5, UnitType::Terran_Marine, 160, 138},
            },
            "EnemySpawns"));
      },
      1000);
}

// Scenario setup: we shouldn't time out during huge battles
// TODO Make this succeed when we don't time out
SCENARIO("combat/zergling_swarm[.dev]") {
  microScenario(
      lest_env,
      "test/maps/micro-empty.scm",
      "Zerg",
      [](Player& bot) {
        using namespace tc::BW;
        std::vector<SpawnPosition> zerglings;
        std::vector<SpawnPosition> marines;
        for (auto i = 0U; i < 300; i++) {
          zerglings.emplace_back(1, UnitType::Zerg_Zergling, 10, 132);
        }
        for (auto i = 0U; i < 100; i++) {
          marines.emplace_back(1, UnitType::Terran_Marine, 160, 132);
        }
        bot.addModule(
            OnceModule::makeWithSpawns(std::move(zerglings), "MySpawns"));
        bot.addModule(
            OnceModule::makeWithEnemySpawns(std::move(marines), "EnemySpawns"));
      },
      1000);
}
// Scenario setup: we should be able to beat the built-in AI with proper
// spreading of the hydras to avoid too much splash
SCENARIO("combat/hydras_tanks[hide]") {
  microScenario(
      lest_env,
      "test/maps/micro-empty.scm",
      "Zerg",
      [](Player& bot) {
        using namespace tc::BW;
        bot.addModule(OnceModule::makeWithSpawns(
            {
                {8, UnitType::Zerg_Hydralisk, 32, 132},
            },
            "MySpawns"));
        bot.addModule(OnceModule::makeWithEnemySpawns(
            {
                {1, UnitType::Terran_Siege_Tank_Siege_Mode, 160, 132},
                {1, UnitType::Terran_Siege_Tank_Siege_Mode, 160, 126},
                {1, UnitType::Terran_Siege_Tank_Siege_Mode, 168, 132},
                {1, UnitType::Terran_Siege_Tank_Siege_Mode, 160, 138},
            },
            "EnemySpawns"));
      },
      1000);
}

// Scenario setup: we should be able to beat the built-in AI with proper
// mines focus firing
// TODO Why is this failing?
SCENARIO("combat/hydras_mines[.dev]") {
  microScenario(
      lest_env,
      "test/maps/micro-empty.scm",
      "Zerg",
      [](Player& bot) {
        using namespace tc::BW;
        bot.addModule(OnceModule::makeWithSpawns(
            {
                {4, UnitType::Zerg_Hydralisk, 104, 132},
            },
            "MySpawns"));
        bot.addModule(OnceModule::makeWithEnemySpawns(
            {
                {1, UnitType::Terran_Vulture_Spider_Mine, 140, 132},
                {1, UnitType::Terran_Vulture_Spider_Mine, 144, 128},
                {1, UnitType::Terran_Vulture_Spider_Mine, 148, 136},
                {3, UnitType::Terran_Vulture, 155, 132},
                {1, UnitType::Terran_Vulture, 140, 132},
            },
            "EnemySpawns"));
      },
      1000);
}

// Scenario setup: we should be able to beat the built-in AI
// by dragging the mines to the tank
// TODO Why is this failing?
SCENARIO("combat/zergling_mine_drag[.dev]") {
  microScenario(
      lest_env,
      "test/maps/micro-empty.scm",
      "Zerg",
      [](Player& bot) {
        using namespace tc::BW;
        bot.addModule(OnceModule::makeWithSpawns(
            {
                {1, UnitType::Zerg_Hydralisk, 80, 132},
                {2, UnitType::Zerg_Zergling, 100, 132},
            },
            "MySpawns"));
        bot.addModule(OnceModule::makeWithEnemySpawns(
            {
                {1, UnitType::Terran_Vulture_Spider_Mine, 140, 135},
                {1, UnitType::Terran_Vulture_Spider_Mine, 140, 132},
                {1, UnitType::Terran_Vulture_Spider_Mine, 140, 129},
                {1, UnitType::Terran_Siege_Tank_Tank_Mode, 150, 132},
            },
            "EnemySpawns"));
      },
      1000);
}

// Scenario setup: we should be able to beat the built-in AI with proper
// marine focus firing
SCENARIO("combat/mutas_marines[.dev]") {
  microScenario(
      lest_env,
      "test/maps/micro-empty.scm",
      "Zerg",
      [](Player& bot) {
        using namespace tc::BW;
        bot.addModule(OnceModule::makeWithSpawns(
            {
                {6, UnitType::Zerg_Mutalisk, 104, 132},
            },
            "MySpawns"));
        bot.addModule(OnceModule::makeWithEnemySpawns(
            {
                {1, UnitType::Terran_Marine, 140, 132},
                {1, UnitType::Terran_Marine, 142, 132},
                {1, UnitType::Terran_Marine, 144, 132},
                {1, UnitType::Terran_Marine, 148, 132},
                {1, UnitType::Terran_Marine, 140, 128},
                {1, UnitType::Terran_Marine, 142, 128},
                {1, UnitType::Terran_Marine, 144, 128},
                {1, UnitType::Terran_Marine, 148, 128},
                {1, UnitType::Terran_Marine, 140, 136},
                {1, UnitType::Terran_Marine, 142, 136},
                {1, UnitType::Terran_Marine, 144, 136},
                {1, UnitType::Terran_Marine, 148, 136},
            },
            "EnemySpawns"));
      },
      1000);
}

// Scenario setup: we should be able to beat the built-in AI with by going
// up the ramp and then attacking
SCENARIO("combat/ramp_hydras_marines[.dev]") {
  microScenario(
      lest_env,
      "test/maps/micro-ramp.scm",
      "Zerg",
      [](Player& bot) {
        using namespace tc::BW;
        bot.addModule(OnceModule::makeWithSpawns(
            {
                {5, UnitType::Zerg_Hydralisk, 100, 136},
            },
            "MySpawns"));
        bot.addModule(OnceModule::makeWithEnemySpawns(
            {
                {4, UnitType::Terran_Marine, 128, 118},
                {2, UnitType::Terran_Marine, 130, 120},
            },
            "EnemySpawns"));
      },
      1000);
}

// Scenario setup: we should be able to beat the built-in AI with by going
// up the ramp and then attacking
SCENARIO("combat/ramp_zerglings_marines[.dev]") {
  microScenario(
      lest_env,
      "test/maps/micro-ramp.scm",
      "Zerg",
      [](Player& bot) {
        using namespace tc::BW;
        bot.addModule(OnceModule::makeWithSpawns(
            {
                {12, UnitType::Zerg_Zergling, 100, 136},
            },
            "MySpawns"));
        bot.addModule(OnceModule::makeWithEnemySpawns(
            {
                {6, UnitType::Terran_Marine, 128, 118},
            },
            "EnemySpawns"));
      },
      1000);
}

// Scenario setup: we should be able to beat the built-in AI with
// kiting (move away the targeted mutalisk(s), focus fire with others)
SCENARIO("combat/mutas_scourges[.dev][0.9]") {
  microScenario(
      lest_env,
      "test/maps/micro-empty.scm",
      "Zerg",
      [](Player& bot) {
        using namespace tc::BW;
        bot.addModule(OnceModule::makeWithSpawns(
            {
                {1, UnitType::Zerg_Mutalisk, 104, 132},
            },
            "MySpawns"));
        bot.addModule(OnceModule::makeWithEnemySpawns(
            {
                {2, UnitType::Zerg_Scourge, 136, 132},
            },
            "EnemySpawns"));
      },
      1000);
}

SCENARIO("combat/vulture_zealots[.dev]") {
  microScenario(
      lest_env,
      "test/maps/micro-empty.scm",
      "Zerg",
      [](Player& bot) {
        using namespace tc::BW;
        bot.addModule(OnceModule::makeWithSpawns(
            {
                {1, UnitType::Terran_Vulture, 104, 132},
            },
            "MySpawns"));
        bot.addModule(OnceModule::makeWithEnemySpawns(
            {
                {1, UnitType::Protoss_Zealot, 136, 132},
            },
            "EnemySpawns"));
      },
      1000);
}

SCENARIO("combat/vulture_marines[.dev]") {
  microScenario(
      lest_env,
      "test/maps/micro-empty.scm",
      "Zerg",
      [](Player& bot) {
        using namespace tc::BW;
        bot.addModule(OnceModule::makeWithSpawns(
            {
                {1, UnitType::Terran_Vulture, 104, 132},
            },
            "MySpawns"));
        bot.addModule(OnceModule::makeWithEnemySpawns(
            {
                {3, UnitType::Terran_Marine, 136, 132},
            },
            "EnemySpawns"));
      },
      1000);
}

SCENARIO("combat/no_hit_larval") {
  microScenario(
      lest_env,
      "test/maps/micro-empty.scm",
      "Zerg",
      [](Player& bot) {
        using namespace tc::BW;
        bot.addModule(OnceModule::makeWithSpawns(
            {
                {8, UnitType::Zerg_Hydralisk, 104, 132},
            },
            "MySpawns"));
        bot.addModule(OnceModule::makeWithEnemySpawns(
            {
                {2, UnitType::Zerg_Zergling, 104, 126},
            },
            "EnemySpawns"));
      },
      1000);
}

// Test that cloaked units like dark templars are properly reported
SCENARIO("combat/cloaked_flags_set") {
  using namespace tc::BW;

  auto scenario = Scenario("test/maps/micro-empty.scm", "Zerg");
  Player bot(scenario.makeClient());
  bot.setRealtimeFactor(FLAGS_rtfactor);
  bot.addModule(OnceModule::makeWithSpawns(
      {
          {1, UnitType::Zerg_Zergling, 104, 132},
      },
      "MySpawns"));
  bot.addModule(OnceModule::makeWithEnemySpawns(
      {
          {1, UnitType::Protoss_Dark_Templar, 136, 132},
      },
      "EnemySpawns"));

  bot.init();
  auto& ui = bot.state()->unitsInfo();

  // Provide some time for spawning and cloaking
  while (bot.state()->currentFrame() < 50) {
    bot.step();
  }

  EXPECT(ui.myUnits().size() == size_t(1));
  EXPECT(ui.enemyUnits().size() == size_t(1));
  if (ui.enemyUnits().size() > 0) {
    auto* u = ui.enemyUnits()[0];
    EXPECT(u->type == buildtypes::Protoss_Dark_Templar);
    EXPECT((u->unit.flags & tc::Unit::Flags::Cloaked) != 0ul);
    EXPECT((u->unit.flags & tc::Unit::Flags::Detected) == 0ul);
  }
}

SCENARIO("experimental/random_fighting_spirit[hide]") {
  auto scenario = Scenario("test/maps/fighting_spirit_nofow.scm", "Terran");
  auto battles = std::vector<std::string>({
      "test/battles/TL_PvT_GG32647.json",
      "test/battles/TL_PvT_IC409383.json",
      "test/battles/TL_PvZ_GG37241.json",
      "test/battles/TL_PvZ_GG42444.json",
      "test/battles/TL_PvZ_IC321902.json",
  });

  auto myspawns = std::vector<SpawnPosition>();
  auto theirspawns = std::vector<SpawnPosition>();
  auto myspawnset = std::vector<int>();
  auto theirspawnset = std::vector<int>();

  while (true) { // Find a battle where we are Zerg
    auto battlefn = battles[common::Rand::rand() % battles.size()];
    VLOG(1) << "Grabbing battles from " << battlefn;

    std::ifstream ifs(battlefn);
    EXPECT(!ifs.fail());
    json data;
    ifs >> data;
    EXPECT(data.is_array());
    auto choice = rand() % (data.size());
    const auto& battle = data[choice]["data_start"];
    EXPECT(battle.is_array());

    if (battle[0].size() == 0 || battle[1].size() == 0) {
      continue;
    }
    auto ut = battle[0][0][0].get<int>();
    if (torchcraft::BW::data::GetRace[ut] != "Zerg") {
      continue;
    }
    VLOG(1) << "Doing battle " << choice << "/" << data.size();

    for (auto i = 0U; i < battle[0].size(); i++) {
      myspawns.emplace_back(
          1,
          tc::BW::UnitType::_from_integral(battle[0][i][0].get<int>()),
          battle[0][i][1].get<int>(),
          battle[0][i][2].get<int>());
      myspawnset.push_back(battle[0][i][0].get<int>());
    }
    for (auto i = 0U; i < battle[1].size(); i++) {
      theirspawns.emplace_back(
          1,
          tc::BW::UnitType::_from_integral(battle[1][i][0].get<int>()),
          battle[1][i][1].get<int>(),
          battle[1][i][2].get<int>());
      theirspawnset.push_back(battle[1][i][0].get<int>());
    }
    break;
  }

  VLOG(1) << "=== My units: " << myspawns.size();
  for (auto x : myspawns) {
    VLOG(1) << x.type << " " << x.x << " " << x.y;
  }
  VLOG(1) << "=== Their units: " << theirspawns.size();
  for (auto x : theirspawns) {
    VLOG(1) << x.type << " " << x.x << " " << x.y;
  }
  Player bot(scenario.makeClient());
  bot.setRealtimeFactor(FLAGS_rtfactor);

  // Scenario setup
  bot.addModule(OnceModule::makeWithSpawns(myspawns, "MySpawns"));
  bot.addModule(OnceModule::makeWithEnemySpawns(theirspawns, "EnemySpawns"));

  bot.addModule(Module::make<TopModule>());
  bot.addModule(Module::make<MockTacticsModule>());
  bot.addModule(Module::make<CombatModule>());
  bot.addModule(Module::make<CombatMicroModule>());
  bot.addModule(Module::make<UPCToCommandModule>());

  bot.init();
  auto state = bot.state();
  for (int nframes = 0; !state->gameEnded(); nframes++) {
    bot.step();
    // TODO (or not do becauses impossible) Bugs in spawn unit:
    //   - larva (since creep is not generated right away but also who cares)
    //   - workers who occupy the same space (kinda important for harass
    //   scenarios) - floating buildings
    //   - Invisible units cannot be seen and therefore cannot be compared
    //
    // This list is not exhaustive, but I'm not using EXPECT below because of
    // that
    if (nframes == 2) {
      auto tcunits = state->tcstate()->frame->units;
      if (tcunits.find(0) != tcunits.end()) {
        for (auto unit : tcunits.at(0)) {
          auto found = false;
          for (auto itr = myspawnset.begin(); itr != myspawnset.end(); itr++) {
            if ((*itr) == unit.type) {
              myspawnset.erase(itr);
              found = true;
              break;
            }
          }
          if (!found) {
            VLOG(1) << "Created unit type " << unit.type << " erroneously";
          }
        }
      }
      if (tcunits.find(1) != tcunits.end()) {
        for (auto unit : tcunits.at(1)) {
          auto found = false;
          for (auto itr = theirspawnset.begin(); itr != theirspawnset.end();
               itr++) {
            if ((*itr) == unit.type) {
              theirspawnset.erase(itr);
              found = true;
              break;
            }
          }
          if (!found) {
            VLOG(1) << "Created unit " << unit.type << " erroneously";
          }
        }
      }
      VLOG(1) << myspawnset.size() << " " << theirspawnset.size();
      for (auto type : myspawnset) {
        VLOG(1) << "Failed to create unit "
                << tc::BW::UnitType::_from_integral(type)
                << " for player myself";
      }
      for (auto type : theirspawnset) {
        VLOG(1) << "Failed to create unit "
                << tc::BW::UnitType::_from_integral(type)
                << " for player enemy";
      }
    }
  }
}
