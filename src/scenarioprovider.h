/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "modules/once.h"
#include "selfplayscenario.h"
#include "state.h"

namespace cherrypi {

class MicroPlayer;

/**
 * Base class for providing scenarios.
 * Returns a pair of players to be used by the training code.
 *
 * Detects game end and cleans up after the scenario.
 */
class ScenarioProvider {
 public:
  /// \arg maxFrame Frame limit of the scenario
  ScenarioProvider(int maxFrame, bool gui = false)
      : maxFrame_(maxFrame), gui_(gui) {}
  virtual ~ScenarioProvider() {}

  /// Spawns the scenario. It takes as parameters the setup functions for both
  /// players (this should take care of adding modules), and returns the
  /// pointers
  /// to the created players
  virtual std::pair<std::shared_ptr<BasePlayer>, std::shared_ptr<BasePlayer>>
  spawnNextScenario(
      const std::function<void(BasePlayer*)>& setup1,
      const std::function<void(BasePlayer*)>& setup2) = 0;

  /// Check whether the scenario is finished.
  /// By default, return true whenever the number of frames is exceeded or one
  /// of
  /// the players don't have any units left
  /// If checkAttack is true, it will also check that at least one unit in one
  /// army is able to attack at least one unit in the opponent't army.
  virtual bool isFinished(int currentStep, bool checkAttack = true);

  /// Clean the possible left-overs of the last scenario. Must be called before
  /// spawnNextScenario
  virtual void cleanScenario() {}

 protected:
  template <typename T>
  void loadMap(
      std::string const& map,
      tc::BW::Race race1,
      tc::BW::Race race2,
      GameType gameType,
      std::string const& replayPath = std::string()) {
    scenario_ = std::make_shared<SelfPlayScenario>(
        map, race1, race2, gameType, replayPath, gui_);
    player1_ = std::make_shared<T>(scenario_->makeClient1());
    player2_ = std::make_shared<T>(scenario_->makeClient2());
  }

  int maxFrame_;
  bool gui_;
  std::shared_ptr<BasePlayer> player1_;
  std::shared_ptr<BasePlayer> player2_;
  std::shared_ptr<SelfPlayScenario> scenario_;

  int lastPossibleAttack_ = -1;
};

struct SpawnPosition {
  int count;
  int x, y;
  float spreadX = 0.0, spreadY = 0.0;
};
using SpawnList = std::multimap<tc::BW::UnitType, SpawnPosition>;

class BaseMicroScenario : public ScenarioProvider {
 public:
  BaseMicroScenario(
      int maxFrame,
      std::string map = "test/maps/micro-empty2.scm",
      bool gui = false);

  std::pair<std::shared_ptr<BasePlayer>, std::shared_ptr<BasePlayer>>
  spawnNextScenario(
      const std::function<void(BasePlayer*)>& setup1,
      const std::function<void(BasePlayer*)>& setup2) override;

  void cleanScenario() override;

 protected:
  virtual std::pair<std::vector<OnceModule::SpawnInfo>,
                    std::vector<OnceModule::SpawnInfo>>
  getSpawnInfo() = 0;

  void sendKillCmds();

  std::string map_;
  std::shared_ptr<tc::Client> client1_;
  std::shared_ptr<tc::Client> client2_;
};

class MicroFixedScenario : public BaseMicroScenario {
 public:
  MicroFixedScenario(
      int maxFrame,
      SpawnList spawnPlayer1,
      SpawnList spawnPlayer2,
      std::string map = "test/maps/micro-empty2.scm",
      bool gui = false);

  void setSpawns(SpawnList spawnPlayer1, SpawnList spawnPlayer2);

 protected:
  std::pair<std::vector<OnceModule::SpawnInfo>,
            std::vector<OnceModule::SpawnInfo>>
  getSpawnInfo() override;
  SpawnList spawnPlayer1_;
  SpawnList spawnPlayer2_;
};

/**
 * Generates Random armies. Parameters:
 * AllowedRaces: the set of races we can randomly draw from
 * maxSupplyMap: Maximum supply for each race.
 * randomSize: if true, the target supply is drawn uniformly in [min(10,
 *              maxSupply), maxSupply]. Else, the target supply is the one
 *              from the supplyMap.
 * checkCompatiblity: if true, we don't sample armies that are incompatible.
 * Sources of incompatiblilites: air units in one army but no air weapon in
 * the other, ground units in one army but no ground weapon in the other,
 * cloaked/burrowable units in one army but no detector in the other.
 * In other words, we require that each unit can be attacked by at least one
 * unit of the other army.
 *
 * Note that due to sampling artifacts, the actual sampled supply
 * might be a bit smaller than the target
 *
 * The default parameters give scenarios that are somewhat balanced (as
 * mesured by playing random battles using attack-closest heuristic and no
 * micro). Protoss has a slightly lower win-rate on average, around 30%.
 *
 * The following units are currently left out:
 * - All spell caster (except Science Vessels, which are used as detectors for
 * terrans)
 * - Reavers (no way to spawn scarabs currently)
 * - Carrier (same with interceptors)
 * - Dropships
 * - SCVs, Drones, Probes
 * - Scourge + infested terrans (annoying micro)
 */
class RandomMicroScenario : public BaseMicroScenario {
 public:
  RandomMicroScenario(
      int maxFrame,
      std::vector<tc::BW::Race> allowedRaces = {tc::BW::Race::Protoss,
                                                tc::BW::Race::Terran,
                                                tc::BW::Race::Zerg},
      bool randomSize = true,
      std::map<tc::BW::Race, int> maxSupplyMap = {{tc::BW::Race::Protoss, 60},
                                                  {tc::BW::Race::Terran, 55},
                                                  {tc::BW::Race::Zerg, 50}},
      bool checkCompatibility = true,
      std::string map = "test/maps/micro-empty2.scm",
      bool gui = false);

  void setParams(
      std::vector<tc::BW::Race> allowedRaces,
      bool randomSize,
      std::map<tc::BW::Race, int> maxSupplyMap,
      bool checkCompatibility);

 protected:
  std::pair<std::vector<OnceModule::SpawnInfo>,
            std::vector<OnceModule::SpawnInfo>>
  getSpawnInfo() override;

  std::vector<tc::BW::Race> allowedRaces_;
  bool randomSize_;
  std::map<tc::BW::Race, int> maxSupplyMap_;
  bool checkCompatibility_;
};

} // namespace cherrypi
