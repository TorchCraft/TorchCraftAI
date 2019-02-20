/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "microrandomscenario.h"
#include "common/rand.h"

namespace cherrypi {

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

ScenarioInfo sampleArmies(
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

  auto addUnit = [](SpawnList& list,
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
      list.emplace_back(1, allUnits[i], x, 132, 0.5, spread);
    }
  };

  ScenarioInfo output;
  for (size_t i = 0; i < std::max(chosen1.size(), chosen2.size()); i++) {
    addUnit(output.allyList, i, chosen1, allUnits1, true);
    addUnit(output.enemyList, i, chosen2, allUnits2, false);
  }
  return output;
}

} // namespace

MicroRandomScenario::MicroRandomScenario(
    int maxFrame,
    std::vector<tc::BW::Race> allowedRaces,
    bool randomSize,
    std::map<tc::BW::Race, int> maxSupplyMap,
    bool checkCompatibility,
    bool gui)
    : BaseMicroScenario(maxFrame, gui),
      allowedRaces_(std::move(allowedRaces)),
      randomSize_(std::move(randomSize)),
      maxSupplyMap_(std::move(maxSupplyMap)),
      checkCompatibility_(std::move(checkCompatibility)) {}

void MicroRandomScenario::setParams(
    std::vector<tc::BW::Race> allowedRaces,
    bool randomSize,
    std::map<tc::BW::Race, int> maxSupplyMap,
    bool checkCompatibility) {
  allowedRaces_ = std::move(allowedRaces);
  randomSize_ = std::move(randomSize);
  maxSupplyMap_ = std::move(maxSupplyMap);
  checkCompatibility_ = std::move(checkCompatibility);
}

ScenarioInfo MicroRandomScenario::getScenarioInfo() {
  return sampleArmies(
      allowedRaces_, maxSupplyMap_, randomSize_, checkCompatibility_);
}

} // namespace cherrypi
