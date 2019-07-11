/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "basetypes.h"
#include "common/language.h"
#include "state.h"

namespace cherrypi {

class Agent;

/**
 * A Behavior is a self-contained situational micro rule.
 */
class Behavior {
 public:
  /// Checks if a unit still needs a micro decision. If so, invokes onPerform()
  void perform(Agent& agent);

  virtual ~Behavior() = default;
  virtual std::string name() const = 0;

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
   public:                                          \
    virtual MicroAction onPerform(Agent&) override; \
    virtual std::string name() const override {     \
      return MAKE_STRING(NAME);                     \
    }                                               \
  };

// Class declarations for micro Behaviors
#define CPI_DEFINE_INHERIT_BEHAVIOR(NAME, NAMEP)    \
  class Behavior##NAME : public Behavior##NAMEP {   \
   public:                                          \
    virtual MicroAction onPerform(Agent&) override; \
    virtual std::string name() const override {     \
      return MAKE_STRING(NAME);                     \
    }                                               \
  };

CPI_DEFINE_BEHAVIOR(ML)
CPI_DEFINE_BEHAVIOR(Unstick)
CPI_DEFINE_BEHAVIOR(IfIrradiated)
CPI_DEFINE_BEHAVIOR(IfStormed)
CPI_DEFINE_BEHAVIOR(VsScarab)
CPI_DEFINE_BEHAVIOR(Formation)
CPI_DEFINE_BEHAVIOR(Detect)
CPI_DEFINE_BEHAVIOR(AsZergling)
CPI_DEFINE_BEHAVIOR(AsMutaliskVsScourge)
CPI_DEFINE_BEHAVIOR(AsMutaliskMicro)
CPI_DEFINE_BEHAVIOR(AsScourge)
CPI_DEFINE_BEHAVIOR(AsLurker)
CPI_DEFINE_BEHAVIOR(AsHydralisk)
CPI_DEFINE_BEHAVIOR(AsOverlord)
CPI_DEFINE_BEHAVIOR(AsDefilerMoveToBattle)
CPI_DEFINE_BEHAVIOR(AsDefilerConsumeOnly)
CPI_DEFINE_INHERIT_BEHAVIOR(AsDefiler, AsDefilerConsumeOnly)
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
typedef std::vector<std::shared_ptr<Behavior>> BehaviorList;
class BehaviorSeries : public Behavior {
 protected:
  const BehaviorList behaviors_ = {};

 public:
  BehaviorSeries(BehaviorList behaviors) : behaviors_(behaviors){};
  virtual MicroAction onPerform(Agent& agent) override;
  virtual std::string name() const override {
    return "Series";
  }
};

} // namespace cherrypi
