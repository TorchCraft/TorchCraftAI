/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "snapshotter.h"

namespace microbattles {

class DefilerSnapshotter : public cherrypi::Snapshotter {
 public:
  virtual bool isCameraReady(torchcraft::State*) override;
  virtual std::string outputDirectory() override {
    return outputDirectory_;
  };

  /// Where to write Snapshots
  DefilerSnapshotter& setOutputDirectory(std::string directory) {
    outputDirectory_ = directory;
    return *this;
  };
  /// Maximum ratio of total army value to allow snapshotting.
  /// The purpose is to avoid snapshotting scenarios where one side is totally
  /// outnumbered.
  DefilerSnapshotter& setArmyValueRatioMax(double value) {
    armyValueRatioMax_ = value;
    return *this;
  }
  /// Maximum distance from enemy army to consider snapshotting, in walktiles.
  /// The purpose is to avoid snapshotting when no Defilers are involved.
  DefilerSnapshotter& setDefilerDistanceMax(double value) {
    defilerDistanceMax_ = value;
    return *this;
  }

 protected:
  double armyValueRatioMax_ = 3.0;

  // Defiler max spell range is 9 buildtiles;
  // Here we'll add some buffer to that.
  double defilerDistanceMax_ = 4 * (9 + 6);

  std::string outputDirectory_ = Snapshotter::outputDirectory();
};

} // namespace microbattles