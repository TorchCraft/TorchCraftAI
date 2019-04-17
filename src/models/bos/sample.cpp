/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "sample.h"

#include "buildorders/base.h"
#include "models/bandit.h"
#include "state.h"

namespace cherrypi {
namespace bos {

namespace {

auto constexpr kGScoreScale = 1.0f / 1000.0f;
torch::Tensor gScoreNorm() {
  static torch::Tensor data;
  auto init = [&] {
    UnitTypeMDefoggerFeaturizer udf;
    auto n = UnitTypeMDefoggerFeaturizer::kNumUnitTypes / 2;
    data = torch::zeros({n * 2}, torch::TensorOptions(torch::kFloat));
    auto acc = data.accessor<float, 1>();
    for (int i = 0; i < n; i++) {
      auto const* bt = getUnitBuildType(udf.unmapType(i));
      acc[i] = acc[i + n] = bt->gScore * kGScoreScale;
    }

    // XXX Mask out all unit types that we have not seen in training (based on
    // subset of EA03). We don't want to surprise the model.
    auto unseen = std::vector<int>{
        0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,
        13,  14,  15,  16,  18,  22,  27,  28,  30,  31,  32,  33,  35,
        36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,
        49,  50,  51,  52,  53,  54,  55,  56,  58,  59,  60,  61,  62,
        63,  64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,
        76,  77,  81,  83,  94,  95,  96,  97,  98,  99,  100, 101, 102,
        103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115,
        116, 117, 130, 133, 148, 154, 168, 169, 170, 171, 172, 173, 176,
        195, 199, 228, 229, 230, 231, 232, 233, 234, 235};
    for (auto const& idx : unseen) {
      VLOG(2) << fmt::format(
          "Masking out unseen unit type {} {}",
          idx < n ? "allied" : "enemy",
          getUnitBuildType(udf.unmapType(idx % n))->name);
      acc[idx] = 0;
    }
  };

  static std::once_flag flag;
  std::call_once(flag, init);
  return data;
}

} // namespace

std::map<std::string, int64_t> const& buildOrderMap() {
  static std::map<std::string, int64_t> boMap = {{"Z-zvz12poolhydras", 0},
                                                 {"Z-zvzoverpool", 1},
                                                 {"Z-zvzoverpoolplus1", 2},
                                                 {"Z-zvz9poolspeed", 3},
                                                 {"Z-zvz9gas10pool", 4},
                                                 {"Z-hydras", 5},
                                                 {"Z-9poolspeedlingmuta", 6},
                                                 {"Z-ultras", 7},
                                                 {"Z-zve9poolspeed", 8},
                                                 {"Z-10hatchling", 9},
                                                 {"T-zvtantimech", 10},
                                                 {"T-zvtmacro", 11},
                                                 {"T-hydras", 12},
                                                 {"T-3basepoollings", 13},
                                                 {"T-zvt2baseguardian", 14},
                                                 {"T-12hatchhydras", 15},
                                                 {"T-2hatchmuta", 16},
                                                 {"T-12poolmuta", 17},
                                                 {"T-ultras", 18},
                                                 {"T-midmassling", 19},
                                                 {"T-zve9poolspeed", 20},
                                                 {"T-zvp10hatch", 21},
                                                 {"T-zvtp1hatchlurker", 22},
                                                 {"T-zvt3hatchlurker", 23},
                                                 {"T-10hatchling", 24},
                                                 {"T-zvt2baseultra", 25},
                                                 {"P-zvtantimech", 26},
                                                 {"P-zvtmacro", 27},
                                                 {"P-hydras", 28},
                                                 {"P-3basepoollings", 29},
                                                 {"P-12hatchhydras", 30},
                                                 {"P-2hatchmuta", 31},
                                                 {"P-12poolmuta", 32},
                                                 {"P-ultras", 33},
                                                 {"P-zvp6hatchhydra", 34},
                                                 {"P-zvpohydras", 35},
                                                 {"P-midmassling", 36},
                                                 {"P-zvpmutas", 37},
                                                 {"P-zve9poolspeed", 38},
                                                 {"P-zvp10hatch", 39},
                                                 {"P-zvpomutas", 40},
                                                 {"P-zvp3hatchhydra", 41},
                                                 {"P-zvtp1hatchlurker", 42},
                                                 {"P-10hatchling", 43}};
  return boMap;
}

std::vector<std::string> const& targetBuilds() {
  auto allTargets = []() {
    auto const& boMap = buildOrderMap();
    std::vector<std::string> targets;
    for (auto race :
         {+tc::BW::Race::Zerg, +tc::BW::Race::Terran, +tc::BW::Race::Protoss}) {
      for (auto it : model::buildOrdersForTraining()) {
        if (!it.second.validSwitch()) {
          continue;
        }
        auto& o = it.second.ourRaces_;
        if (std::find(o.begin(), o.end(), +tc::BW::Race::Zerg) == o.end()) {
          continue;
        }
        auto& r = it.second.enemyRaces_;
        if (std::find(r.begin(), r.end(), race) != r.end()) {
          auto key = fmt::format("{}-{}", race._to_string()[0], it.first);
          if (boMap.find(key) != boMap.end()) {
            targets.push_back(std::move(key));
          }
        }
      }
    }
    return targets;
  };

  static std::vector<std::string> boTargets = allTargets();
  return boTargets;
}

std::string allowedTargetsAsFlag() {
  return utils::joinVector(targetBuilds(), '_');
}

std::string allowedOpeningsAsFlag() {
  std::string openings;
  auto const& boMap = buildOrderMap();
  for (auto race :
       {+tc::BW::Race::Zerg, +tc::BW::Race::Terran, +tc::BW::Race::Protoss}) {
    for (auto it : model::buildOrdersForTraining()) {
      if (!it.second.validOpening()) {
        continue;
      }
      // Only go with openings the model knows about
      if (boMap.find(fmt::format("{}-{}", race._to_string()[0], it.first)) ==
          boMap.end()) {
        // The model assumes active build orders are a subset of target build
        // orders
        continue;
      }
      auto& o = it.second.ourRaces_;
      if (std::find(o.begin(), o.end(), +tc::BW::Race::Zerg) == o.end()) {
        continue;
      }
      auto& r = it.second.enemyRaces_;
      if (std::find(r.begin(), r.end(), race) == r.end()) {
        continue;
      }
      if (!openings.empty()) {
        openings += "_";
      }
      openings += it.first;
    }
  }
  return openings;
}

torch::Tensor getBuildOrderMaskByRace(char race) {
  // Targets only
  auto const& targets = targetBuilds();
  auto const& boMap = buildOrderMap();
  auto mask = torch::zeros({(int64_t)targets.size()});
  auto acc = mask.accessor<float, 1>();
  for (const auto& bo : targets) {
    if (bo[0] == race) {
      acc[boMap.at(bo)] = 1.0f;
    }
  }
  return mask;
}

torch::Tensor getBuildOrderMaskByRace(int race) {
  switch (race) {
    case +tc::BW::Race::Zerg:
      return getBuildOrderMaskByRace('Z');
    case +tc::BW::Race::Terran:
      return getBuildOrderMaskByRace('T');
    case +tc::BW::Race::Protoss:
      return getBuildOrderMaskByRace('P');
    default:
      break;
  }
  throw std::runtime_error("Unknown race: " + std::to_string(race));
}

int64_t mapId(torch::Tensor mapFeats) {
  // Hardcoded map IDs based on sum of features. If featurization changes, this
  // changes as well!!
  // Use 0 as a fallback, but warn about it
  static std::map<int64_t, int64_t> ids{{161180, 1},
                                        {192021, 2},
                                        {400088, 3},
                                        {401550, 4},
                                        {412053, 5},
                                        {416217, 6},
                                        {437353, 7},
                                        {439096, 8},
                                        {442095, 9},
                                        {470412, 10},
                                        {498659, 11},
                                        {507745, 12},
                                        {531960, 13},
                                        {638461, 14},
                                        {642734, 15},
                                        {713678, 16}};
  auto sum = mapFeats.sum().item<int64_t>();
  auto it = ids.find(sum);
  if (it == ids.end()) {
    LOG(WARNING) << "Map with feature sum " << sum
                 << " not found, mapping to 0";
    ids[sum] = 0;
    return 0;
  }
  return it->second;
}

StaticData::StaticData(State* state) {
  auto bbox =
      Rect::centeredWithSize(state->mapRect().center(), kMapSize, kMapSize);
  map = featurizePlain(
      state,
      {PlainFeatureType::Walkability,
       PlainFeatureType::Buildability,
       PlainFeatureType::GroundHeight,
       PlainFeatureType::StartLocations},
      bbox);
  assert(map.numChannels() == kNumMapChannels);

  race[0] = state->myRace()._to_integral();
  race[1] = state->board()->get<int>(Blackboard::kEnemyRaceKey);
  opponentName = state->board()->get<std::string>(Blackboard::kEnemyNameKey);
  // Supposedly this is filled out at some later stage if we extract static data
  // at the start of the game
  won = state->won();
}

Sample::Sample(
    State* state,
    int res,
    int stride,
    std::shared_ptr<StaticData> sd)
    : staticData(std::move(sd)) {
  if (staticData == nullptr) {
    staticData = std::make_shared<StaticData>(state);
  }

  auto bbox = Rect::centeredWithSize(
      state->mapRect().center(), StaticData::kMapSize, StaticData::kMapSize);
  auto udf = UnitTypeMDefoggerFeaturizer();
  units = udf.toDefoggerFeature(
      udf.extract(state, state->unitsInfo().liveUnits(), bbox), res, stride);
  units.tensor = units.tensor *
      gScoreNorm().unsqueeze(1).unsqueeze(2).expand(units.tensor.sizes());

  frame = state->currentFrame();
  resources = state->resources();
  buildOrder = addRacePrefix(
      state->board()->get<std::string>(Blackboard::kBuildOrderKey),
      state->board()->get<int>(Blackboard::kEnemyRaceKey));

  // Pending upgrades and techs
  for (auto* unit : state->unitsInfo().myUnits()) {
    if (unit->upgrading() && unit->upgradingType) {
      // cf TorchCraft Controller::packResources()
      auto constexpr kNumLevelableUpgrades = 16;
      pendingUpgrades |= 1ll << unit->upgradingType->upgrade;
      if (unit->upgradingType->level == 2) {
        pendingUpgradesLevel |= 1ll << unit->upgradingType->upgrade;
      } else if (unit->upgradingType->level == 3) {
        pendingUpgradesLevel |= 1ll
            << (unit->upgradingType->upgrade + kNumLevelableUpgrades);
      }
    }
    if (unit->researching() && unit->researchingType) {
      pendingTechs |= 1ll << unit->researchingType->tech;
    }
  }

  if (resources.ore < 0 || resources.gas < 0 || resources.used_psi < 0 ||
      resources.total_psi < 0) {
    VLOG(2) << "Something is wrong: ore " << resources.ore << " gas "
            << resources.gas << " used_psi " << resources.used_psi
            << " total_psi " << resources.total_psi;
  }
}

torch::Tensor Sample::featurize(BosFeature feature, torch::Tensor buffer)
    const {
  auto destTensor = [&](at::IntList sizes,
                        torch::TensorOptions options) -> torch::Tensor {
    if (!buffer.defined()) {
      return torch::zeros(sizes, options);
    } else {
      return buffer.to(options.dtype()).resize_(sizes);
    }
  };

  switch (feature) {
    case BosFeature::Map: {
      if (buffer.defined()) {
        auto mapt = staticData->map.tensor;
        buffer.to(mapt.options().dtype()).resize_as_(mapt).copy_(mapt);
      } else {
        return staticData->map.tensor;
      }
      return buffer;
    }
    case BosFeature::MapId: {
      auto t = destTensor({1}, torch::kI64);
      t[0] = mapId(staticData->map.tensor);
      return t;
    }
    case BosFeature::Race: {
      auto t = destTensor({2}, torch::kI64);
      t[0] = staticData->race[0];
      t[1] = staticData->race[1];
      return t;
    }
    case BosFeature::Units: {
      if (buffer.defined()) {
        auto ut = units.tensor;
        buffer.to(ut.options().dtype()).resize_as_(ut).copy_(ut);
      } else {
        return units.tensor;
      }
      return buffer;
    }
    case BosFeature::BagOfUnitCounts: {
      auto ubag = units.tensor.view({units.tensor.size(0), -1}).sum(1);
      if (buffer.defined()) {
        buffer.to(ubag.options().dtype()).resize_as_(ubag).copy_(ubag);
      } else {
        return ubag;
      }
      return buffer;
    }
    case BosFeature::BagOfUnitCountsAbs5_15_30: {
      auto udf = UnitTypeMDefoggerFeaturizer();
      auto t = destTensor({118 * 3}, torch::kFloat);
      if (nextAbboStates.empty()) {
        t.fill_(0.0f);
        return t;
      }
      auto acc = t.accessor<float, 1>();
      auto j = 0;
      auto const& st5 = nextAbboStates.at(5 * 24);
      for (auto i = 0; i < 118; i++) {
        auto const& bt = getUnitBuildType(udf.unmapType(i));
        acc[j++] =
            autobuild::countPlusProduction(st5, bt) * bt->gScore * kGScoreScale;
      }
      auto const& st15 = nextAbboStates.at(15 * 24);
      for (auto i = 0; i < 118; i++) {
        auto const& bt = getUnitBuildType(udf.unmapType(i));
        acc[j++] = autobuild::countPlusProduction(st15, bt) * bt->gScore *
            kGScoreScale;
      }
      auto const& st30 = nextAbboStates.at(30 * 24);
      for (auto i = 0; i < 118; i++) {
        auto const& bt = getUnitBuildType(udf.unmapType(i));
        acc[j++] = autobuild::countPlusProduction(st30, bt) * bt->gScore *
            kGScoreScale;
      }
      return t;
    }
    case BosFeature::Resources5Log: {
      auto t = destTensor({4}, torch::kFloat);
      auto acc = t.accessor<float, 1>();
      acc[0] = std::log(float(std::max(resources.ore, 0)) / 5 + 1);
      acc[1] = std::log(float(std::max(resources.gas, 0)) / 5 + 1);
      acc[2] = std::log(float(std::max(resources.used_psi, 0)) / 5 + 1);
      acc[3] = std::log(float(std::max(resources.total_psi, 0)) / 5 + 1);
      return t;
    }
    case BosFeature::TechUpgradeBits: {
      auto t = destTensor({142}, torch::kFloat);
      auto acc = t.accessor<float, 1>();
      auto ti = 0;
      for (auto j = 0; j < 63; j++) {
        acc[ti++] = (resources.upgrades & (1ll << j)) != 0;
      }
      for (auto j = 0; j < 32; j++) {
        acc[ti++] = (resources.upgrades_level & (1ll << j)) != 0;
      }
      for (auto j = 0; j < 47; j++) {
        acc[ti++] = (resources.techs & (1ll << j)) != 0;
      }
      return t;
    }
    case BosFeature::PendingTechUpgradeBits: {
      auto t = destTensor({142}, torch::kFloat);
      auto acc = t.accessor<float, 1>();
      auto ti = 0;
      for (auto j = 0; j < 63; j++) {
        acc[ti++] = (pendingUpgrades & (1ll << j)) != 0;
      }
      for (auto j = 0; j < 32; j++) {
        acc[ti++] = (pendingUpgradesLevel & (1ll << j)) != 0;
      }
      for (auto j = 0; j < 47; j++) {
        acc[ti++] = (pendingTechs & (1ll << j)) != 0;
      }
      return t;
    }
    case BosFeature::TimeAsFrame: {
      auto t = destTensor({1}, torch::kI64);
      t[0] = frame;
      return t;
    }
    case BosFeature::ActiveBo: {
      auto t = destTensor({1}, torch::kI64);
      t[0] = buildOrderId(buildOrder);
      return t;
    }
    case BosFeature::NextBo: {
      auto t = destTensor({1}, torch::kI64);
      t[0] = buildOrderId(nextBuildOrder);
      return t;
    }
    default:
      break;
  }
  return torch::Tensor();
}

ag::tensor_list Sample::featurize(std::vector<BosFeature> features) const {
  ag::tensor_list dest;
  torch::NoGradGuard ng;
  for (auto const& feat : features) {
    dest.push_back(featurize(feat));
  }
  return dest;
}

void Sample::renormV2Features() {
  // v2 features have been saved with a simple counts / 10
  if (units.tensor.defined()) {
    torch::NoGradGuard ng;
    auto norm = 10 * gScoreNorm();
    units.tensor = units.tensor *
        norm.unsqueeze(1).unsqueeze(2).expand(units.tensor.sizes());
  }
}

std::map<int, autobuild::BuildState> Sample::simulateAbbo(
    State* state,
    std::string const& buildOrder,
    std::vector<int> const& frameOffsets) {
  if (!std::is_sorted(frameOffsets.begin(), frameOffsets.end())) {
    throw std::runtime_error("Frame offsets must be sorted for simulateAbbo");
  }
  std::map<int, autobuild::BuildState> states;

  auto task = buildorders::createTask(kRootUpcId, buildOrder, state, nullptr);

  auto st = autobuild::getMyState(state);
  int o = 0;
  for (int t : frameOffsets) {
    task->simEvaluateFor(st, t - o);
    states[t] = st;
    o = t;
  }

  return states;
}

} // namespace bos
} // namespace cherrypi
