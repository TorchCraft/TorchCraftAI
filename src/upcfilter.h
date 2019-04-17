/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "upc.h"

namespace cherrypi {

class Module;
class State;

/**
 * Base class for UPC filters.
 *
 * Note that the ownership of UPCs are yielded to the blackboard when posted.
 * These filters are then applied to each UPC to ensure validity.
 * If Upcs are slightly incorrect but fixable, then they will be modified to
 * comply with the specification. Otherwise, if they are wrong and we cannot fix
 * them, the "filter" method should return false, indicating a wrong UPC. In
 * that case, the corresponding UPC should not be posted in the blackboard.
 *
 * Possible use cases for UPC filters are consistency checks (which would log a
 * warning if simple conditions are violated, for example) or enforcing unit
 * allocations according to a set of rules based on which tasks are currently
 * holding on to units. See below for a concrete application of the latter.
 */
class UPCFilter {
 public:
  UPCFilter() {}
  virtual ~UPCFilter() {}

  // This function ensures that the UPC is valid. If that is the case (possibly
  // after being fixed), it returns true, and false otherwise
  virtual bool
  filter(State* state, std::shared_ptr<UPCTuple> upc, Module* origin) = 0;
};

/**
 * Removes units from an UPC that are allocated to high-priority tasks.
 */
class AssignedUnitsFilter : public UPCFilter {
 public:
  using UPCFilter::UPCFilter;
  virtual ~AssignedUnitsFilter() {}

  bool filter(State* state, std::shared_ptr<UPCTuple> upc, Module* origin)
      override;
};

/**
 * Try to fix the malformed UPCs. It does two checks:
 * - Remove nullptr from unit, positionU, and createType
 * - Clamp probabilities in [0,1]
 * Note that all UPCs will be accepted (after being fixed)
 */
class SanityFilter : public UPCFilter {
 public:
  using UPCFilter::UPCFilter;
  virtual ~SanityFilter() {}

  bool filter(State* state, std::shared_ptr<UPCTuple> upc, Module* origin)
      override;
};

} // namespace cherrypi
