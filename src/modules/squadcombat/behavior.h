/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "basetypes.h"
#include "state.h"

namespace cherrypi {

/**
 * Represents a Behavior's decision of how to control a unit.
 */
struct MicroAction {
  /// If true: this UPC is the final decision on what this unit should do.
  /// Cease performing behaviors
  bool isFinal = false;

  /// Only used when isFinal is true.
  ///
  /// If defined: This UPC is the final decision on what this unit should do.
  ///
  /// If undefined: The final decision on what this unit should do is to
  /// *do nothing* and issue no commands.
  std::shared_ptr<UPCTuple> upc = nullptr;

  std::shared_ptr<UPCTuple> getFinalUPC() const;
};

class Agent;

/**
 * A Behavior is a self-contained situational micro rule.
 */
class Behavior {
 public:
  /// Checks if a unit still needs a micro decision. If so, invokes onPerform()
  void perform(Agent& agent);

  virtual ~Behavior() = default;

 protected:
  /// Convenience method: Form a MicroAction reflecting a decision to
  /// issue a UPC for this unit.
  MicroAction doAction(std::shared_ptr<UPCTuple> upc) const {
    return MicroAction{true, upc};
  }
  /// Convenience: A MicroAction reflecting a decision to
  /// ignore this unit and let another Behavior control it.
  const MicroAction pass = MicroAction();

  /// Convenience: A MicroAction reflecting a decision to
  /// do nothing with this unit and let no other Behavior control it.
  const MicroAction doNothing = doAction(nullptr);

  /// Decide what to do with a unit that has not yet been controlled by a
  /// Behavior.
  virtual MicroAction onPerform(Agent&) = 0;
};

// Class declarations for micro Behaviors
#define CPI_DEFINE_BEHAVIOR(NAME)                   \
  class Behavior##NAME : public Behavior {          \
    virtual MicroAction onPerform(Agent&) override; \
  };
#define CPI_ADD_BEHAVIOR(NAME) std::make_shared<Behavior##NAME>()
CPI_DEFINE_BEHAVIOR(Unstick)
CPI_DEFINE_BEHAVIOR(IfIrradiated)
CPI_DEFINE_BEHAVIOR(IfStormed)
CPI_DEFINE_BEHAVIOR(VsScarab)
CPI_DEFINE_BEHAVIOR(Formation)
CPI_DEFINE_BEHAVIOR(AsZergling)
CPI_DEFINE_BEHAVIOR(AsMutaliskVsScourge)
CPI_DEFINE_BEHAVIOR(AsMutaliskMicro)
CPI_DEFINE_BEHAVIOR(AsScourge)
CPI_DEFINE_BEHAVIOR(AsLurker)
CPI_DEFINE_BEHAVIOR(AsHydralisk)
CPI_DEFINE_BEHAVIOR(AsOverlord)
CPI_DEFINE_BEHAVIOR(Chase)
CPI_DEFINE_BEHAVIOR(Kite)
CPI_DEFINE_BEHAVIOR(EngageCooperatively)
CPI_DEFINE_BEHAVIOR(Engage)
CPI_DEFINE_BEHAVIOR(Leave)
CPI_DEFINE_BEHAVIOR(Travel)

/**
 * Gives a series of Behaviors the option of issuing a UPC for the unit.
 * Continues until a Behavior either:
 * * Issues a UPC, indicating a command for the unit
 * * Issues a null UPC, indicating that the unit should be left alone
 */
class BehaviorSeries : public Behavior {
 protected:
  typedef std::vector<std::shared_ptr<Behavior>> BehaviorList;
  virtual const BehaviorList& behaviors() const = 0;
  virtual MicroAction onPerform(Agent& agent) override;
};

/**
 * The default top-level Behavior for Delete UPCs.
 * Selects an appropriate Behavior.
 */
class BehaviorDelete : public BehaviorSeries {
 private:
  const BehaviorList behaviors_ = {CPI_ADD_BEHAVIOR(Unstick),
                                   CPI_ADD_BEHAVIOR(IfIrradiated),
                                   CPI_ADD_BEHAVIOR(IfStormed),
                                   CPI_ADD_BEHAVIOR(VsScarab),
                                   CPI_ADD_BEHAVIOR(Formation),
                                   CPI_ADD_BEHAVIOR(AsZergling),
                                   CPI_ADD_BEHAVIOR(AsMutaliskVsScourge),
                                   CPI_ADD_BEHAVIOR(AsMutaliskMicro),
                                   CPI_ADD_BEHAVIOR(AsScourge),
                                   CPI_ADD_BEHAVIOR(AsLurker),
                                   CPI_ADD_BEHAVIOR(AsHydralisk),
                                   CPI_ADD_BEHAVIOR(AsOverlord),
                                   CPI_ADD_BEHAVIOR(Chase),
                                   CPI_ADD_BEHAVIOR(Kite),
                                   CPI_ADD_BEHAVIOR(EngageCooperatively),
                                   CPI_ADD_BEHAVIOR(Engage),
                                   CPI_ADD_BEHAVIOR(Leave),
                                   CPI_ADD_BEHAVIOR(Travel)};
  virtual const BehaviorList& behaviors() const override {
    return behaviors_;
  }
};

/**
 * The default top-level Behavior for Flee UPCs.
 * Selects an appropriate Behavior.
 */
class BehaviorFlee : public BehaviorSeries {
 private:
  const BehaviorList behaviors_ = {CPI_ADD_BEHAVIOR(Unstick),
                                   CPI_ADD_BEHAVIOR(IfIrradiated),
                                   CPI_ADD_BEHAVIOR(IfStormed),
                                   CPI_ADD_BEHAVIOR(AsZergling),
                                   CPI_ADD_BEHAVIOR(AsLurker),
                                   CPI_ADD_BEHAVIOR(Kite),
                                   CPI_ADD_BEHAVIOR(Travel)};
  virtual const BehaviorList& behaviors() const override {
    return behaviors_;
  }
};

#undef CPI_DEFINE_BEHAVIOR
#undef CPI_ADD_BEHAVIOR

} // namespace cherrypi
