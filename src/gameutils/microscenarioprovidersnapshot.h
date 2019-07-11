/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "common/datareader.h"
#include "mapmatcher.h"
#include "microscenarioprovider.h"
#include "snapshotter.h"

namespace cherrypi {

/**
 * Provides scenarios constructed from Snapshots of real games.
 */
class MicroScenarioProviderSnapshot : public MicroScenarioProvider {
 public:
  /// Specifies the directory where snapshots are located.
  MicroScenarioProviderSnapshot& setSnapshotDirectory(const std::string&);

  /// Specifies a file lists snapshots relative to the SnapshotDirectory.
  MicroScenarioProviderSnapshot& setIndexFile(const std::string&);

  /// Specifies how to partition the Snapshot list,
  /// dividing it into this many partitions
  MicroScenarioProviderSnapshot& setPartitionSize(int);

  /// Specifies how to partition the Snapshot list,
  /// taking this Snapshot from each parititon
  MicroScenarioProviderSnapshot& setPartitionIndex(int);

  MicroScenarioProviderSnapshot& setUseEachSnapshotTimes(size_t);

  /// Samples a Scenario from the list specified in setIndexFile.
  FixedScenario getFixedScenario() override;

 protected:
  void invalidate_();
  Snapshot loadSnapshot_();
  std::string snapshotDirectory_;
  std::string indexFile_;
  unsigned partitionIndex_ = 0;
  unsigned partitionSize_ = 1;
  size_t idx_ = 0;
  size_t useEachSnapshotTimes_ = 1;
  std::vector<std::pair<std::string, size_t>> partitionedPaths_;
  MapMatcher mapMatcher_;
};

} // namespace cherrypi
