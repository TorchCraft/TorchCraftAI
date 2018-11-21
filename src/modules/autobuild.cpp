/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "autobuild.h"
#include "builderhelper.h"
#include "buildtype.h"
#include "fmt/format.h"
#include "state.h"
#include "utils.h"

#include <BWAPI.h>
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/types/deque.hpp>
#include <cereal/types/unordered_set.hpp>
#include <cereal/types/utility.hpp>
#include <cereal/types/vector.hpp>
#include <fmt/format.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include <algorithm>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <tuple>
#include <utility>

// This flag is distinct from VLOG level for performance
DEFINE_bool(
    autobuild_verbose,
    false,
    "Enable (very) verbose logging of the build steps.");
DEFINE_bool(
    autobuild_draw,
    false,
    "Draw Autobuild state on the screen. Displays the same information as "
    "autobuild_print. Intermittently causes OpenBW to crash but should work on "
    "Windows.");
DEFINE_int32(autobuild_log_period, 10, "Log autobuild state 1/N of the time");
DEFINE_bool(
    autobuild_manual_gas,
    true,
    "Respect gas worker limits manually set by build orders");

namespace cherrypi {

REGISTER_SUBCLASS_0(Module, AutoBuildModule);

namespace autobuild {

thread_local int buildLogDepth = 0;
std::string buildLogIndent() {
  return std::string(buildLogDepth * 2, ' ');
}

bool hasUnit(const BuildState& st, const BuildType* type) {
  auto i = st.units.find(type);
  if (i == st.units.end()) {
    return false;
  }
  return !i->second.empty();
}

bool hasUpgrade(const BuildState& st, const BuildType* type) {
  return st.upgradesAndTech.find(type) != st.upgradesAndTech.end();
}

bool hasTech(const BuildState& st, const BuildType* type) {
  return st.upgradesAndTech.find(type) != st.upgradesAndTech.end();
}

bool has(const BuildState& st, const BuildType* type) {
  if (type->isUnit()) {
    return hasUnit(st, type);
  } else {
    return st.upgradesAndTech.find(type) != st.upgradesAndTech.end();
  }
}

int countUnits(const BuildState& st, const BuildType* type) {
  if (type == buildtypes::Zerg_Larva) {
    int r = 0;
    for (auto& v : st.units) {
      const BuildType* t = v.first;
      if (t == buildtypes::Zerg_Hatchery || t == buildtypes::Zerg_Lair ||
          t == buildtypes::Zerg_Hive) {
        for (auto& u : v.second) {
          r += larvaCount(st, u);
        }
      }
    }
    return r;
  }
  auto i = st.units.find(type);
  if (i == st.units.end()) {
    return 0;
  }
  return i->second.size();
}

bool isInProduction(const BuildState& st, const BuildType* type) {
  for (auto& v : st.production) {
    if (v.second == type) {
      return true;
    }
  }
  return false;
}

bool hasOrInProduction(const BuildState& st, const BuildType* type) {
  return has(st, type) || isInProduction(st, type);
}

int framesUntil(const BuildState& st, const BuildType* type) {
  if (has(st, type)) {
    return 0;
  }
  for (auto& v : st.production) {
    if (v.second == type) {
      return v.first - st.frame;
    }
  }
  return kForever;
}

int countProduction(const BuildState& st, const BuildType* type) {
  int r = 0;
  for (auto& v : st.production) {
    if (v.second == type) {
      ++r;
    }
  }
  return r;
}
int countPlusProduction(const BuildState& st, const BuildType* type) {
  int r = 0;
  if (type->isUnit()) {
    r += countUnits(st, type);
  } else if (has(st, type)) {
    ++r;
  }
  r += countProduction(st, type);
  if (type == buildtypes::Zerg_Hatchery) {
    return r + countPlusProduction(st, buildtypes::Zerg_Lair);
  }
  if (type == buildtypes::Zerg_Lair) {
    return r + countPlusProduction(st, buildtypes::Zerg_Hive);
  }
  if (type == buildtypes::Zerg_Spire) {
    return r + countPlusProduction(st, buildtypes::Zerg_Greater_Spire);
  }
  return r;
}

int larvaCount(
    const autobuild::BuildState& st,
    const autobuild::BuildStateUnit& u) {
  return std::max(std::min((st.frame - u.larvaTimer) / kLarvaFrames, 3), 0);
}

BuildStateUnit& addUnit(BuildState& st, const BuildType* type) {
  auto& c = st.units[type];
  c.emplace_back();
  auto& r = c.back();
  r.type = type;
  if (type->isWorker) {
    ++st.workers;
  }
  if (type->isRefinery) {
    ++st.refineries;
  }
  st.usedSupply[type->race] += type->supplyRequired;
  st.maxSupply[type->race] += type->supplyProvided;
  return r;
}

void removeUnit(BuildState& st, BuildStateUnit& u) {
  st.usedSupply[u.type->race] -= u.type->supplyRequired;
  st.maxSupply[u.type->race] -= u.type->supplyProvided;
  if (u.type->isWorker) {
    --st.workers;
  }
  if (u.type->isRefinery) {
    --st.refineries;
  }
  auto& c = st.units[u.type];
  c.erase(c.begin() + (&u - c.data()));
}

template <typename list_T, typename T>
void emplaceProd(list_T& list, int frame, T t) {
  auto pred = [&](auto i) {
    if (i == list.begin()) {
      return true;
    }
    auto p = std::prev(i);
    return p->first <= frame;
  };
  for (auto i = list.end();; --i) {
    if (pred(i)) {
      list.emplace(i, frame, std::move(t));
      break;
    }
  }
}

BuildState getMyState(State* state) {
  BuildState st;
  st.frame = state->currentFrame();
  auto res = state->resources();
  st.minerals = (double)res.ore;
  st.gas = (double)res.gas;

  if (state->board()->hasKey(Blackboard::kMineralsPerFramePerGatherer)) {
    st.mineralsPerFramePerGatherer =
        state->board()->get<double>(Blackboard::kMineralsPerFramePerGatherer);
  }
  if (state->board()->hasKey(Blackboard::kGasPerFramePerGatherer)) {
    st.gasPerFramePerGatherer =
        state->board()->get<double>(Blackboard::kGasPerFramePerGatherer);
  }

  st.availableGases = 0;
  if (builderhelpers::findGeyserForRefinery(
          state, buildtypes::Zerg_Extractor, {})) {
    st.availableGases = 1;
  }

  std::unordered_map<const Unit*, int> larvaCount;
  for (const Unit* u :
       state->unitsInfo().myUnitsOfType(buildtypes::Zerg_Larva)) {
    if (u->associatedUnit) {
      ++larvaCount[u->associatedUnit];
    }
  }

  for (const Unit* u : state->unitsInfo().myUnits()) {
    const BuildType* t = u->type;
    if (t == buildtypes::Terran_Siege_Tank_Siege_Mode) {
      t = buildtypes::Terran_Siege_Tank_Tank_Mode;
    }
    if (u->upgrading() && u->upgradingType) {
      autobuild::emplaceProd(
          st.production,
          st.frame + u->remainingUpgradeResearchTime,
          u->upgradingType);
    }
    if (u->researching() && u->researchingType) {
      autobuild::emplaceProd(
          st.production,
          st.frame + u->remainingUpgradeResearchTime,
          u->researchingType);
    }
    if (t == buildtypes::Zerg_Larva) {
      continue;
    }
    if (t == buildtypes::Zerg_Egg || t == buildtypes::Zerg_Cocoon) {
      t = u->constructingType;
      if (!t) {
        continue;
      }
    }
    if (t == buildtypes::Zerg_Lurker_Egg) {
      t = buildtypes::Zerg_Lurker;
    }
    if (!u->completed() || u->morphing()) {
      autobuild::emplaceProd(
          st.production, st.frame + u->remainingBuildTrainTime, t);
      if (t->isTwoUnitsInOneEgg) {
        autobuild::emplaceProd(
            st.production, st.frame + u->remainingBuildTrainTime, t);
      }
      if (t == buildtypes::Zerg_Lair || t == buildtypes::Zerg_Hive) {
        st.morphingHatcheries.emplace_back();
        auto& stU = st.morphingHatcheries.back();
        stU.type = t;
        stU.busyUntil = st.frame + u->remainingUpgradeResearchTime;
        stU.larvaTimer = st.frame;
        auto i = larvaCount.find(u);
        if (i != larvaCount.end()) {
          stU.larvaTimer -= 342 * i->second;
        }
      }

      st.inprodSupply[t->race] += t->supplyProvided;
      continue;
    }
    auto& stU = addUnit(st, t);
    if (u->addon) {
      stU.addon = u->addon->type;
    }
    if (t == buildtypes::Terran_Nuclear_Silo && u->associatedCount) {
      stU.busyUntil = std::numeric_limits<int>::max();
    }
    if (t == buildtypes::Zerg_Hatchery || t == buildtypes::Zerg_Lair ||
        t == buildtypes::Zerg_Hive) {
      stU.busyUntil = st.frame + u->remainingUpgradeResearchTime;
      stU.larvaTimer = st.frame - 342 + u->remainingBuildTrainTime;
      auto i = larvaCount.find(u);
      if (i != larvaCount.end()) {
        stU.larvaTimer -= 342 * i->second;
      }
    } else {
      stU.busyUntil = st.frame +
          std::max(u->remainingBuildTrainTime, u->remainingUpgradeResearchTime);
    }
  }

  for (const BuildType* t : buildtypes::allUpgradeTypes) {
    if (state->getUpgradeLevel(t) >= t->level) {
      st.upgradesAndTech.insert(t);
    }
  }

  for (const BuildType* t : buildtypes::allTechTypes) {
    if (state->hasResearched(t)) {
      st.upgradesAndTech.insert(t);
    }
  }

  st.usedSupply.fill(res.used_psi / 2.0);
  st.maxSupply.fill(res.total_psi / 2.0);

  st.race = BWAPI::Races::Enum::Terran;
  int scvs = countUnits(st, buildtypes::Terran_SCV);
  int probes = countUnits(st, buildtypes::Protoss_Probe);
  int drones = countUnits(st, buildtypes::Zerg_Drone);
  int best = std::max(scvs, std::max(probes, drones));
  if (best == scvs) {
    st.race = BWAPI::Races::Enum::Terran;
  } else if (best == probes) {
    st.race = BWAPI::Races::Enum::Protoss;
  } else if (best == drones) {
    st.race = BWAPI::Races::Enum::Zerg;
  }

  return st;
}

const BuildType failedObject;
const BuildType* failed = &failedObject;
const BuildType timeoutObject;
const BuildType* timeout = &timeoutObject;
const BuildType builtdepObject;
const BuildType* builtdep = &builtdepObject;

// This function advances the state until build has been put into production,
// or endFrame is reached, whichever is first.
// Returns null on success, failed on failure, timeout if endFrame was reached,
// or some other BuildType* that needs to be built first.
const BuildType* advance(BuildState& st, BuildEntry thing, int endFrame) {
  const BuildType* const build = thing.type;
  if (st.frame >= endFrame) {
    if (FLAGS_autobuild_verbose) {
      VLOG(0) << buildLogIndent() << "advance "
              << (build ? build->name : "null") << " -> instant timeout";
    }
    return timeout;
  }

  if (build && !build->builder) {
    throw std::runtime_error("autobuild::advance: build has no builder");
  }

  using namespace buildtypes;

  const BuildType* addonRequired = nullptr;
  bool prereqInProd = false;
  if (build) {
    for (const BuildType* prereq : build->prerequisites) {
      if (prereq == Zerg_Larva)
        continue;
      // If there is a required addon which has the same builder as this type,
      // then we assume
      // that the thing can only be built from a unit which has this addon.
      if (prereq->isAddon && prereq->builder == build->builder &&
          !build->builder->isAddon) {
        addonRequired = prereq;
      }
      if (!has(st, prereq)) {
        if (prereq == Zerg_Spire && hasOrInProduction(st, Zerg_Greater_Spire))
          continue;
        else if (prereq == Zerg_Hatchery && hasOrInProduction(st, Zerg_Hive))
          continue;
        else if (prereq == Zerg_Hatchery && hasOrInProduction(st, Zerg_Lair))
          continue;
        else if (prereq == Zerg_Lair && hasOrInProduction(st, Zerg_Hive))
          continue;
        if (isInProduction(st, prereq))
          prereqInProd = true;
        else {
          if (FLAGS_autobuild_verbose) {
            VLOG(0) << buildLogIndent() << "advance "
                    << (build ? build->name : "null")
                    << " -> prereq: " << prereq->name;
          }
          return prereq;
        }
      }
    }
  }

  auto race = build ? build->race : BWAPI::Races::Enum::Terran;
  const BuildType* refinery = buildtypes::getRaceRefinery(race);
  const BuildType* supply = buildtypes::getRaceSupplyDepot(race);

  while (true) {
    while (!st.production.empty() && st.frame >= st.production.front().first) {
      const BuildType* t = st.production.front().second;
      st.production.pop_front();
      if (t->isUnit()) {
        if (t->isAddon) {
          for (auto& v : st.units[t->builder]) {
            if (st.frame >= v.busyUntil && !v.addon) {
              v.addon = t;
              break;
            }
          }
        }
        st.inprodSupply[t->race] -= t->supplyProvided;
        st.usedSupply[t->race] -= t->supplyRequired;
        addUnit(st, t);
        if (t->builder == buildtypes::Zerg_Lair ||
            t->builder == buildtypes::Zerg_Hive) {
          if (!st.morphingHatcheries.empty()) {
            st.morphingHatcheries.pop_back();
          }
        }
      } else {
        st.upgradesAndTech.insert(t);
      }
      if (prereqInProd) {
        prereqInProd = false;
        for (const BuildType* prereq : build->prerequisites) {
          if (prereq == Zerg_Larva)
            continue;
          if (!hasUnit(st, prereq)) {
            if (prereq == Zerg_Spire &&
                hasOrInProduction(st, Zerg_Greater_Spire)) {
              continue;
            } else if (
                prereq == Zerg_Hatchery && hasOrInProduction(st, Zerg_Hive)) {
              continue;
            } else if (
                prereq == Zerg_Hatchery && hasOrInProduction(st, Zerg_Lair)) {
              continue;
            } else if (
                prereq == Zerg_Lair && hasOrInProduction(st, Zerg_Hive)) {
              continue;
            }
            prereqInProd = true;
          }
        }
      }
    }

    if (build) {
      auto addBuilt = [&](const BuildType* t, bool subtractBuildTime) {
        emplaceProd(
            st.buildOrder,
            st.frame - (subtractBuildTime ? t->buildTime : 0),
            BuildEntry{t, {}});
        addUnit(st, t);
        st.minerals -= t->mineralCost;
        st.gas -= t->gasCost;
      };
      bool hasEnoughMinerals =
          build->mineralCost == 0 || st.minerals >= build->mineralCost;
      bool hasEnoughGas = build->gasCost == 0 || st.gas >= build->gasCost;

      if (st.autoBuildRefineries && st.availableGases && hasEnoughMinerals &&
          !hasEnoughGas) {
        if (st.minerals >= build->mineralCost + refinery->mineralCost) {
          addBuilt(refinery, false);
          ++st.refineries;
          --st.availableGases;
          if (FLAGS_autobuild_verbose) {
            VLOG(0) << buildLogIndent() << "advance "
                    << (build ? build->name : "null")
                    << " -> prebuilt refinery (" << refinery->name << ") ("
                    << st.refineries << "/" << st.availableGases << ")";
          }
          return builtdep;
        }
      }

      bool hasSupply = true;
      if (build->isUnit()) {
        if (build->supplyRequired && build->builder != Zerg_Mutalisk &&
            build != Protoss_Archon && build != Protoss_Dark_Archon) {
          double nextSupply =
              st.usedSupply[build->race] + build->supplyRequired;
          if (nextSupply >= 200.0) {
            if (FLAGS_autobuild_verbose) {
              VLOG(0) << buildLogIndent() << "advance "
                      << (build ? build->name : "null")
                      << " -> failed: maxed out";
            }
            return failed;
          }
          if (nextSupply >
              st.maxSupply[build->race] + st.inprodSupply[build->race]) {
            hasSupply = false;
            if ((st.maxSupply[build->race] > 10 || st.production.empty()) &&
                (nextSupply >
                     st.maxSupply[build->race] + st.inprodSupply[build->race] ||
                 (st.minerals >= build->mineralCost + supply->mineralCost &&
                  st.maxSupply[build->race] + st.inprodSupply[build->race] -
                          nextSupply <
                      30))) {
              if (st.maxSupply[build->race] < 16) {
                if (FLAGS_autobuild_verbose) {
                  VLOG(0) << buildLogIndent() << "advance "
                          << (build ? build->name : "null") << " -> supply ("
                          << supply->name << ")";
                }
                return supply;
              } else {
                addBuilt(supply, true);
                if (FLAGS_autobuild_verbose) {
                  VLOG(0) << buildLogIndent() << "advance "
                          << (build ? build->name : "null")
                          << " -> prebuilt supply (" << supply->name << ")";
                }
                return builtdep;
              }
            }
          }
        }
      }
      if (hasEnoughMinerals && hasEnoughGas && hasSupply && !prereqInProd) {
        BuildStateUnit* builder = nullptr;
        BuildStateUnit* builder2 = nullptr;
        bool builderExists = false;
        if (build->builder == Zerg_Larva) {
          int builderLarvaTimer = 0;
          for (int i = 0; i != 4 && !builder; ++i) {
            for (auto& u : i == 3
                     ? st.morphingHatcheries
                     : st.units[i == 0 ? Zerg_Hive : i == 1 ? Zerg_Lair
                                                            : Zerg_Hatchery]) {
              if (!builderExists) {
                builderExists = true;
              }
              int t = st.frame - u.larvaTimer;
              if (t >= 342 && t > builderLarvaTimer) {
                builderLarvaTimer = t;
                builder = &u;
              }
            }
          }
        } else {
          for (auto& u : st.units[build->builder]) {
            if (build->isAddon && u.addon) {
              continue;
            }
            if (addonRequired && u.addon != addonRequired) {
              continue;
            }
            if (!builderExists) {
              builderExists = true;
            }
            if (st.frame >= u.busyUntil) {
              builder = &u;
              break;
            }
          }
          if (builder &&
              (build == Protoss_Archon || build == Protoss_Dark_Archon)) {
            for (auto& u : st.units[build->builder]) {
              if (&u == builder) {
                continue;
              }
              builder2 = &u;
              break;
            }
            if (!builder2) {
              builder = nullptr;
              builderExists = false;
            }
          }
        }
        if (!builder && !builderExists) {
          if (build->builder == Zerg_Larva) {
            if (!hasOrInProduction(st, Zerg_Hatchery) &&
                !hasOrInProduction(st, Zerg_Lair) &&
                !hasOrInProduction(st, Zerg_Hive)) {
              if (FLAGS_autobuild_verbose) {
                VLOG(0) << buildLogIndent() << "advance "
                        << (build ? build->name : "null")
                        << " -> builder hatchery";
              }
              return Zerg_Hatchery;
            }
          } else {
            if (!builderExists && !isInProduction(st, build->builder)) {
              const BuildType* bt =
                  addonRequired ? addonRequired : build->builder;
              if (FLAGS_autobuild_verbose) {
                VLOG(0) << buildLogIndent() << "advance "
                        << (build ? build->name : "null") << " -> builder ("
                        << bt->name << ")";
              }
              return bt;
            }
          }
        }
        if (builder) {
          if (build->builder == Zerg_Larva) {
            if (st.frame - builder->larvaTimer >= 342 * 3) {
              builder->larvaTimer = st.frame - 342 * 2;
            } else {
              builder->larvaTimer += 342;
            }
          } else {
            builder->busyUntil = st.frame + build->buildTime;
          }
          if (build == Terran_Nuclear_Missile) {
            builder->busyUntil = std::numeric_limits<int>::max();
          }
          if (build->isResourceDepot && thing.pos != Position()) {
            st.isExpanding = true;
          }

          st.inprodSupply[build->race] += build->supplyProvided;
          st.usedSupply[build->race] += build->supplyRequired;
          emplaceProd(st.production, st.frame + build->buildTime, build);
          emplaceProd(st.buildOrder, st.frame, std::move(thing));
          if (build->isTwoUnitsInOneEgg) {
            st.inprodSupply[build->race] += build->supplyProvided;
            st.usedSupply[build->race] += build->supplyRequired;
            emplaceProd(st.production, st.frame + build->buildTime, build);
          }
          st.minerals -= build->mineralCost;
          st.gas -= build->gasCost;
          if (build->isAddon) {
            builder->addon = build;
          }
          if (builder->type->race == BWAPI::Races::Enum::Zerg &&
              build->builder != Zerg_Larva) {
            if (build->isUnit()) {
              if (builder->type == buildtypes::Zerg_Lair ||
                  builder->type == buildtypes::Zerg_Hive) {
                st.morphingHatcheries.push_back(*builder);
              }
              removeUnit(st, *builder);
            }
          } else if (build == Protoss_Archon || build == Protoss_Dark_Archon) {
            removeUnit(st, *builder);
            removeUnit(st, *builder2);
          }
          if (FLAGS_autobuild_verbose) {
            VLOG(0) << buildLogIndent() << "advance "
                    << (build ? build->name : "null") << " -> success";
          }
          return nullptr;
        }
      }
    }

    int f = std::min(15, endFrame - st.frame);

    // This is a super rough estimate.  TODO something more accurate?
    int gasWorkers = std::min(3 * st.refineries, st.workers / 4);
    int mineralWorkers = st.workers - gasWorkers;
    const double gasPerFramePerWorker =
        std::max(st.gasPerFramePerGatherer, 0.1) * 0.85;
    const double mineralsPerFramePerWorker =
        std::max(st.mineralsPerFramePerGatherer, 0.05) * 0.85;
    double mineralIncome = mineralsPerFramePerWorker * mineralWorkers;
    double gasIncome = gasPerFramePerWorker * gasWorkers;

    st.minerals += mineralIncome * f;
    st.gas += gasIncome * f;
    st.frame += f;

    if (st.frame >= endFrame) {
      if (FLAGS_autobuild_verbose) {
        VLOG(0) << buildLogIndent() << "advance "
                << (build ? build->name : "null") << " -> timeout";
      }
      return timeout;
    }
  }
}

bool depbuild(BuildState& st, const BuildState& prevSt, BuildEntry thing) {
  auto* type = thing.type;
  auto* initialType = type;
  if (FLAGS_autobuild_verbose) {
    VLOG(0) << buildLogIndent() << "depbuild " << initialType->name;
  }
  int endFrame = st.frame + 15 * 60 * 10;
  while (true) {
    if (FLAGS_autobuild_verbose) {
      ++buildLogDepth;
    }
    auto* builtType = thing.type;
    type = advance(st, thing, endFrame);
    if (FLAGS_autobuild_verbose) {
      --buildLogDepth;
    }
    if (!type) {
      if (FLAGS_autobuild_verbose) {
        VLOG(0) << buildLogIndent() << "depbuild " << initialType->name
                << ": successfully built " << builtType->name;
      }
      return true;
    }
    if (type == builtdep) {
      if (FLAGS_autobuild_verbose) {
        VLOG(0) << buildLogIndent() << "depbuild " << initialType->name
                << ": successfully built some dependency";
      }
      return true;
    }
    if (st.frame != prevSt.frame)
      st = prevSt;
    if (type == failed) {
      if (FLAGS_autobuild_verbose)
        VLOG(0) << buildLogIndent() << "depbuild " << initialType->name
                << ": failed";
      return false;
    }
    if (type == timeout) {
      if (FLAGS_autobuild_verbose) {
        VLOG(0) << buildLogIndent() << "depbuild " << initialType->name
                << ": timed out";
      }
      return false;
    }
    if (type == builtType || (type->builder && type->builder->builder == type &&
                              !hasOrInProduction(st, type->builder))) {
      if (FLAGS_autobuild_verbose) {
        VLOG(0) << buildLogIndent() << "depbuild " << initialType->name
                << ": failing because of unsatisfiable cyclic dependency";
      }
      return false;
    }
    if (type->isWorker) {
      if (FLAGS_autobuild_verbose) {
        VLOG(0) << buildLogIndent() << "depbuild " << initialType->name
                << ": failing because of worker dependency";
      }
      return false;
    }
    thing = {type, {}};
  }
}

// st is the initial state, thingSt is the state after building thing.
template <typename F>
bool nodelayStage2(
    BuildState& st,
    BuildState thingSt,
    BuildEntry thing,
    F&& otherThing) {
  if (FLAGS_autobuild_verbose) {
    VLOG(0) << buildLogIndent() << "nodelayStage2 " << thing.type->name;
    ++buildLogDepth;
  }
  // Then try to build the other thing..
  if (otherThing(st)) {
    if (FLAGS_autobuild_verbose) {
      --buildLogDepth;
    }
    // If the other thing took too long, just do the thing
    if (st.frame >= thingSt.frame) {
      if (FLAGS_autobuild_verbose)
        VLOG(0) << buildLogIndent() << "nodelayStage2 " << thing.type->name
                << ": too late; choose thing";
      st = std::move(thingSt);
      return true;
    }
    auto otherThingSt = st;
    if (FLAGS_autobuild_verbose) {
      ++buildLogDepth;
    }
    // Then do the thing again!
    if (depbuild(st, otherThingSt, thing)) {
      if (FLAGS_autobuild_verbose) {
        --buildLogDepth;
      }
      // If we could do the other thing then the thing at least
      // as fast as just doing the thing, then do all the things!
      if (st.frame <= thingSt.frame) {
        if (FLAGS_autobuild_verbose) {
          VLOG(0) << buildLogIndent() << "nodelayStage2 " << thing.type->name
                  << ": no delay; choose otherThing";
        }
        st = std::move(otherThingSt);
        return true;
      } else {
        if (FLAGS_autobuild_verbose) {
          VLOG(0) << buildLogIndent() << "nodelayStage2 " << thing.type->name
                  << ": would delay; choose thing";
        }
        // otherwise, just do the thing
        st = std::move(thingSt);
        return true;
      }
    } else {
      if (FLAGS_autobuild_verbose) {
        --buildLogDepth;
        VLOG(0) << buildLogIndent() << "nodelayStage2 " << thing.type->name
                << ": depbuild failed";
      }
      // Should only fail if it times out, so let's just return the thing since
      // it is higher priority
      st = std::move(thingSt);
      return true;
    }
  } else {
    if (FLAGS_autobuild_verbose) {
      --buildLogDepth;
      VLOG(0) << buildLogIndent() << "nodelayStage2 " << thing.type->name
              << ": otherThing failed";
    }
    // If otherThing failed, we just do thing
    st = std::move(thingSt);
    return true;
  }
}

// This function tries to build thing, but if it can it will squeeze in
// a call to otherThing first. It evaluates whether it can do the call to
// otherThing without delaying the construction of thing.
template <typename F>
bool nodelay(BuildState& st, BuildEntry thing, F&& otherThing) {
  if (FLAGS_autobuild_verbose) {
    VLOG(0) << buildLogIndent() << "nodelay " << thing.type->name;
    ++buildLogDepth;
  }
  auto prevSt = st;
  // First try to build the thing
  if (depbuild(st, prevSt, thing)) {
    if (FLAGS_autobuild_verbose) {
      --buildLogDepth;
    }
    // Copy the state, restore the original state in preparation
    // of doing the other thing
    auto thingSt = std::move(st);
    st = std::move(prevSt);
    return nodelayStage2(
        st, std::move(thingSt), thing, std::forward<F>(otherThing));
  } else {
    if (FLAGS_autobuild_verbose) {
      --buildLogDepth;
      VLOG(0) << buildLogIndent() << "nodelay " << thing.type->name
              << ": depbuild failed";
    }
    // If it failed, then just do func
    st = std::move(prevSt);
    return std::forward<F>(otherThing)(st);
  }
}

} // namespace autobuild

void AutoBuildTask::build(
    const BuildType* type,
    Position pos,
    std::function<void()> builtCallback) {
  if (!type->isUnit() &&
      autobuild::hasOrInProduction(currentBuildState, type)) {
    return;
  }
  queue = [
    queue = std::move(queue),
    type,
    pos,
    builtCallback = std::move(builtCallback)
  ](autobuild::BuildState & st) {
    return autobuild::nodelay(st, {type, pos, builtCallback}, queue);
  };
}

void AutoBuildTask::build(
    const BuildType* type,
    std::function<void()> builtCallback) {
  if (!type->isUnit() &&
      autobuild::hasOrInProduction(currentBuildState, type)) {
    return;
  }
  queue = [
    queue = std::move(queue),
    type,
    builtCallback = std::move(builtCallback)
  ](autobuild::BuildState & st) {
    return autobuild::nodelay(st, {type, Position(), builtCallback}, queue);
  };
}

void AutoBuildTask::build(const BuildType* type, Position pos) {
  if (!type->isUnit() &&
      autobuild::hasOrInProduction(currentBuildState, type)) {
    return;
  }
  queue = [ queue = std::move(queue), type, pos ](autobuild::BuildState & st) {
    return autobuild::nodelay(st, {type, pos}, queue);
  };
}

void AutoBuildTask::build(const BuildType* type) {
  if (!type->isUnit() &&
      autobuild::hasOrInProduction(currentBuildState, type)) {
    return;
  }
  if (type == buildtypes::Zerg_Lurker &&
      autobuild::has(currentBuildState, buildtypes::Lurker_Aspect)) {
    build(buildtypes::Zerg_Hydralisk);
  }
  queue = [ queue = std::move(queue), type ](autobuild::BuildState & st) {
    return autobuild::nodelay(st, {type}, queue);
  };
}

bool AutoBuildTask::buildN(const BuildType* type, int n) {
  if (autobuild::countPlusProduction(currentBuildState, type) >= n) {
    return true;
  }
  build(type);
  return false;
}

bool AutoBuildTask::buildN(const BuildType* type, int n, int simultaneous) {
  if (simultaneous <= autobuild::countProduction(currentBuildState, type)) {
    return true;
  }
  return buildN(type, n);
}

bool AutoBuildTask::buildN(
    const BuildType* type,
    int n,
    Position positionIfWeBuildMore) {
  if (autobuild::countPlusProduction(currentBuildState, type) >= n) {
    return true;
  }
  build(type, positionIfWeBuildMore);
  return false;
}

bool AutoBuildTask::upgrade(const BuildType* type) {
  if (autobuild::has(currentBuildState, type)) {
    return true;
  }
  buildN(type, 1);
  return false;
}

void AutoBuildTask::postBlackboardKey(
    const std::string& key,
    const Blackboard::Data& data) {
  if (!isSimulation) {
    state_->board()->post(key, data);
  }
}

bool AutoBuildTask::cancelGas() {
  if (!isSimulation) {
    for (Unit* u : state_->unitsInfo().myBuildings()) {
      if (u->type == buildtypes::Zerg_Extractor && !u->completed()) {
        state_->board()->postUPC(
            utils::makeSharpUPC(u, Command::Cancel), -1, module_);
        return true;
      }
    }
  }
  return false;
}

void AutoBuildTask::update(State* state) {
  auto board = state->board();
  for (size_t i = 0; i < targetUpcIds_.size(); i++) {
    auto& target = targets_[i];
    if (target == nullptr) {
      target = board->taskForId(targetUpcIds_[i]);
      if (target) {
        VLOG(5) << "Found target task for "
                << utils::upcString(targetUpcIds_[i]);
      }
    }
  }
  auto newUnit = [&](Unit* u) {
    for (size_t i = 0; i < targetUpcIds_.size(); i++) {
      auto upcId = targetUpcIds_[i];
      auto it = scheduledUpcs.find(upcId);
      const BuildType* utype = u->type;
      if (utype == buildtypes::Zerg_Egg) {
        utype = u->constructingType;
      }
      auto& entry = std::get<0>(it->second);
      if (entry.type == utype) {
        if (entry.builtCallback) {
          entry.builtCallback();
        }
        if (targets_[i]) {
          // Cancel just in case this is a mismatch. This prevents a serious
          // bug where we build two buildings instead of one.
          // Usually this cancel won't do anything since construction has
          // started.
          targets_[i]->cancel(state);
        }
        targetUpcIds_.erase(targetUpcIds_.begin() + i);
        targets_.erase(targets_.begin() + i);
        scheduledUpcs.erase(it);
        break;
      }
    }
  };

  for (Unit* u : state->unitsInfo().getNewUnits()) {
    if (u->isMine) {
      newUnit(u);
    }
  }
  for (Unit* u : state->unitsInfo().getStartedMorphingUnits()) {
    if (u->isMine) {
      newUnit(u);
    }
  }

  // In contrast to the base class, don't determine status based on proxied
  // tasks.
  // TODO: Detect overall success/failure?

  draw(state);
}

void AutoBuildTask::evaluate(State* state, Module* module) {
  initialBuildState = autobuild::getMyState(state);
  currentBuildState = initialBuildState;

  int endFrame = initialBuildState.frame + 15 * 60 * 4;

  int firstFrameToBuildHatchery = 0;

  auto& st = currentBuildState;

  isSimulation = false;
  if (!module_) {
    LOG(FATAL) << "module_ is null";
  }

  state->board()->remove(Blackboard::kGathererMinGasWorkers);
  state->board()->remove(Blackboard::kGathererMaxGasWorkers);
  preBuild(currentBuildState);
  auto previousToLastState = st;
  while (currentBuildState.frame < endFrame) {
    if (firstFrameToBuildHatchery == 0 && currentBuildState.minerals >= 300 &&
        autobuild::countPlusProduction(
            currentBuildState, buildtypes::Zerg_Larva) == 0) {
      firstFrameToBuildHatchery = currentBuildState.frame;
    }

    queue = [](autobuild::BuildState&) { return false; };
    buildStep(currentBuildState);
    if (!queue(currentBuildState)) {
      break;
    }

    previousToLastState = currentBuildState;
  }
  postBuild(currentBuildState);

  // Build macro hatcheries if needed.
  if (initialBuildState.autoBuildHatcheries &&
      state->myRace() == +tc::BW::Race::Zerg && firstFrameToBuildHatchery &&
      previousToLastState.minerals >= 300 &&
      autobuild::countPlusProduction(
          previousToLastState, buildtypes::Zerg_Larva) < 3 &&
      autobuild::countPlusProduction(
          initialBuildState, buildtypes::Zerg_Larva) < 3) {
    auto* t = buildtypes::Zerg_Hatchery;
    bool prebuild = firstFrameToBuildHatchery > 24 * 60 * 6;
    autobuild::emplaceProd(
        currentBuildState.buildOrder,
        firstFrameToBuildHatchery - (prebuild ? t->buildTime / 2 : 0),
        autobuild::BuildEntry{t, {}});
  }

  int frame = state->currentFrame();

  // Figure out how many workers we need on gas based on how much gas we
  // actually spent in the simulation.
  int maxGasGatherers = 0;
  double spentGas = -initialBuildState.gas;
  for (auto& v : currentBuildState.buildOrder) {
    if (v.first >= frame + 15 * 60 * 2) {
      break;
    }
    spentGas += v.second.type->gasCost;
    double g = std::round(
        spentGas / (v.first - frame) /
        initialBuildState.gasPerFramePerGatherer);
    if (g > 90) {
      g = 90;
    } else if (g < 0 || g != g) {
      g = 0;
    }
    maxGasGatherers = std::max(maxGasGatherers, (int)g);
  }

  if (!FLAGS_autobuild_manual_gas ||
      !state->board()->hasKey(Blackboard::kGathererMinGasWorkers)) {
    state->board()->post(Blackboard::kGathererMinGasWorkers, 0);
  }
  if (!FLAGS_autobuild_manual_gas ||
      !state->board()->hasKey(Blackboard::kGathererMaxGasWorkers)) {
    state->board()->post(Blackboard::kGathererMaxGasWorkers, maxGasGatherers);
  }

  targetBuildState = currentBuildState;

  auto* board = state->board();
  for (auto& v : board->upcsFrom(module)) {
    for (size_t i = 0; i != targetUpcIds_.size(); ++i) {
      if (targetUpcIds_[i] == v.first) {
        targetUpcIds_.erase(targetUpcIds_.begin() + i);
        targets_.erase(targets_.begin() + i);
        board->consumeUPC(v.first, module);
        break;
      }
    }
  }

  for (size_t i = 0; i != targetUpcIds_.size();) {
    if (!targets_[i] || targets_[i]->finished()) {
      auto upcId = targetUpcIds_[i];
      targetUpcIds_.erase(targetUpcIds_.begin() + i);
      targets_.erase(targets_.begin() + i);
      auto it = scheduledUpcs.find(upcId);
      if (it != scheduledUpcs.end()) {
        scheduledUpcs.erase(it);
      }
    } else {
      ++i;
    }
  }

  std::vector<UpcId> newUpcs;

  auto sendPriority = [&](UpcId upcId, float priority) {
    UPCTuple upc;
    upc.scale = 1;
    upc.command[Command::SetCreatePriority] = 1;
    upc.state = UPCTuple::SetCreatePriorityState{upcId, priority};

    board->postUPC(
        std::make_shared<UPCTuple>(std::move(upc)), this->upcId(), module);
  };

  std::unordered_set<UpcId> upcMatched;

  float priority = 0.0f;

  for (auto& v : currentBuildState.buildOrder) {
    if (v.first >= frame + 15 * 30) {
      break;
    }
    priority += 1.0f;
    autobuild::BuildEntry thing = v.second;
    const BuildType* type = v.second.type;
    Position pos = v.second.pos;

    // Update the priority of this item if it exists
    bool found = false;
    for (UpcId id : targetUpcIds_) {
      auto it = scheduledUpcs.find(id);
      if (std::get<0>(it->second) == thing && upcMatched.insert(id).second) {
        found = true;
        if (std::get<1>(it->second) != priority) {
          std::get<1>(it->second) = priority;
          sendPriority(id, priority);
        }
        break;
      }
    }
    if (found) {
      continue;
    }

    // Otherwise, spawn a new UPC.
    UPCTuple upc;
    upc.scale = 1;
    if (pos != kInvalidPosition && pos != Position()) {
      upc.position = pos;
    }
    // else, implicit uniform position
    upc.command[Command::Create] = 1;
    upc.state = UPCTuple::BuildTypeMap{{type, 1}};

    // Post a new UPC and proxy it
    auto id = board->postUPC(
        std::make_shared<UPCTuple>(std::move(upc)), upcId(), module);
    if (id != kFilteredUpcId) {
      scheduledUpcs[id] = std::make_tuple(std::move(thing), priority);
      newUpcs.push_back(id);

      sendPriority(id, priority);
    }
  }

  // Cancel any tasks that are no longer in the build order.
  for (size_t i = 0; i != targetUpcIds_.size();) {
    auto upcId = targetUpcIds_[i];
    if (upcMatched.find(upcId) == upcMatched.end()) {
      if (targets_[i]) {
        targets_[i]->cancel(state);
      }
      targetUpcIds_.erase(targetUpcIds_.begin() + i);
      targets_.erase(targets_.begin() + i);
      auto it = scheduledUpcs.find(upcId);
      if (it != scheduledUpcs.end()) {
        scheduledUpcs.erase(it);
      }
    } else {
      ++i;
    }
  }

  for (auto id : newUpcs) {
    targetUpcIds_.push_back(id);
    targets_.push_back(nullptr);
  }

  log(state);
}

void AutoBuildTask::simEvaluateFor(
    autobuild::BuildState& argSt,
    FrameNum frames) {
  currentBuildState = std::move(argSt);
  initialBuildState = currentBuildState;

  int endFrame = initialBuildState.frame + frames;

  isSimulation = true;

  auto& st = currentBuildState;

  preBuild(st);
  autobuild::BuildState previousToLastState;
  while (st.frame < endFrame) {
    previousToLastState = st;

    queue = [](autobuild::BuildState&) { return false; };
    buildStep(currentBuildState);
    if (!queue(currentBuildState)) {
      break;
    }
  }
  postBuild(st);

  if (st.frame > endFrame) {
    st = previousToLastState;
  }

  if (st.frame < endFrame) {
    autobuild::advance(st, {}, endFrame);
  }

  targetBuildState = st;
  argSt = std::move(st);
}

std::shared_ptr<AutoBuildTask> AutoBuildModule::createTask(
    State* state,
    int srcUpcId,
    std::shared_ptr<UPCTuple> srcUpc) {
  // Only consume UPCs with string (coming from StrategyModule)
  // The string contains the build order, but we dont use it in this module
  if (!srcUpc->state.is<std::string>() &&
      !srcUpc->state.is<UPCTuple::Empty>()) {
    return nullptr;
  }
  // Return early if there is already a task created
  for (auto& task : state->board()->tasksOfModule(this)) {
    if (std::dynamic_pointer_cast<AutoBuildTask>(task)) {
      return nullptr;
    }
  }

  // TODO: How to determine build targets from UPC in the future?
  std::vector<DefaultAutoBuildTask::Target> targets;
  targets.emplace_back(buildtypes::Zerg_Hydralisk);
  targets.emplace_back(buildtypes::Zerg_Drone, 60);
  targets.emplace_back(buildtypes::Zerg_Hydralisk, 20);
  targets.emplace_back(buildtypes::Zerg_Drone, 20);

  return std::make_shared<DefaultAutoBuildTask>(
      srcUpcId, state, this, std::move(targets));
}

void AutoBuildModule::checkForNewUPCs(State* state) {
  auto board = state->board();
  for (auto& upcs : board->upcsWithSharpCommand(Command::Create)) {
    std::shared_ptr<UPCTuple> upc = upcs.second;
    if (auto task = createTask(state, upcs.first, upc)) {
      board->consumeUPC(upcs.first, this);
      board->postTask(task, this, true);
      return;
    }
  }
}

namespace {
class IncomeTrackerTask : public Task {
  std::deque<double> mineralsHistoryPerGatherer;
  std::deque<double> gasHistoryPerGatherer;
  double prevMinerals = 0.0;
  double prevGas = 0.0;
  FrameNum lastUpdate = 0;

 public:
  using Task::Task;

  double mineralsPerFramePerGatherer = 0;
  double gasPerFramePerGatherer = 0;

  virtual void update(State* state) override {
    static const int resourcePerFrameAverageSize = 15 * 40;

    int framesSinceLastUpdate = state->currentFrame() - lastUpdate;
    lastUpdate = state->currentFrame();

    int mineralGatherers = 0;
    int gasGatherers = 0;
    for (Unit* u : state->unitsInfo().myWorkers()) {
      if (!u->unit.orders.empty()) {
        auto& o = u->unit.orders.front();
        if (o.type == tc::BW::Order::MoveToMinerals ||
            o.type == tc::BW::Order::WaitForMinerals ||
            o.type == tc::BW::Order::MiningMinerals ||
            o.type == tc::BW::Order::ReturnMinerals) {
          ++mineralGatherers;
        } else if (
            o.type == tc::BW::Order::MoveToGas ||
            o.type == tc::BW::Order::Harvest1 ||
            o.type == tc::BW::Order::WaitForGas ||
            o.type == tc::BW::Order::HarvestGas ||
            o.type == tc::BW::Order::ReturnGas) {
          ++gasGatherers;
        }
      }
    }

    auto updateResource = [&](auto& container, double value) {
      for (int i = 0; i != framesSinceLastUpdate; ++i) {
        if (container.size() >= resourcePerFrameAverageSize) {
          container.pop_front();
        }
        container.push_back(value);
        value = 0;
      }
      double sum = 0.0;
      for (double val : container) {
        sum += val;
      }
      return sum / container.size();
    };

    if (mineralGatherers) {
      double minerals = state->resources().ore;
      mineralsPerFramePerGatherer = updateResource(
          mineralsHistoryPerGatherer,
          std::max(minerals - prevMinerals, 0.0) / mineralGatherers);
      prevMinerals = minerals;
    }
    if (gasGatherers) {
      double gas = state->resources().gas;
      gasPerFramePerGatherer = updateResource(
          gasHistoryPerGatherer, std::max(gas - prevGas, 0.0) / gasGatherers);
      prevGas = gas;
    }

    state->board()->post(
        Blackboard::kMineralsPerFramePerGatherer, mineralsPerFramePerGatherer);
    state->board()->post(
        Blackboard::kGasPerFramePerGatherer, gasPerFramePerGatherer);
  }
};
}

void AutoBuildModule::step(State* state) {
  checkForNewUPCs(state);

  int frame = state->currentFrame();
  auto tasks = state->board()->tasksOfModule(this);

  std::shared_ptr<IncomeTrackerTask> incomeTracker;
  for (auto& task : tasks) {
    incomeTracker = std::dynamic_pointer_cast<IncomeTrackerTask>(task);
    if (incomeTracker) {
      break;
    }
  }

  if (!incomeTracker) {
    auto board = state->board();
    auto upcId = board->postUPC(std::make_shared<UPCTuple>(), kRootUpcId, this);
    incomeTracker = std::make_shared<IncomeTrackerTask>(upcId);
    board->postTask(incomeTracker, this, true);
  }

  // Update tasks
  for (auto& task : tasks) {
    auto abtask = std::dynamic_pointer_cast<AutoBuildTask>(task);
    if (abtask && abtask->status() == TaskStatus::Ongoing) {
      if (abtask->lastEvaluate == 0 || frame - abtask->lastEvaluate >= 15) {
        // Time to replan!
        abtask->evaluate(state, this);
        abtask->lastEvaluate = frame;
      }
    }
  }
}

/////////////
// Logging //
/////////////

typedef std::pair<const BuildType*, std::vector<autobuild::BuildStateUnit>>
    BuildUnitPair;
static struct {
  bool operator()(const BuildUnitPair& a, const BuildUnitPair& b) const {
    if (a.second.size() == b.second.size()) {
      return a.first->name < b.first->name;
    }
    return a.second.size() > b.second.size();
  }
} compareUnitsForDescription;

typedef std::pair<int, const BuildType*> ProductionPair;
static struct {
  bool operator()(const ProductionPair& a, const ProductionPair& b) const {
    return a.first < b.first;
  }
} compareProductionForDescription;

// Don't bother logging these because you start the game with these
static std::vector<std::string> freeTechs{
    "Scanner_Sweep",
    "Defensive_Matrix",
    "Infestation",
    "Dark_Swarm",
    "Parasite",
    "Archon_Warp",
    "Dark_Archon_Meld",
    "Feedback",
    "Healing",
};

std::string AutoBuildTask::frameToString(State* state) {
  auto frame = initialBuildState.frame;
  auto seconds = frame / 24;
  return fmt::format("Time: {0}:{1:0>2d}", seconds / 60, seconds % 60);
}

std::string mineralsToString(State* state) {
  return fmt::format("Minerals: {}", state->resources().ore);
}

std::string gasToString(State* state) {
  return fmt::format("Gas: {}", state->resources().gas);
}

std::string supplyToString(State* state) {
  const auto& resources = state->resources();
  return fmt::format(
      "Supply: {}/{}", (1 + resources.used_psi) / 2, resources.total_psi / 2);
}

std::string larvaToString(State* state) {
  return fmt::format(
      "Larva: {}",
      state->unitsInfo().myUnitsOfType(buildtypes::Zerg_Larva).size());
}

std::vector<std::string> AutoBuildTask::upgradesToString(State* state) {
  std::vector<const BuildType*> upgrades{
      initialBuildState.upgradesAndTech.begin(),
      initialBuildState.upgradesAndTech.end()};
  std::sort(upgrades.begin(), upgrades.end());
  std::vector<std::string> output;
  for (auto& upgrade : upgrades) {
    auto name = upgrade->name;
    if (freeTechs.end() ==
        std::find(freeTechs.begin(), freeTechs.end(), name)) {
      output.push_back(name);
    }
  }
  return output;
}

std::vector<std::vector<std::string>> AutoBuildTask::unitsToString(
    State* state) {
  std::vector<BuildUnitPair> units{initialBuildState.units.begin(),
                                   initialBuildState.units.end()};
  std::sort(units.begin(), units.end(), compareUnitsForDescription);

  std::vector<std::vector<std::string>> output;
  for (auto& typeAndUnits : units) {
    output.push_back(
        std::vector<std::string>{std::to_string(typeAndUnits.second.size()),
                                 typeAndUnits.first->name});
  }
  return output;
}

std::vector<std::vector<std::string>> AutoBuildTask::productionToString(
    State* state) {
  std::vector<ProductionPair> production{initialBuildState.production.begin(),
                                         initialBuildState.production.end()};
  std::sort(
      production.begin(), production.end(), compareProductionForDescription);

  std::vector<std::vector<std::string>> output;
  for (auto& pair : production) {
    output.push_back(
        {std::to_string(std::max(0, (pair.first - state->currentFrame()) / 24)),
         pair.second->name});
  }
  return output;
}

std::vector<std::vector<std::string>> AutoBuildTask::queueToString(
    State* state) {
  std::vector<std::vector<std::string>> output;
  for (auto& entry : currentBuildState.buildOrder) {
    output.push_back(
        {std::to_string(std::max(0, entry.first - state->currentFrame()) / 24),
         entry.second.type->name});
  }
  return output;
}

void logColumns(
    const std::vector<std::string>& strings,
    int logLevel,
    int width) {
  std::stringstream concatenated;
  concatenated << strings[0] << std::string(width - strings[0].size(), ' ')
               << strings[1];
  VLOG(logLevel) << concatenated.str();
}

void AutoBuildTask::log(State* state) {
  ++logInvocations;
  if (logInvocations % FLAGS_autobuild_log_period > 0) {
    return;
  }
  VLOG(1);
  VLOG(1) << frameToString(state);
  VLOG(1) << mineralsToString(state);
  VLOG(1) << gasToString(state);
  VLOG(1) << supplyToString(state);
  VLOG(1) << larvaToString(state);
  auto completeUpgrades = upgradesToString(state);
  if (completeUpgrades.size() > 0) {
    VLOG(2);
    VLOG(2) << "Upgrades:";
  }
  for (auto& upgrade : completeUpgrades) {
    VLOG(2) << upgrade;
  }
  VLOG(2);
  VLOG(2) << "Units:";
  auto units = unitsToString(state);
  for (auto& unit : units) {
    logColumns(unit, 2, 3);
  }

  auto production = productionToString(state);
  if (production.size() > 0) {
    VLOG(2);
    VLOG(2) << "In production (Seconds left)";
  }
  for (auto& item : production) {
    logColumns(item, 2, 3);
  }

  VLOG(3);
  VLOG(3) << "Queue (Seconds in future):";
  constexpr int maxQueueLength = 10;
  auto items = queueToString(state);
  auto queueCount = 0;
  for (auto& item : items) {
    if (queueCount < maxQueueLength) {
      logColumns(item, 3, 4);
    }
    ++queueCount;
  }
  int unlistedItems = items.size() - maxQueueLength;
  if (unlistedItems > 0) {
    VLOG(3) << fmt::format("...plus {0} more items", unlistedItems);
  }
}

void AutoBuildTask::draw(State* state) {
  if (!FLAGS_autobuild_draw) {
    return;
  }

  utils::drawTextScreen(state, 0, 0, mineralsToString(state));
  utils::drawTextScreen(state, 16, 0, gasToString(state));
  utils::drawTextScreen(state, 32, 0, supplyToString(state));
  utils::drawTextScreen(state, 48, 0, frameToString(state));
  utils::drawTextScreen(state, 64, 0, larvaToString(state));
  auto y = 2;

  auto upgrades = upgradesToString(state);
  for (auto& upgrade : upgrades) {
    utils::drawTextScreen(state, 0, y++, upgrade);
  }
  ++y;

  auto units = unitsToString(state);
  for (auto& unit : units) {
    utils::drawTextScreen(state, 0, y, unit[0]);
    utils::drawTextScreen(state, 5, y, unit[1]);
    ++y;
  }
  ++y;

  utils::drawTextScreen(state, 0, y++, "Production:");
  auto production = productionToString(state);
  for (auto& item : production) {
    utils::drawTextScreen(state, 0, y, item[0]);
    utils::drawTextScreen(state, 5, y, item[1]);
    ++y;
  }
  ++y;

  utils::drawTextScreen(state, 0, y++, "Queue:");
  auto queue = queueToString(state);
  for (auto& entry : queue) {
    utils::drawTextScreen(state, 0, y, entry[0]);
    utils::drawTextScreen(state, 8, y, entry[1]);
    ++y;
  }
}

// Cerealization
#ifdef HAVE_TORCH
namespace autobuild {

using BuildTypeId = uint32_t;

inline BuildTypeId buildTypeId(cherrypi::BuildType const* type) {
  // One byte each for unit, upgrade, tech, level
  // Assuming nothing gets past 254 -- -1 will map to 255
  BuildTypeId id = 0;
  if (type != nullptr) {
    id |= uint8_t(type->unit) << 0;
    id |= uint8_t(type->upgrade) << 8;
    id |= uint8_t(type->tech) << 16;
    id |= uint8_t(type->level) << 24;
    // Level should be small, so use highest bit to mark that this is not a
    // nullptr
    id |= 1 << 31;
  }
  return id;
}

inline cherrypi::BuildType const* buildTypeFromId(BuildTypeId id) {
  int unit, upgrade, tech, level;
  if (id >> 31 == 0) {
    return nullptr;
  }
  unit = (id >> 0) & 0xFF;
  upgrade = (id >> 8) & 0xFF;
  tech = (id >> 16) & 0xFF;
  level = (id >> 24) & 0x7F; // ignore highest bit (nullptr mark)
  if (unit != 255) {
    return cherrypi::getUnitBuildType(unit);
  } else if (tech != 255) {
    return cherrypi::getTechBuildType(tech);
  }
  return cherrypi::getUpgradeBuildType(upgrade, level);
}

template <class Archive>
void save(Archive& ar, BuildStateUnit const& stu) {
  ar(buildTypeId(stu.type),
     stu.busyUntil,
     buildTypeId(stu.addon),
     stu.larvaTimer);
}

template <class Archive>
void load(Archive& ar, BuildStateUnit& stu) {
  BuildTypeId type, addon;
  ar(type, stu.busyUntil, addon, stu.larvaTimer);
  stu.type = buildTypeFromId(type);
  stu.addon = buildTypeFromId(addon);
}

template <class Archive>
void save(Archive& ar, BuildEntry const& e) {
  ar(e.type->unit, e.pos); // can't serialize callback...
}

template <class Archive>
void load(Archive& ar, BuildEntry& e) {
  BuildTypeId type;
  ar(type, e.pos);
  e.type = buildTypeFromId(type);
}

template <class Archive>
void save(Archive& ar, BuildState const& st) {
  ar(st.frame, st.race, st.minerals, st.gas);
  ar(st.mineralsPerFramePerGatherer, st.gasPerFramePerGatherer);
  for (auto i = 0U; i < st.usedSupply.size(); i++) {
    ar(st.usedSupply[i], st.maxSupply[i], st.inprodSupply[i]);
  }

  // We can't serialize BuildType pointers out of the box;
  // pack them into an integer instead
  std::unordered_map<BuildTypeId, std::vector<BuildStateUnit>> units;
  for (auto const& it : st.units) {
    units[buildTypeId(it.first)] = it.second;
  }
  std::unordered_set<BuildTypeId> upgradesAndTech;
  for (auto const& ut : st.upgradesAndTech) {
    upgradesAndTech.insert(buildTypeId(ut));
  }
  std::deque<std::pair<int, BuildTypeId>> production;
  for (auto const& it : st.production) {
    production.emplace_back(it.first, buildTypeId(it.second));
  }
  ar(units, upgradesAndTech, production);

  ar(st.morphingHatcheries);
  ar(st.workers, st.refineries, st.availableGases);
  ar(st.autoBuildRefineries, st.autoBuildHatcheries, st.isExpanding);
}

template <class Archive>
void load(Archive& ar, BuildState& st) {
  using namespace cherrypi::autobuild;
  ar(st.frame, st.race, st.minerals, st.gas);
  ar(st.mineralsPerFramePerGatherer, st.gasPerFramePerGatherer);
  for (auto i = 0U; i < st.usedSupply.size(); i++) {
    ar(st.usedSupply[i], st.maxSupply[i], st.inprodSupply[i]);
  }

  // We can't serialize BuildType pointers out of the box;
  // pack them into an integer instead
  std::unordered_map<BuildTypeId, std::vector<BuildStateUnit>> units;
  std::unordered_set<BuildTypeId> upgradesAndTech;
  std::deque<std::pair<int, BuildTypeId>> production;
  ar(units, upgradesAndTech, production);
  for (auto const& it : units) {
    st.units[buildTypeFromId(it.first)] = std::move(it.second);
  }
  for (auto const& ut : upgradesAndTech) {
    st.upgradesAndTech.insert(buildTypeFromId(ut));
  }
  for (auto const& it : production) {
    st.production.emplace_back(it.first, buildTypeFromId(it.second));
  }

  ar(st.morphingHatcheries);
  ar(st.workers, st.refineries, st.availableGases);
  ar(st.autoBuildRefineries, st.autoBuildHatcheries, st.isExpanding);
}

template void save<cereal::BinaryOutputArchive>(
    cereal::BinaryOutputArchive& archive,
    BuildStateUnit const& stu);
template void load<cereal::BinaryInputArchive>(
    cereal::BinaryInputArchive& archive,
    BuildStateUnit& stu);
template void save<cereal::BinaryOutputArchive>(
    cereal::BinaryOutputArchive& archive,
    BuildEntry const& e);
template void load<cereal::BinaryInputArchive>(
    cereal::BinaryInputArchive& archive,
    BuildEntry& e);
template void save<cereal::BinaryOutputArchive>(
    cereal::BinaryOutputArchive& archive,
    BuildState const& stu);
template void load<cereal::BinaryInputArchive>(
    cereal::BinaryInputArchive& archive,
    BuildState& stu);
template void save<cereal::JSONOutputArchive>(
    cereal::JSONOutputArchive& archive,
    BuildStateUnit const& stu);
template void load<cereal::JSONInputArchive>(
    cereal::JSONInputArchive& archive,
    BuildStateUnit& stu);
template void save<cereal::JSONOutputArchive>(
    cereal::JSONOutputArchive& archive,
    BuildEntry const& e);
template void load<cereal::JSONInputArchive>(
    cereal::JSONInputArchive& archive,
    BuildEntry& e);
template void save<cereal::JSONOutputArchive>(
    cereal::JSONOutputArchive& archive,
    BuildState const& stu);
template void load<cereal::JSONInputArchive>(
    cereal::JSONInputArchive& archive,
    BuildState& stu);

} // namespace autobuild
#endif // HAVE_TORCH
} // namespace cherrypi
