/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "scenarioprovider.h"

#include "common/rand.h"
#include "microplayer.h"
#include "modules/once.h"

#include <algorithm>

namespace cherrypi {

// estimation of the number of played frames needed to propagate detection. This
// is 36 frames, assuming a frame skip of 3.
namespace {
int constexpr kDetectionDelay = 12;
}
bool ScenarioProvider::isFinished(int currentStep, bool checkAttack) {
  if (player1_ == nullptr || player2_ == nullptr) {
    return true;
  }
  int units1 = player1_->state()->unitsInfo().myUnits().size();
  int units2 = player2_->state()->unitsInfo().myUnits().size();
  if (currentStep > maxFrame_ || (units1 == 0) || (units2 == 0)) {
    // trivial termination conditions
    return true;
  }

  // We consider the scenario to be finished when no pair of units can attack
  // each other. We need to remind the last step on which we could attack,
  // because detection takes a while to be propagated, hence we need to wait to
  // see if attacks are going to be possible again. If the last attack step is
  // uninitialized, or higher than the current step, we assume that we are at a
  // beginning of an episode and start counting from now.
  if (lastPossibleAttack_ < 0 || lastPossibleAttack_ > currentStep) {
    lastPossibleAttack_ = currentStep;
  }
  auto canAttack = [](const auto& allyUnits, const auto& enemyUnits) {
    for (const auto& u : allyUnits) {
      for (const auto& v : enemyUnits) {
        if (u->canAttack(v)) {
          return true;
        }
      }
    }
    return false;
  };

  bool canAttack1 = canAttack(
      player1_->state()->unitsInfo().myUnits(),
      player1_->state()->unitsInfo().enemyUnits());
  bool canAttack2 = canAttack(
      player2_->state()->unitsInfo().myUnits(),
      player2_->state()->unitsInfo().enemyUnits());

  bool possibleAttack = canAttack1 || canAttack2;
  // we might not be able to attack yet, for example in case the detection
  // status has not been updated yet. That's why we need to track the last time
  // we could attack to avoid premature ending.
  if (possibleAttack) {
    lastPossibleAttack_ = currentStep;
  }

  if (checkAttack && !possibleAttack) {
    return (currentStep - lastPossibleAttack_ > kDetectionDelay);
  }
  return false;
}

BaseMicroScenario::BaseMicroScenario(int maxFrame, std::string map, bool gui)
    : ScenarioProvider(maxFrame, gui), map_(std::move(map)) {}

std::pair<std::shared_ptr<BasePlayer>, std::shared_ptr<BasePlayer>>
BaseMicroScenario::spawnNextScenario(
    const std::function<void(BasePlayer*)>& setup1,
    const std::function<void(BasePlayer*)>& setup2) {
  if (!player1_) {
    // this is probably first run, we need to spawn the game
    // In micro, we don't care about races.
    scenario_ = std::make_shared<SelfPlayScenario>(
        map_,
        tc::BW::Race::Terran,
        tc::BW::Race::Terran,
        GameType::UseMapSettings,
        std::string() /* no replay saving */,
        gui_);
    // Retain TorchCraft clients for efficient re-spawns.
    client1_ = scenario_->makeClient1();
    client2_ = scenario_->makeClient2();
    player1_ = std::make_shared<MicroPlayer>(client1_);
    player2_ = std::make_shared<MicroPlayer>(client2_);

    std::vector<tc::Client::Command> comms;
    comms.emplace_back(tc::BW::Command::SetSpeed, 0);
    comms.emplace_back(tc::BW::Command::SetGui, gui_);
    comms.emplace_back(tc::BW::Command::SetCombineFrames, 1);
    comms.emplace_back(tc::BW::Command::SetFrameskip, 1);
    comms.emplace_back(tc::BW::Command::SetBlocking, true);
    player1_->queueCmds(comms);
    player2_->queueCmds(comms);
  } else {
    // Reset player states by instantiating new ones
    player1_ = std::make_shared<MicroPlayer>(client1_);
    player2_ = std::make_shared<MicroPlayer>(client2_);
  }

  // setup the players
  setup1(player1_.get());
  setup2(player2_.get());

  // spawn units info
  std::vector<OnceModule::SpawnInfo> ally_spawns, enemy_spawns;

  std::tie(ally_spawns, enemy_spawns) = getSpawnInfo();
  auto cmds =
      OnceModule::makeSpawnCommands(ally_spawns, player1_->state()->playerId());

  auto cmds2 = OnceModule::makeSpawnCommands(
      enemy_spawns, player2_->state()->playerId());
  cmds.reserve(cmds.size() + cmds2.size());
  for (const auto& c : cmds2) {
    cmds.push_back(c);
  }
  // make sure commands are sent to the server
  player1_->queueCmds(cmds);

  // loop until all units are ready
  auto state1 = player1_->state();
  auto state2 = player2_->state();
  while (state1->unitsInfo().myUnits().size() != ally_spawns.size() &&
         state2->unitsInfo().myUnits().size() != enemy_spawns.size()) {
    player1_->step();
    player2_->step();
  }

  // notify players of game start
  std::static_pointer_cast<MicroPlayer>(player1_)->onGameStart();
  std::static_pointer_cast<MicroPlayer>(player2_)->onGameStart();
  return {player1_, player2_};
}

void BaseMicroScenario::sendKillCmds() {
  auto state1 = player1_->state();
  auto state2 = player2_->state();
  std::vector<tc::Client::Command> cmds;
  for (const auto& u : state1->unitsInfo().myUnits()) {
    cmds.push_back(
        tc::Client::Command(
            tc::BW::Command::CommandOpenbw,
            tc::BW::OpenBWCommandType::KillUnit,
            u->id));
  }
  player1_->queueCmds(std::move(cmds));
  cmds.clear();
  for (const auto& u : state2->unitsInfo().myUnits()) {
    cmds.push_back(
        tc::Client::Command(
            tc::BW::Command::CommandOpenbw,
            tc::BW::OpenBWCommandType::KillUnit,
            u->id));
  }
  player2_->queueCmds(std::move(cmds));
}

void BaseMicroScenario::cleanScenario() {
  if (!player1_ || !player2_) {
    return;
  }
  // notify players of game end
  std::static_pointer_cast<MicroPlayer>(player1_)->onGameEnd();
  std::static_pointer_cast<MicroPlayer>(player2_)->onGameEnd();

  auto state1 = player1_->state();
  auto state2 = player2_->state();
  player1_->step();
  player2_->step();

  // clean remaining units
  sendKillCmds();
  VLOG(2) << "killing units: state1 my=" << state1->unitsInfo().myUnits().size()
          << " state1 enemy" << state1->unitsInfo().enemyUnits().size()
          << " state2 my=" << state2->unitsInfo().myUnits().size()
          << "state2 enemy=" << state1->unitsInfo().enemyUnits().size();

  // loop until units are actually dead
  int i = 0;
  while (state1->unitsInfo().myUnits().size() != 0 ||
         state2->unitsInfo().myUnits().size() != 0) {
    VLOG(2) << "killing steps: state1 my="
            << state1->unitsInfo().myUnits().size() << " state1 enemy"
            << state1->unitsInfo().enemyUnits().size()
            << " state2 my=" << state2->unitsInfo().myUnits().size()
            << "state2 enemy=" << state1->unitsInfo().enemyUnits().size();
    player1_->step();
    player2_->step();
    i++;
    if (i % 10 == 0) {
      sendKillCmds();
    }
  }
  VLOG(2) << "killing end: state1 my=" << state1->unitsInfo().myUnits().size()
          << " state1 enemy" << state1->unitsInfo().enemyUnits().size()
          << " state2 my=" << state2->unitsInfo().myUnits().size()
          << "state2 enemy=" << state1->unitsInfo().enemyUnits().size();
}

MicroFixedScenario::MicroFixedScenario(
    int maxFrame,
    SpawnList spawnPlayer1,
    SpawnList spawnPlayer2,
    std::string map,
    bool gui)
    : BaseMicroScenario(maxFrame, map, gui),
      spawnPlayer1_(std::move(spawnPlayer1)),
      spawnPlayer2_(std::move(spawnPlayer2)) {}

void MicroFixedScenario::setSpawns(
    SpawnList spawnPlayer1,
    SpawnList spawnPlayer2) {
  spawnPlayer1_ = std::move(spawnPlayer1);
  spawnPlayer2_ = std::move(spawnPlayer2);
}

std::pair<std::vector<OnceModule::SpawnInfo>,
          std::vector<OnceModule::SpawnInfo>>
MicroFixedScenario::getSpawnInfo() {
  // spawn units of player1
  std::vector<OnceModule::SpawnInfo> ally_spawns;
  for (const auto& pair : spawnPlayer1_) {
    for (int i = 0; i < pair.second.count; i++) {
      ally_spawns.emplace_back(
          pair.first,
          pair.second.x,
          pair.second.y,
          pair.second.spreadX,
          pair.second.spreadY);
    }
  }

  // spawn units of player2
  std::vector<OnceModule::SpawnInfo> enemy_spawns;
  for (const auto& pair : spawnPlayer2_) {
    for (int i = 0; i < pair.second.count; i++) {
      enemy_spawns.emplace_back(
          pair.first,
          pair.second.x,
          pair.second.y,
          pair.second.spreadX,
          pair.second.spreadY);
    }
  }
  return {ally_spawns, enemy_spawns};
}

namespace {
const std::map<tc::BW::Race, std::vector<tc::BW::UnitType>> allowedTypes{
    {tc::BW::Race::Zerg,
     {tc::BW::UnitType::Zerg_Zergling,
      tc::BW::UnitType::Zerg_Hydralisk,
      tc::BW::UnitType::Zerg_Lurker,
      tc::BW::UnitType::Zerg_Ultralisk,
      tc::BW::UnitType::Zerg_Mutalisk,
      tc::BW::UnitType::Zerg_Guardian,
      tc::BW::UnitType::Zerg_Devourer,
      tc::BW::UnitType::Zerg_Overlord}},
    {tc::BW::Race::Terran,
     {tc::BW::UnitType::Terran_Battlecruiser,
      tc::BW::UnitType::Terran_Firebat,
      tc::BW::UnitType::Terran_Ghost,
      tc::BW::UnitType::Terran_Goliath,
      tc::BW::UnitType::Terran_Marine,
      tc::BW::UnitType::Terran_Medic,
      tc::BW::UnitType::Terran_Siege_Tank_Siege_Mode,
      tc::BW::UnitType::Terran_Siege_Tank_Tank_Mode,
      tc::BW::UnitType::Terran_Valkyrie,
      tc::BW::UnitType::Terran_Vulture,
      tc::BW::UnitType::Terran_Wraith,
      tc::BW::UnitType::Terran_Science_Vessel}},
    {tc::BW::Race::Protoss,
     {tc::BW::UnitType::Protoss_Zealot,
      tc::BW::UnitType::Protoss_Dragoon,
      tc::BW::UnitType::Protoss_Archon,
      // tc::BW::UnitType::Protoss_High_Templar,
      tc::BW::UnitType::Protoss_Dark_Templar,
      // tc::BW::UnitType::Protoss_Reaver,
      tc::BW::UnitType::Protoss_Scout,
      // tc::BW::UnitType::Protoss_Carrier,
      tc::BW::UnitType::Protoss_Corsair,
      tc::BW::UnitType::Protoss_Observer}}};

static std::set<tc::BW::UnitType> detectors, flying, antiair, ground,
    antiground;
static std::once_flag flag1;

static void init() {
  for (const auto& races : allowedTypes) {
    for (const auto& u : races.second) {
      auto bt = getUnitBuildType((int)u);
      if (bt->isDetector) {
        detectors.insert(u);
      } else {
        if (bt->isFlyer) {
          flying.insert(u);
        } else {
          ground.insert(u);
        }
      }
      if (bt->hasAirWeapon) {
        antiair.insert(u);
      }
      if (bt->hasGroundWeapon) {
        antiground.insert(u);
      }
    }
  }
}
static const std::set<tc::BW::UnitType> cloaked{
    tc::BW::UnitType::Zerg_Lurker,
    tc::BW::UnitType::Protoss_Dark_Templar,
    tc::BW::UnitType::Protoss_Observer};

std::pair<std::vector<OnceModule::SpawnInfo>,
          std::vector<OnceModule::SpawnInfo>>
sampleArmies(
    const std::vector<tc::BW::Race>& allowedRaces,
    std::map<tc::BW::Race, int> maxSupplyMap,
    bool randomSize,
    bool checkCompatibility) {
  std::call_once(flag1, init);

  // pick the races
  std::uniform_int_distribution<int> race_dist(0, allowedRaces.size() - 1);
  auto race1 = allowedRaces[common::Rand::sample(race_dist)];
  auto race2 = allowedRaces[common::Rand::sample(race_dist)];

  if (randomSize) {
    // pick the target supplies for both races
    for (const auto& r : {race1, race2}) {
      std::uniform_int_distribution<int> supply_dist(
          std::min(10, maxSupplyMap[r]), maxSupplyMap[r]);
      maxSupplyMap[r] = common::Rand::sample(supply_dist);
    }
  }

  auto computeSupply = [](const tc::BW::UnitType& unit) {
    // We skew a bit the supplies for the detectors, to avoid too many of them.
    // Since observers are more fragile, we give them a slight bonus
    if (detectors.count(unit) > 0) {
      return ((int)unit == (int)tc::BW::UnitType::Protoss_Observer) ? 3 : 4;
    }
    return tc::BW::data::SupplyRequired[unit];
  };

  // this helper prepares the vectors of all units that you can sample from.
  // This vectors contains as many units as you could pick in total. For
  // example, if a unit costs 2 supplies, and the max supply for this army is
  // 50, then we add 25 of this unit in the vector
  auto prepareUnits = [&computeSupply, &maxSupplyMap](
      decltype(allowedRaces.front()) race,
      std::vector<tc::BW::UnitType>& allUnits) {
    for (const auto& u : allowedTypes.at(race)) {
      const int supply = computeSupply(u);
      for (int i = 0; i < maxSupplyMap.at(race) / supply; i++) {
        allUnits.push_back(u);
      }
    }

  };
  std::vector<tc::BW::UnitType> allUnits1, allUnits2;
  prepareUnits(race1, allUnits1);
  prepareUnits(race2, allUnits2);

  // boolean mask of the units we picked
  std::vector<bool> chosen1(allUnits1.size(), false),
      chosen2(allUnits2.size(), false);

  // distribution over possible units
  std::uniform_int_distribution<int> unit_dist1(0, chosen1.size() - 1);
  std::uniform_int_distribution<int> unit_dist2(0, chosen2.size() - 1);

  // the sampling is inspired by
  // https://www.cc.gatech.edu/~mihail/D.8802readings/knapsack.pdf The idea is
  // to make a random walk on the space of knapsack solutions. At each step, we
  // pick an item (a unit) at random. If it is already in the sack, we remove
  // it. If it is not, and it fits, then we add it
  // The paper claims a mixing time of this MC in O(n^{4.5}), but this is too
  // computationally intensive for our purposes. So we do n^3 iterations
  // instead, and hope for the best.
  const int iters = allUnits1.size() * allUnits2.size() * allUnits1.size();
  // this simulates a step on the MC
  auto transition = [&computeSupply](
      std::vector<bool>& chosen,
      int& currentSupply,
      std::vector<tc::BW::UnitType>& allUnits,
      std::uniform_int_distribution<int>& unit_dist,
      int maxSupply) {
    int index = common::Rand::sample(unit_dist);
    const int supply = computeSupply(allUnits[index]);
    if (!chosen[index] && currentSupply + supply > maxSupply)
      return;
    currentSupply += supply * (chosen[index] ? -1 : 1);
    chosen[index] = !chosen[index];
  };
  int currentSupply1 = 0, currentSupply2 = 0;
  for (int i = 0; i < iters; ++i) {
    transition(
        chosen1, currentSupply1, allUnits1, unit_dist1, maxSupplyMap.at(race1));
    transition(
        chosen2, currentSupply2, allUnits2, unit_dist2, maxSupplyMap.at(race2));
    if (i == iters - 1 && checkCompatibility) {
      // this is the last iteration, we have to check if the armies are
      // compatible
      bool hasFlying1 = false, hasFlying2 = false;
      bool hasGround1 = false, hasGround2 = false;
      bool hasCloaked1 = false, hasCloaked2 = false;
      bool hasDetector1 = false, hasDetector2 = false;
      bool hasAntiGround1 = false, hasAntiGround2 = false;
      bool hasAntiAir1 = false, hasAntiAir2 = false;
      for (size_t i = 0; i < std::max(chosen1.size(), chosen2.size()); i++) {
        if (i < chosen1.size() && chosen1[i]) {
          if (flying.count(allUnits1[i]) > 0) {
            hasFlying1 = true;
          }
          if (ground.count(allUnits1[i]) > 0) {
            hasGround1 = true;
          }
          if (antiair.count(allUnits1[i]) > 0) {
            hasAntiAir1 = true;
          }
          if (antiground.count(allUnits1[i]) > 0) {
            hasAntiGround1 = true;
          }
          if (detectors.count(allUnits1[i]) > 0) {
            hasDetector1 = true;
          }
          if (cloaked.count(allUnits1[i]) > 0) {
            hasCloaked1 = true;
          }
        }
        if (i < chosen2.size() && chosen2[i]) {
          if (flying.count(allUnits2[i]) > 0) {
            hasFlying2 = true;
          }
          if (antiair.count(allUnits2[i]) > 0) {
            hasAntiAir2 = true;
          }
          if (detectors.count(allUnits2[i]) > 0) {
            hasDetector2 = true;
          }
          if (cloaked.count(allUnits2[i]) > 0) {
            hasCloaked2 = true;
          }
          if (antiair.count(allUnits2[i]) > 0) {
            hasAntiAir2 = true;
          }
          if (antiground.count(allUnits2[i]) > 0) {
            hasAntiGround2 = true;
          }
        }
      }
      if ((hasFlying1 && !hasAntiAir2) || (hasFlying2 && !hasAntiAir1) ||
          (hasCloaked1 && !hasDetector2) || (hasCloaked2 && !hasDetector1) ||
          (hasGround1 && !hasAntiGround2) || (hasGround2 && !hasAntiGround1)) {
        // in that case, the armies are not compatible, we have to continue
        // sampling
        i--;
      }
    }
  }
  std::vector<OnceModule::SpawnInfo> list1, list2;
  auto addUnit = [](
      std::vector<OnceModule::SpawnInfo>& list,
      size_t i,
      const std::vector<bool>& chosen,
      const std::vector<tc::BW::UnitType>& allUnits,
      bool ally) {
    if (i < chosen.size() && chosen[i]) {
      bool isDetector = (detectors.count(allUnits[i]) > 0);
      // we spawn stuff in concavish-looking shapes, so that the initial
      // positions don't have too much of an impact
      float spread = isDetector ? 0.0 : 5.0;
      int x = isDetector ? 110 : 100;
      if (!ally) {
        x = isDetector ? 130 : 140;
      }
      list.emplace_back(allUnits[i], x, 132, 0.5, spread);
    }
  };
  for (size_t i = 0; i < std::max(chosen1.size(), chosen2.size()); i++) {
    addUnit(list1, i, chosen1, allUnits1, true);
    addUnit(list2, i, chosen2, allUnits2, false);
  }
  return {list1, list2};
}

} // namespace

RandomMicroScenario::RandomMicroScenario(
    int maxFrame,
    std::vector<tc::BW::Race> allowedRaces,
    bool randomSize,
    std::map<tc::BW::Race, int> maxSupplyMap,
    bool checkCompatibility,
    std::string map,
    bool gui)
    : BaseMicroScenario(maxFrame, map, gui),
      allowedRaces_(std::move(allowedRaces)),
      randomSize_(std::move(randomSize)),
      maxSupplyMap_(std::move(maxSupplyMap)),
      checkCompatibility_(std::move(checkCompatibility)) {}

void RandomMicroScenario::setParams(
    std::vector<tc::BW::Race> allowedRaces,
    bool randomSize,
    std::map<tc::BW::Race, int> maxSupplyMap,
    bool checkCompatibility) {
  allowedRaces_ = std::move(allowedRaces);
  randomSize_ = std::move(randomSize);
  maxSupplyMap_ = std::move(maxSupplyMap);
  checkCompatibility_ = std::move(checkCompatibility);
}

std::pair<std::vector<OnceModule::SpawnInfo>,
          std::vector<OnceModule::SpawnInfo>>
RandomMicroScenario::getSpawnInfo() {
  return sampleArmies(
      allowedRaces_, maxSupplyMap_, randomSize_, checkCompatibility_);
}

} // namespace cherrypi
