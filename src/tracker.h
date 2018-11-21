/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "cherrypi.h"

namespace cherrypi {

class State;

enum class TrackerStatus {
  /// Haven't found the target that should be tracker yet
  NotTracking,
  /// Corresponding order picked up but not being actually executed yet (e.g.
  /// worker moving to building location)
  Pending,
  /// NotTracking or Pending for too long
  Timeout,
  /// Corresponding target is being executed
  Ongoing,
  /// Corresponding target finished succesfully
  Success,
  /// Corresponding target aborted and no chance of automatic recovery
  Failure,
  /// Tracker was cancelled externally
  Cancelled,
};

/**
 * Abstract base class for Trackers.
 *
 * A tracker monitors the execution of a given target. For example, this could
 * be the command issued in the game (via CommandTracker) or the implementation
 * of an UPCSTuple (via TaskTracker).  At any given time, the tracker will
 * provide a status assumption regarding the target. It will end up in either
 * `Timeout`, `Success` or `Failure`. See TrackerStatus for a description of the
 * individual modes.
 *
 * Tracker implementations provide update functions for the NotTracking, Pending
 * and Ongoing status. Timeouts are handled by the base class.
 */
class Tracker {
 public:
  Tracker(int timeout) : timeout_(timeout) {}
  virtual ~Tracker() {}

  TrackerStatus status() const {
    return status_;
  }

  void cancel() {
    status_ = TrackerStatus::Cancelled;
  }
  bool failed() const {
    return status_ == TrackerStatus::Failure ||
        status_ == TrackerStatus::Timeout;
  }
  bool succeeded() const {
    return status_ == TrackerStatus::Success;
  }

  /// Updates the tracker.
  /// Returns true if status has changed
  virtual bool update(State* s);

 protected:
  /// Updates the tracker if its status is NotTracking.
  /// Returns true if status has changed
  virtual bool updateNotTracking(State* s) = 0;
  /// Updates the tracker if its status is Pending.
  /// Returns true if status has changed
  virtual bool updatePending(State* s) = 0;
  /// Updates the tracker if its status is Ongoing.
  /// Returns true if status has changed
  virtual bool updateOngoing(State* s) = 0;

  TrackerStatus status_ = TrackerStatus::NotTracking;
  int time_ = 0;
  int timeout_;
};

} // namespace cherrypi
