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

void MicroScenarioProviderSnapshot::invalidate_() {
  dataReader_ = nullptr;
  dataIterator_ = nullptr;
}

Snapshot MicroScenarioProviderSnapshot::loadSnapshot_() {
  if (dataReader_ == nullptr) {
    auto snapshotPaths = common::fsutils::readLines(indexFile_);
    dataReader_ = std::make_unique<common::DataReader<Snapshot>>(
        common::makeDataReader<Snapshot>(
            snapshotPaths, 1, 1, snapshotDirectory_));
  }
  if (dataIterator_ == nullptr || !dataIterator_->hasNext()) {
    dataReader_->shuffle();
    dataIterator_ = dataReader_->iterator();

    if (!dataIterator_->hasNext()) {
      throw std::runtime_error("Couldn't find any snapshots");
    }
  }
  return dataIterator_->next()[0];
}

FixedScenario MicroScenarioProviderSnapshot::getFixedScenario() {
  return snapshotToScenario(loadSnapshot_());
}

} // namespace cherrypi
