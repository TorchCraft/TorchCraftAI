/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "tracker.h"

#include <glog/logging.h>

namespace cherrypi {

bool Tracker::update(State* state) {
  switch (status_) {
    case TrackerStatus::NotTracking:
      if (++time_ > timeout_) {
        VLOG(2) << "Tracker timed out";
        status_ = TrackerStatus::Timeout;
        return true;
      }
      if (updateNotTracking(state)) {
        if (status_ == TrackerStatus::Pending) {
          time_ = 0;
        }
        return true;
      }
      break;

    case TrackerStatus::Pending:
      if (++time_ > timeout_) {
        VLOG(2) << "Tracker timed out";
        status_ = TrackerStatus::Timeout;
        return true;
      }
      if (updatePending(state)) {
        if (status_ == TrackerStatus::Ongoing) {
          time_ = 0;
        }
        return true;
      }
      break;

    case TrackerStatus::Ongoing:
      return updateOngoing(state);

    default:
      break;
  }

  return false;
}

} // namespace cherrypi
