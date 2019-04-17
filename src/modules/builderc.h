/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "controller.h"

#include "modules/builder.h"

namespace cherrypi {

/**
 * Base class for controllers used by BuilderModule.
 *
 * Builder-related tasks will select a unit when they are ready for production
 * or construction and not upon creation. Hence, relevant controllers will
 * advertise the units they're using via currentUnits(), and the module that
 * uses them will need to make sure that proper player-wide allocation is done
 * after calling step().
 */
class BuilderControllerBase : public Controller {
 public:
  BuilderControllerBase(
      Module* module,
      BuildType const* type,
      std::unordered_map<Unit*, float> unitProbs,
      std::shared_ptr<BuilderControllerData> bcdata);
  virtual ~BuilderControllerBase() = default;
  virtual const char* getName() const override {
    return "Builder";
  };

 protected:
  virtual bool didSucceed() const override {
    return succeeded_;
  }
  virtual bool didFail() const override {
    return failed_;
  }

  void grabUnit(State* state, Unit* unit);
  void releaseUnit(State* state, Unit* unit);

  bool findBuilder(State* state, Position const& pos = Position());

  auto defaultUnitBuilderScore(State* state);
  auto larvaBuilderScore(State* state, bool preferSaturation);
  auto hatcheryTechBuilderScore(State* state);

  bool cancelled(State* state) const;

 public:
  float priority() {
    return priority_;
  }
  void setPriority(float value) {
    priority_ = value;
  }
  BuildType const* type() {
    return type_;
  }

 protected:
  BuildType const* type_;
  Unit* builder_ = nullptr;
  std::unordered_map<Unit*, float> unitProbs_;
  std::shared_ptr<BuilderControllerData> bcdata_;
  bool succeeded_ = false;
  bool failed_ = false;
  float priority_;
};

/**
 * A unit production controller for units that require a worker.
 *
 * This is used by BuilderModule.
 * It is assumed that every type that this controller should produce is a
 * building.
 */
class WorkerBuilderController : public BuilderControllerBase {
 public:
  WorkerBuilderController(
      Module* module,
      BuildType const* type,
      std::unordered_map<Unit*, float> unitProbs,
      std::shared_ptr<BuilderControllerData> bcdata,
      Position pos);
  virtual ~WorkerBuilderController() = default;

  virtual void step(State* state) override;
  virtual void removeUnit(State* state, Unit* unit, UpcId id) override;
  virtual const char* getName() const override {
    return "WorkerBuilder";
  };

 protected:
  std::string logPrefix() const;

 private:
  Position pos_;
  Unit* detector_ = nullptr;
  int lastUpdate_ = 0;
  bool constructionStarted_ = false;
  int lastCheckLocation_ = 0;
  int lastMoveUnitsInTheWay_ = 0;
  int moveAttempts_ = 0;
  std::unordered_set<Unit*> movedUnits_;
  int buildAttempts_ = 0;
  std::shared_ptr<Tracker> tracker_ = nullptr;
  TrackerStatus trackerStatus_;
  bool moving_ = false;
  bool building_ = false;
};

/**
 * A unit production controller for all other units.
 *
 * This is used by BuilderModule.
 */
class BuilderController : public BuilderControllerBase {
 public:
  BuilderController(
      Module* module,
      BuildType const* type,
      std::unordered_map<Unit*, float> unitProbs,
      std::shared_ptr<BuilderControllerData> bcdata);
  virtual ~BuilderController() = default;

  virtual void step(State* state) override;
  virtual void removeUnit(State* state, Unit* unit, UpcId id) override;

 protected:
  std::string logPrefix() const;

 private:
  int lastUpdate_ = 0;
  bool constructionStarted_ = false;
  std::shared_ptr<Tracker> tracker_ = nullptr;
  TrackerStatus trackerStatus_;
};

} // namespace cherrypi
