/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "common/datareader.h"
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

  /// Samples a Scenario from the list specified in setIndexFile.
  FixedScenario getFixedScenario() override;

 protected:
  void invalidate_();
  Snapshot loadSnapshot_();
  std::string snapshotDirectory_;
  std::string indexFile_;
  std::unique_ptr<common::DataReader<Snapshot>> dataReader_;
  std::unique_ptr<common::DataReaderIterator<Snapshot>> dataIterator_;
};

} // namespace cherrypi
