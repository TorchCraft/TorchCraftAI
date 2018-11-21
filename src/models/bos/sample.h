/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "cherrypi.h"
#include "features/features.h"
#include "features/unitsfeatures.h"
#include "modules/autobuild.h"

#ifdef HAVE_CPID
#include <cpid/centraltrainer.h>
#endif // HAVE_CPID

#include <autogradpp/autograd.h>

#include <regex>

#include <cereal/archives/binary.hpp>
#include <cereal/types/vector.hpp>

namespace cereal {
template <class Archive>
void serialize(Archive& archive, torchcraft::Resources& resource) {
  archive(
      resource.ore,
      resource.gas,
      resource.used_psi,
      resource.total_psi,
      resource.upgrades,
      resource.upgrades_level,
      resource.techs);
}
} // namespace cereal

namespace cherrypi {
namespace bos {

std::map<std::string, int64_t> const& buildOrderMap();
std::vector<std::string> const& targetBuilds();
std::string allowedTargetsAsFlag();
std::string allowedOpeningsAsFlag();

inline char getOpponentRace(std::string const& opponent) {
  std::regex race_regex("_([PZT]{1})_");
  std::smatch race_match;
  if (!std::regex_search(opponent, race_match, race_regex)) {
    throw std::runtime_error("Opponent string is not correct!");
  }
  return race_match[0].str()[1];
}

inline std::string addRacePrefix(std::string buildOrder, char prefix) {
  return std::string(1, prefix) + "-" + buildOrder;
}

inline std::string addRacePrefix(std::string buildOrder, int race) {
  return addRacePrefix(
      std::move(buildOrder),
      tc::BW::Race::_from_integral(race)._to_string()[0]);
}

inline std::string stripRacePrefix(std::string prefixedBo) {
  return prefixedBo.substr(2);
}

inline int64_t buildOrderId(std::string const& bo) {
  auto const& boMap = buildOrderMap();
  auto it = boMap.find(bo);
  if (it == boMap.end()) {
    throw std::runtime_error("Unknown build order: " + bo);
  }
  return it->second;
}

torch::Tensor getBuildOrderMaskByRace(char race);
torch::Tensor getBuildOrderMaskByRace(int race);

/// A list of possible features that can be extracted from a Sample
enum class BosFeature {
  Undef,
  /// Map features from StaticData
  Map,
  /// Map "ID" based on sum of map features
  MapId,
  /// 2-dimensional: our and their race
  Race,
  /// Defogger-style pooled unit types
  Units,
  /// Bag-of-words unit type counts
  BagOfUnitCounts,
  /// Bag-of-words unit type counts in future autobuild states (ours only)
  BagOfUnitCountsAbs5_15_30,
  /// Ore/Gas/UsedPsi/TotalPsi: log(x / 5 + 1)
  Resources5Log,
  /// 142-dim tech/upgrade vector: one bit for each upgrade/level/tech
  TechUpgradeBits,
  /// 142-dim vector of pending upgrades/techs
  PendingTechUpgradeBits,
  /// Numerical frame value
  TimeAsFrame,
  /// Id of active build order
  ActiveBo,
  /// Id of next build order
  NextBo,
};

// Features that don't change throughout the game
struct StaticData {
  static int constexpr kMapSize = 512; // walk tiles
  static int constexpr kNumMapChannels = 4;

  /// Various map features
  FeatureData map;
  /// Probability of having taken a random switch (per sample).
  /// Only used during supervised data generation.
  float switchProba = 0.0f;
  /// Race for our player (0) and the opponent (1)
  int race[2];
  /// Player name of opponent
  std::string opponentName;
  /// Did we win this game?
  bool won = false;
  /// Game Id (optional)
  std::string gameId;

  StaticData() = default;
  StaticData(State* state);

  template <class Archive>
  void serialize(Archive& ar, uint32_t const version) {
    ar(CEREAL_NVP(map),
       CEREAL_NVP(switchProba),
       CEREAL_NVP(race[0]),
       CEREAL_NVP(race[1]),
       CEREAL_NVP(opponentName),
       CEREAL_NVP(won));
    if (version > 0) {
      ar(CEREAL_NVP(gameId));
    }
  }
};

struct Sample {
  std::shared_ptr<StaticData> staticData;
  /// Defogger style unit types in spatial representation
  FeatureData units;
  /// Frame number of this sample
  FrameNum frame;
  /// Our resources
  tc::Resources resources;
  /// Current build order
  std::string buildOrder;
  /// Build order until next sample
  std::string nextBuildOrder;
  /// Whether we've switched the build order after taking this sample
  bool switched = false;
  /// Upgrades that are currently being researched
  uint64_t pendingUpgrades = 0;
  /// Levels for upgrades that are currently researched
  uint64_t pendingUpgradesLevel = 0;
  /// Techs that are currently being researched
  uint64_t pendingTechs = 0;
  /// Future autobuild states for given frame offsets
  std::map<int, autobuild::BuildState> nextAbboStates;

  Sample() = default;
  Sample(
      State* state,
      int res,
      int sride,
      std::shared_ptr<StaticData> sd = nullptr);
  virtual ~Sample() = default;

  template <class Archive>
  void serialize(Archive& ar, uint32_t const version) {
    if (version < 2) {
      throw std::runtime_error("Unsupported version");
    }
    // Note that Cereal will serialized shared_ptr instances only once per
    // archive
    ar(CEREAL_NVP(staticData),
       CEREAL_NVP(units),
       CEREAL_NVP(frame),
       CEREAL_NVP(resources),
       CEREAL_NVP(buildOrder),
       CEREAL_NVP(nextBuildOrder),
       CEREAL_NVP(switched),
       CEREAL_NVP(pendingUpgrades),
       CEREAL_NVP(pendingUpgradesLevel),
       CEREAL_NVP(pendingTechs),
       CEREAL_NVP(nextAbboStates));
    if (version == 2) {
      // Unit features saved with /= 10 instead of gscore
      renormV2Features();
    }
  }

  torch::Tensor featurize(
      BosFeature feature,
      torch::Tensor buffer = torch::Tensor()) const;
  ag::tensor_list featurize(std::vector<BosFeature> features) const;
  void renormV2Features();

  static std::map<int, autobuild::BuildState> simulateAbbo(
      State* state,
      std::string const& buildOrder,
      std::vector<int> const& frameOffsets);
};

#ifdef HAVE_CPID
struct ReplayBufferFrame : cpid::CerealizableReplayBufferFrame {
  Sample sample;

  ReplayBufferFrame() = default;
  ReplayBufferFrame(Sample sample) : sample(std::move(sample)) {}
  virtual ~ReplayBufferFrame() = default;

  template <class Archive>
  void serialize(Archive& ar, uint32_t const version) {
    ar(cereal::base_class<cpid::CerealizableReplayBufferFrame>(this), sample);
  }
};

struct EpisodeData {
  cpid::GameUID gameId;
  cpid::EpisodeKey episodeKey;
  std::vector<std::shared_ptr<cpid::CerealizableReplayBufferFrame>> frames;

  template <class Archive>
  void serialize(Archive& ar) {
    ar(gameId);
    ar(episodeKey);
    uint32_t size = frames.size();
    ar(size);
    frames.resize(size);
    for (auto i = 0U; i < size; i++) {
      ar(frames[i]);
    }
  }
};
#endif // HAVE_CPID

} // namespace bos

// Backwards compatibility with previous naming
// using BosStaticData = bos::StaticData;
// using BosSample = bos::Sample;
typedef bos::StaticData BosStaticData;
typedef bos::Sample BosSample;
#ifdef HAVE_CPID
typedef bos::ReplayBufferFrame BosReplayBufferFrame;
typedef bos::EpisodeData BosEpisodeData;
#endif // HAVE_CPID

} // namespace cherrypi

CEREAL_CLASS_VERSION(cherrypi::BosStaticData, 2);
CEREAL_CLASS_VERSION(cherrypi::BosSample, 3);
#ifdef HAVE_CPID
CEREAL_REGISTER_TYPE(cherrypi::BosReplayBufferFrame);
CEREAL_CLASS_VERSION(cherrypi::BosReplayBufferFrame, 0);
#endif // HAVE_CPID
