/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "microscenarioprovidersnapshot.h"
#include "common/fsutils.h"

namespace cherrypi {

MicroScenarioProviderSnapshot&
MicroScenarioProviderSnapshot::setSnapshotDirectory(
    const std::string& directory) {
  snapshotDirectory_ = directory;
  invalidate_();
  return *this;
}

MicroScenarioProviderSnapshot& MicroScenarioProviderSnapshot::setIndexFile(
    const std::string& file) {
  indexFile_ = file;
  invalidate_();
  return *this;
}

MicroScenarioProviderSnapshot& MicroScenarioProviderSnapshot::setPartitionSize(
    int value) {
  partitionSize_ = value;
  invalidate_();
  return *this;
}

MicroScenarioProviderSnapshot& MicroScenarioProviderSnapshot::setPartitionIndex(
    int value) {
  partitionIndex_ = value;
  invalidate_();
  return *this;
}

MicroScenarioProviderSnapshot&
MicroScenarioProviderSnapshot::setUseEachSnapshotTimes(size_t value) {
  useEachSnapshotTimes_ = value;
  invalidate_();
  return *this;
}

void MicroScenarioProviderSnapshot::invalidate_() {
  idx_ = 0;
  partitionedPaths_.clear();
}

Snapshot MicroScenarioProviderSnapshot::loadSnapshot_() {
  if (partitionedPaths_.empty()) {
    auto snapshotPaths = common::fsutils::readLines(indexFile_);
    for (auto i = 0u; i < snapshotPaths.size(); ++i) {
      if (i % partitionSize_ == partitionIndex_) {
        partitionedPaths_.emplace_back(
            std::make_pair(std::move(snapshotPaths[i]), useEachSnapshotTimes_));
      }
    }
  }
  if (idx_ == 0 || idx_ >= partitionedPaths_.size()) {
    idx_ = 0;
    std::shuffle(
        std::begin(partitionedPaths_),
        std::end(partitionedPaths_),
        common::Rand::makeRandEngine<std::mt19937>());
    if (partitionedPaths_[0].second != useEachSnapshotTimes_) {
      for (auto& pair : partitionedPaths_) {
        pair.second = useEachSnapshotTimes_;
      }
    }
  }
  if (partitionedPaths_.empty()) {
    throw std::runtime_error("No snapshots path found.");
  }
  lastScenarioName_ = partitionedPaths_[idx_].first;
  Snapshot snapshot;
  common::zstd::ifstream is(
      snapshotDirectory_ + "/" + partitionedPaths_[idx_].first);
  cereal::BinaryInputArchive archive(is);
  archive(snapshot);
  partitionedPaths_[idx_].second -= 1;
  if (partitionedPaths_[idx_].second == 0) {
    idx_++;
  }
  return snapshot;
}

FixedScenario MicroScenarioProviderSnapshot::getFixedScenario() {
  // Only get scenarios with a valid map
  auto snapshot = loadSnapshot_();
  auto scenario = snapshotToScenario(snapshot);
  mapMatcher_.setMapPrefix(mapPathPrefix_);
  scenario.map = mapMatcher_.tryMatch(snapshot.mapTitle);
  scenario.gameType = GameType::UseMapSettings;
  scenario.reward = []() { return defilerFullGameCombatReward(); };
  return scenario;
}

} // namespace cherrypi
