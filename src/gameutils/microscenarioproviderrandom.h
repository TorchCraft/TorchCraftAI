/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "microscenarioprovider.h"

namespace cherrypi {

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
class MicroScenarioProviderRandom : public MicroScenarioProvider {
 public:
  MicroScenarioProviderRandom(
      std::vector<tc::BW::Race> allowedRaces = {tc::BW::Race::Protoss,
                                                tc::BW::Race::Terran,
                                                tc::BW::Race::Zerg},
      bool randomSize = true,
      std::map<tc::BW::Race, int> maxSupplyMap = {{tc::BW::Race::Protoss, 60},
                                                  {tc::BW::Race::Terran, 55},
                                                  {tc::BW::Race::Zerg, 50}},
      bool checkCompatibility = true);

  void setParams(
      std::vector<tc::BW::Race> allowedRaces,
      bool randomSize,
      std::map<tc::BW::Race, int> maxSupplyMap,
      bool checkCompatibility);

 protected:
  FixedScenario getFixedScenario() override;
  std::vector<tc::BW::Race> allowedRaces_;
  bool randomSize_;
  std::map<tc::BW::Race, int> maxSupplyMap_;
  bool checkCompatibility_;
};

} // namespace cherrypi
