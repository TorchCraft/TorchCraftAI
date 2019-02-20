/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "buildtype.h"
#include "cherrypi.h"
#include "module.h"
#include "state.h"

#include <array>
#include <deque>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cherrypi {

namespace autobuild {

struct BuildStateUnit {
  const BuildType* type = nullptr;
  int busyUntil = 0;
  const BuildType* addon = nullptr;
  int larvaTimer = 0;
};

struct BuildEntry {
  const BuildType* type = nullptr;
  Position pos;
  std::function<void()> builtCallback;

  bool operator==(BuildEntry n) const {
    return type == n.type && pos == n.pos;
  }
  bool operator!=(BuildEntry n) const {
    return type != n.type || pos != n.pos;
  }
};

/**
 * Describes a state of the game, either now or in a hypothetical future,
 * for use in AutoBuilds.
 *
 * At the start of an AutoBuild, this reflects the current game state.
 * At each buildStep(), the BuildState is updated to reflect the
 * units/upgrades/tech purchased in the previous buildSteps().
 */
struct BuildState {
  int frame = 0;
  int race = 0;
  double minerals = 0;
  double gas = 0;
  double mineralsPerFramePerGatherer = 0;
  double gasPerFramePerGatherer = 0;
  std::array<double, 3> usedSupply{};
  std::array<double, 3> maxSupply{};
  std::array<double, 3> inprodSupply{};
  std::unordered_map<const BuildType*, std::vector<BuildStateUnit>> units;
  std::unordered_set<const BuildType*> upgradesAndTech;
  std::deque<std::pair<int, const BuildType*>> production;
  std::vector<std::pair<int, BuildEntry>> buildOrder;
  std::vector<BuildStateUnit> morphingHatcheries;

  int workers = 0;
  int refineries = 0;
  int availableGases = 0;

  bool autoBuildRefineries = true;
  bool autoBuildHatcheries = true;
  bool isExpanding = false;
};

// Cerealization
// XXX This is quite a hack, but for now we can't build this on Windows. We'll
// have to figure this out together with the Windows build. If we can't get it
// to work it's not dramatic, we just might have to add a few ifdefs to the BOS
// training code.
template <class Archive>
void save(Archive& archive, BuildStateUnit const& stu);
template <class Archive>
void load(Archive& archive, BuildStateUnit& stu);
template <class Archive>
void save(Archive& archive, BuildEntry const& e);
template <class Archive>
void load(Archive& archive, BuildEntry& e);
template <class Archive>
void save(Archive& archive, BuildState const& stu);
template <class Archive>
void load(Archive& archive, BuildState& stu);

} // namespace autobuild

/**
 * AutoBuildTasks are "build orders" in the colloquial sense.
 * You can subclass AutoBuildTask to design a build order.
 *
 * In practice, build orders are usually based on a subclass of AutoBuildTask
 * called ABBOBase (AutoBuild Build Order Base) which provides handy
 * convenience methods.
 */
class AutoBuildTask : public MultiProxyTask {
 private:
  autobuild::BuildState targetBuildState;
  Module* module_ = nullptr;

 public:
  int lastEvaluate = 0;

  autobuild::BuildState initialBuildState;
  autobuild::BuildState currentBuildState;

  State* state_ = nullptr;

  bool isSimulation = false;

  /// Each of these UPCs is being proxied by this task.
  std::unordered_map<UpcId, std::tuple<autobuild::BuildEntry, float>>
      scheduledUpcs;
  std::function<bool(autobuild::BuildState&)> queue;

  void postBlackboardKey(std::string const& key, Blackboard::Data const& data);
  bool cancelGas();

  AutoBuildTask(int upcId, State* state, Module* module)
      : MultiProxyTask({}, upcId), module_(module), state_(state) {
    setStatus(TaskStatus::Ongoing);
  }
  virtual ~AutoBuildTask() override {}

  virtual void update(State* state) override;

  void evaluate(State* state, Module* module);

  void simEvaluateFor(autobuild::BuildState& st, FrameNum frames);

  /**
   * One of the three steps of processing a build order.
   * preBuild() is invoked once before stepping through the build order.
   *
   * Because it's invoked exactly once (unlike buildStep() which is invoked
   * repeatedly) preBuild is a good place for making decisions that don't
   * directly involve the build queue, like deciding whether to attack or
   * deciding whether to mine gas.
   */
  virtual void preBuild(autobuild::BuildState& st) {}

  /**
   * The meat of a build order lives in buildStep(). This is where you decide
   * what to build, and in what order.
   *
   * Here's how buildStep() works:
   *
   * After preBuild(), AutoBuildTask invokes buildStep() with a BuildState
   * reflecting the current state of the game. That includes the amount of
   * minerals, gas, and supply you have. It also has an empty queue of things
   * to build.
   *
   * In your implementation of buildStep(), you can specify everything you want
   * to build, via methods like build() and upgrade(). These methods add
   * your requested items to the queue in BuildState.
   *
   * However, *only the last request* which would modify the queue actually
   * modifies the BuildState. This request goes to the end of the build queue.
   * The BuildState updates to reflect the request: minerals/gas is spent,
   * supply is updated, etc.
   *
   * Then, if anything was changed, buildStep() is invoked again with the new
   * BuildState. The last request from the previous build is now reflected in
   * the new BuildState. For example, if you have no Spawning Pool, and the
   * last request was buildN(Spawning_Pool, 1), the BuildState will now
   * contain the Spawning Pool you requested.
   *
   * This continues until either the queue has reached a certain length
   * (several minutes into the future) or all requests are enqueued.
   */
  virtual void buildStep(autobuild::BuildState& st) {}

  /**
   * The final step of processing a build order.
   * postBuild() is invoked after the last invocation of buildStep().
   */
  virtual void postBuild(autobuild::BuildState& st) {}

  /// Builds a building at a specific position.
  /// Invokes a callback when we attempt to begin construction.
  /// Enqueues any required but missing prerequisites
  /// (like adding a Lair if you request a Spire).
  void build(
      const BuildType* type,
      Position pos,
      std::function<void()> builtCallback);

  /// Builds a unit/building, upgrade, or technology.
  /// Invokes a callback when we attempt to begin construction.
  /// Enqueues any required but missing prerequisites
  /// (like adding a Spire if you request a Mutalisk).
  void build(const BuildType* type, std::function<void()> builtCallback);

  /// Builds a building at a specific position.
  /// Enqueues any required but missing prerequisites
  /// (like adding a Lair if you request a Spire).
  void build(const BuildType* type, Position pos);

  /// Builds a unit/building, upgrade, or technology.
  /// Enqueues any required but missing prerequisites
  /// (like adding a Lair if you request a Spire).
  void build(const BuildType* type);

  /// Builds up to N of a unit/building.
  /// Enqueues any required but missing prerequisites
  /// like adding a Lair if you request a Spire).
  bool buildN(const BuildType* type, int n);

  /// Builds up to N of a unit/building,
  /// with an optional limit on how many to build consecutively.
  /// Enqueues any required but missing prerequisites
  /// like adding a Lair if you request a Spire).
  bool buildN(const BuildType* type, int n, int simultaneous);

  /// Builds up to N of a building, specifying a position for the next
  /// new building.
  /// Enqueues any required but missing prerequisites
  /// (like adding a Lair if you request a Spire).
  bool buildN(const BuildType* type, int n, Position positionIfWeBuildMore);

  /// Researches an Upgrade or Tech
  /// Enqueues any required but missing prerequisites
  /// (like adding a Lair if you request Lurker Aspect).
  bool upgrade(const BuildType* type);

  autobuild::BuildState& lastEvaluateCurrentState() {
    return initialBuildState;
  }
  autobuild::BuildState& lastEvaluateTargetState() {
    return targetBuildState;
  }

 private:
  std::string frameToString(State* state);
  std::vector<std::vector<std::string>> unitsToString(State* state);
  std::vector<std::vector<std::string>> productionToString(State* state);
  std::vector<std::vector<std::string>> queueToString(State* state);
  std::vector<std::string> upgradesToString(State* state);
  std::vector<std::string> describeState(State* state);
  int logInvocations = 0;
  void log(State* state);

 protected:
  /**
   * Draws any debugging information to the screen
   */
  virtual void draw(State* state);
};

class AutoBuildModule : public Module {
 public:
  virtual ~AutoBuildModule() = default;

  virtual std::shared_ptr<AutoBuildTask>
  createTask(State* state, int srcUpcId, std::shared_ptr<UPCTuple> srcUpc);

  virtual void checkForNewUPCs(State* state);

  virtual void step(State* state) override;
};

/**
 * A very simple build order which builds a fixed list of targets and then
 * stops.
 *
 * Intended for testing purposes.
 */
class DefaultAutoBuildTask : public AutoBuildTask {
 public:
  struct Target {
    BuildType const* type;
    int n;

    Target(BuildType const* type, int n = -1) : type(type), n(n) {}
  };

  std::vector<Target> targets;

  DefaultAutoBuildTask(
      int upcId,
      State* state,
      Module* module,
      std::vector<Target> targets)
      : AutoBuildTask(upcId, state, module), targets(std::move(targets)) {}
  virtual ~DefaultAutoBuildTask() override {}

  virtual void buildStep(autobuild::BuildState& st) override {
    for (auto& target : targets) {
      if (target.n == -1) {
        build(target.type);
      } else {
        buildN(target.type, target.n);
      }
    }
  }
};

namespace autobuild {
BuildState getMyState(State* state);

/// Returns true if we have this unit in this BuildState.
bool hasUnit(const BuildState& st, const BuildType* type);

/// Returns true if we have this upgrade in this BuildState.
bool hasUpgrade(const BuildState& st, const BuildType* type);

/// Returns true if we have this tech in this BuildState.
bool hasTech(const BuildState& st, const BuildType* type);

/// Returns true if we have any of this BuildType completed in this BuildState.
bool has(const BuildState& st, const BuildType* type);

/// Returns the number of completed units of this BuildType in this BuildState.
int countUnits(const BuildState& st, const BuildType* type);

/// Returns true if we are currently producing this BuildType in this
/// BuildState.
bool isInProduction(const BuildState& st, const BuildType* type);

/// Returns true if we have this BuildType in this
/// BuildState, either complete or in production.
bool hasOrInProduction(const BuildState& st, const BuildType* type);

/// Returns how many frames before a type will be available
/// Defaults to kForever if the type isn't complete or enqueued
int framesUntil(const BuildState& st, const BuildType* type);

/// Returns the number of this BuildType in production in this BuildState.
int countProduction(const BuildState& st, const BuildType* type);

/// Returns the number of this BuildType this BuildState, either complete or
/// in production.
int countPlusProduction(const BuildState& st, const BuildType* type);

/// Returns the number of Larva at this Hatchery/Lair/Hive in this BuildState.
int larvaCount(const BuildState& st, const BuildStateUnit&);
} // namespace autobuild

} // namespace cherrypi
