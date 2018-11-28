/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "strategy.h"

#include "blackboard.h"
#include "buildtype.h"
#include "common/rand.h"
#include "models/bandit.h"
#include "player.h"
#include "state.h"
#include "utils.h"

#ifdef HAVE_TORCH
#include "models/bos/models.h"
#endif // HAVE_TORCH

#include <fmt/format.h>
#include <gflags/gflags.h>
#include <prettyprint/prettyprint.hpp>

#include <algorithm>

DEFINE_string(build, "", "What build orders are allowed");
DEFINE_string(
    bandit,
    kBanditExpMooRolling,
    "Which bandit algorithm to use: "
    "none|ucb1|ucb1exploit|thompson|thompsonrolling|expmoorolling");
DEFINE_double(
    ucb1_c,
    0.3, // 2.0 is a classic default
    "Value of the exploration parameter in UCB1");
DEFINE_double(
    bandit_gamma,
    0.75,
    "Value of the discounting parameter (rolling avg) in UCB1Rolling");
DEFINE_double(
    thompson_a,
    0.1, // 1.0 is a classic default
    "Value of the initial alpha in Thompson sampling (Beta(alpha,beta))");
DEFINE_double(
    thompson_b,
    0.1, // 1.0 is a classic default
    "Value of the initial beta in Thompson sampling (Beta(alpha,beta))");
DEFINE_double(
    moo_mult,
    6.0,
    "Value of the multiplier inside the exponential in ExpMooRolling");
DEFINE_string(
    strategy,
    "tournament",
    "Which bandit configuration to use (tournament|training)");
DEFINE_bool(
    game_history,
    true,
    "Read/write game history files from/to bwapi-data/{read,write}");

// Build order switching
DEFINE_int32(
    bos_interval,
    5 * 24,
    "Interval for BOS model inference in frames");
DEFINE_string(bos_model, "", "Path to build order switch model");
DEFINE_string(
    bos_start,
    "6",
    "Game time at which on BOS decisions will be used in any case, in minutes");
DEFINE_double(
    bos_min_advantage,
    0.15,
    "Threshold for switching to a more advantegeous build");

namespace cherrypi {

REGISTER_SUBCLASS_0(Module, StrategyModule);

namespace {

constexpr int kScoutingMaxNbOverlords = 2;
constexpr int kScoutingMaxNbWorkers = 1;
constexpr int kScoutingMaxNbExplorers = 0;

// The strategy module posts UPCs for various activities. We keep track of them
// via ProxyTasks that also contain the command that the UPC was about.
class StrategyProxyTask : public ProxyTask {
 public:
  StrategyProxyTask(UpcId targetUpcId, UpcId upcId, Command command)
      : ProxyTask(targetUpcId, upcId), command(command) {}

  Command command;
};

} // namespace

StrategyModule::StrategyModule(Duty duties) : duties_(duties) {}

void StrategyModule::step(State* state) {
  if (duties_ & Duty::BuildOrder) {
    stepBuildOrder(state);
  }
  if (duties_ & Duty::Scouting) {
    stepScouting(state);
  }
  if (duties_ & Duty::Harassment) {
    stepHarassment(state);
  }
}

void StrategyModule::stepBuildOrder(State* state) {
  auto board = state->board();

// Build order switching support
#ifdef HAVE_TORCH
  auto nextBO = stepBos(state);
#else // HAVE_TORCH
  auto nextBO = currentBuildOrder_;
#endif // HAVE_TORCH
  auto boardBO = board->get<std::string>(Blackboard::kBuildOrderKey);
  if (boardBO != currentBuildOrder_) {
    nextBO = boardBO;
  }
  auto currentTask = getProxyTaskWithCommand(state, Command::Create);
  if (currentTask && nextBO == currentBuildOrder_) {
    return;
  }
  if (currentTask) {
    currentTask->cancel(state);
  }

  // Look for create UPCs with empty state
  for (auto& upcs : state->board()->upcsWithSharpCommand(Command::Create)) {
    if (!upcs.second->state.is<UPCTuple::Empty>()) {
      continue;
    }

    spawnBuildOrderTask(state, upcs.first, nextBO);
    break;
  }
}

void StrategyModule::spawnBuildOrderTask(
    State* state,
    UpcId originUpcId,
    std::string const& buildorder) {
  auto board = state->board();

  board->consumeUPC(originUpcId, this);
  auto upc = std::make_shared<UPCTuple>();
  upc->command[Command::Create] = 1;
  upc->state = buildorder;
  auto id = board->postUPC(std::move(upc), originUpcId, this);
  if (id >= 0) {
    // Create a Create UPC so that we can track execution of the build order
    // (and are able to cancel it if needed).
    board->postTask(
        std::make_shared<StrategyProxyTask>(id, originUpcId, Command::Create),
        this,
        true);
    currentBuildOrder_ = buildorder;
    VLOG(1) << "Posted build order UPC: " << utils::upcString(upc, id);
  }
}

void StrategyModule::stepScouting(State* state) {
  auto board = state->board();

  int minScoutFrame = board->get<int>(Blackboard::kMinScoutFrameKey, 1560);
  if (minScoutFrame <= 0) {
    minScoutFrame = std::numeric_limits<int>::max();
  }

  // We rely on the scouting module to properly select locations that should be
  // scouted so we're not setting a position for the UPC
  auto postUpc = [&](BuildType const* utype) {
    auto upc = std::make_shared<UPCTuple>();
    for (auto unit : state->unitsInfo().myUnitsOfType(utype)) {
      upc->unit[unit] = .5;
    }
    if (upc->unit.empty()) {
      return kInvalidUpcId;
    }
    upc->command[Command::Scout] = 1;
    auto upcId = board->postUPC(std::move(upc), kRootUpcId, this);
    if (upcId < 0) {
      LOG(WARNING) << "Main scouting UPC not sent to the blackboard";
      return kInvalidUpcId;
    } else {
      VLOG(1) << "Posted scouting UPC: "
              << utils::upcString(board->upcWithId(upcId), upcId) << " for "
              << utype->name;
    }
    return upcId;
  };

  // Send worker to see the enemy base. This is the case even if we know where
  // the enemy base is; we still want to send a scout to see it even if it was
  // found by elimination.
  // Posts at most one UPC per unit type at each frame
  auto maxNbExplorers = board->get<int>(
      Blackboard::kMaxScoutExplorersKey, kScoutingMaxNbExplorers);
  auto maxNbWorkers =
      board->get<int>(Blackboard::kMaxScoutWorkersKey, kScoutingMaxNbWorkers);
  auto& ainfo = state->areaInfo();
  auto& uinfo = state->unitsInfo();
  auto workerType = buildtypes::getRaceWorker(state->myRace());

  while (ainfo.foundEnemyStartLocation() &&
         state->currentFrame() >= minScoutFrame &&
         nbScoutingExplorers_<maxNbExplorers&& int(
             uinfo.myCompletedUnitsOfType(workerType).size())>
             nbScoutingExplorers_) {
    auto upcId = postUpc(workerType);
    if (upcId > 0) {
      nbScoutingExplorers_++;
      VLOG(3) << "Creating the " << nbScoutingExplorers_
              << "th scouting UPC for explorer workers: "
              << utils::upcString(upcId);
    }
  }

  if (state->currentFrame() >= 0) {
    while (nbScoutingOverlords_<kScoutingMaxNbOverlords&& int(
               uinfo.myCompletedUnitsOfType(buildtypes::Zerg_Overlord).size())>
               nbScoutingOverlords_) {
      auto upcId = postUpc(buildtypes::Zerg_Overlord);
      if (upcId > 0) {
        nbScoutingOverlords_++;
        VLOG(3) << "Creating the " << nbScoutingOverlords_
                << "th scouting UPC for overlords: " << utils::upcString(upcId);
      }
    }
  }

  if (state->currentFrame() >= minScoutFrame &&
      !ainfo.foundEnemyStartLocation()) {
    while (nbScoutingWorkers_<maxNbWorkers&& int(
               uinfo.myCompletedUnitsOfType(workerType).size())>
               nbScoutingWorkers_) {
      auto upcId = postUpc(workerType);
      if (upcId > 0) {
        nbScoutingWorkers_++;
        VLOG(3) << "Creating the " << nbScoutingWorkers_
                << "th scouting UPC for workers: " << utils::upcString(upcId);
      }
    }
  }
}

void StrategyModule::stepHarassment(State* state) {
  auto board = state->board();
  // helper to go faster when checking is not necessary
  if (!state->areaInfo().foundEnemyStartLocation()) {
    return;
  }

  // In the current tests, we shall have only one harassment task because there
  // is a single enemy location
  if (getProxyTaskWithCommand(state, Command::Harass)) {
    return;
  }

  // Should go through all enemy locations. For now only goes through the one in
  // the blackboard helper to go faster when checking is not necessary
  auto nmyLoc = state->areaInfo().enemyStartLocation();
  if (nmyLoc.x < 0 || nmyLoc.y < 0) {
    LOG(ERROR) << "invalid enemy location";
    return;
  }

  // Should be replaced with enemy units in area info
  bool found = false;
  Unit* worker = nullptr;
  for (auto unit : state->unitsInfo().myUnits()) {
    if (!(unit->type->isWorker ||
          (unit->type->isRefinery && !unit->completed())) ||
        unit->dead || nmyLoc.distanceTo(unit) > 100) {
      continue;
    }
    for (auto nmyu : state->unitsInfo().enemyUnits()) {
      if (!nmyu->type->isBuilding) {
        continue;
      }
      auto distance = utils::distance(unit, nmyu);
      if (distance < 1.5f * unit->sightRange) {
        worker = unit;
        found = true;
        break;
      }
    }
    // Assumes a single harasser per location
    if (found) {
      break;
    }
  }

  std::shared_ptr<UPCTuple> baseUpc = nullptr;
  if (worker) {
    baseUpc = utils::makeSharpUPC(worker, nmyLoc, Command::Harass);
  } else {
    return;
  }

  auto upcId = board->postUPC(std::move(baseUpc), kRootUpcId, this);
  if (upcId < 0) {
    LOG(ERROR) << "Base upc for harassment could not be posted";
  } else {
    board->postTask(
        std::make_shared<StrategyProxyTask>(upcId, kRootUpcId, Command::Harass),
        this,
        true);
    VLOG(1) << "Posted harassment UPC: "
            << utils::upcString(board->upcWithId(upcId), upcId);
  }
}

void StrategyModule::onGameStart(State* state) {
  if (!(duties_ & Duty::BuildOrder)) {
    return;
  }

  // We add the new game is recorded as a loss in the build order history, and
  // save it right away, meaning that we count this opening as a loss if we
  // crash between now and onGameEnd. That helps when one build is crashing in a
  // particular match-up (but hides bugs in statistics).
  auto board = state->board();
  auto openingBuildOrder = getOpeningBuildOrder(state);
  if (FLAGS_game_history) {
    auto bwapiRoot = board->get<std::string>(Blackboard::kBanditRootKey, ".");
    auto readFolder = fmt::format("{}/bwapi-data/read", bwapiRoot);
    auto writeFolder = fmt::format("{}/bwapi-data/write", bwapiRoot);
    model::EnemyHistory history(
        board->get<std::string>(Blackboard::kEnemyNameKey),
        readFolder,
        writeFolder);
    history.addStartingGame(openingBuildOrder);
  }
  currentBuildOrder_ = openingBuildOrder;

  // Record the opening build order on the blackboard, since the build order at
  // the end may not be the opening build order anymore
  board->post(
      Blackboard::kOpeningBuildOrderKey, std::string(openingBuildOrder));
  board->post(Blackboard::kBuildOrderKey, std::move(openingBuildOrder));

#ifdef HAVE_TORCH
  bosRunner_ = makeBosRunner(state);
  nextBosForwardFrame_ = 0;
  bosStartTime_ = std::stof(FLAGS_bos_start) * 60;
  bosMapVerified_ = false;
#endif // HAVE_TORCH
}

std::string StrategyModule::getOpeningBuildOrder(State* state) {
  auto board = state->board();
  if (board->hasKey(Blackboard::kOpeningBuildOrderKey)) {
    // Some sanity checks - since we are reading build order from kBuildOrderKey
    if (board->hasKey(Blackboard::kBuildOrderKey)) {
      if (board->get<std::string>(Blackboard::kOpeningBuildOrderKey) !=
          board->get<std::string>(Blackboard::kBuildOrderKey)) {
        LOG(ERROR) << "kOpeningBuildOrderKey ("
                   << board->get<std::string>(Blackboard::kOpeningBuildOrderKey)
                   << ") != kBuildOrderKey ("
                   << board->get<std::string>(Blackboard::kBuildOrderKey)
                   << ") at game opening! Using value of kOpeningBuildOrderKey";
      }
    }
    return board->get<std::string>(Blackboard::kOpeningBuildOrderKey);
  }
  if (board->hasKey(Blackboard::kBuildOrderKey)) {
    return board->get<std::string>(Blackboard::kBuildOrderKey);
  }
  tc::BW::Race enemyRace = tc::BW::Race::Unknown;
  auto enemyRaceBB = tc::BW::Race::_from_integral_nothrow(
      board->get<int>(Blackboard::kEnemyRaceKey));
  if (enemyRaceBB) {
    enemyRace = *enemyRaceBB;
  }
  auto mapName = state->tcstate()->map_name;
  auto enemyName = board->get<std::string>(Blackboard::kEnemyNameKey);

  return selectBO(state, state->myRace(), enemyRace, mapName, enemyName);
}

void StrategyModule::onGameEnd(State* state) {
  // if opening build order was provided, then update the history
  auto* board = state->board();
  if (FLAGS_game_history && board->hasKey(Blackboard::kOpeningBuildOrderKey)) {
    auto bwapiRoot = board->get<std::string>(Blackboard::kBanditRootKey, ".");
    auto writeFolder = fmt::format("{}/bwapi-data/write", bwapiRoot);
    model::EnemyHistory history(
        board->get<std::string>(Blackboard::kEnemyNameKey),
        writeFolder, // update file created in write directory
        writeFolder);
    if (state->won()) {
      // if the game was won, update the history since we recorded the game as a
      // loss at
      // its beginning
      std::string openingBuildOrder =
          board->get<std::string>(Blackboard::kOpeningBuildOrderKey);
      history.updateLastGameToVictory(openingBuildOrder);
    }
    history.printStatus();
  }
}

std::string StrategyModule::selectBO(
    State* state,
    tc::BW::Race ourRace,
    tc::BW::Race enemyRace,
    const std::string& mapName,
    const std::string& enemyName) {
  VLOG(2) << fmt::format(
      "Selecting build for {} vs. the {} opponent {} on {}",
      ourRace,
      enemyRace,
      enemyName,
      mapName);

  auto allBuildOrders = [&]() {
    if (FLAGS_strategy == "tournament") {
      return model::buildOrdersForTournament(enemyName);
    } else if (FLAGS_strategy == "training") {
      return model::buildOrdersForTraining();
    }
    throw std::runtime_error("Unknown strategy: " + FLAGS_strategy);
  }();

  // Get all acceptable build orders
  std::vector<std::string> acceptable = [&]() {
    if (FLAGS_build.empty()) {
      return model::acceptableBuildOrders(allBuildOrders, ourRace, enemyRace);
    }

    std::vector<std::string> output;
    for (std::string buildName : utils::stringSplit(FLAGS_build, '_')) {
      auto buildOrder = allBuildOrders.find(buildName);
      if (buildOrder == allBuildOrders.end()) {
        VLOG(0) << "-build specified an undefined build order: " << buildName;
        continue;
      }
      output.push_back(buildName);
    }
    return output;
  }();

  // Create a map from acceptable build order to history counts and add
  // the current configuration. This will serve as input to the score
  // function which will choose the build order
  std::map<std::string, model::BuildOrderCount> acceptableCounts;
  if (FLAGS_game_history) {
    auto bwapiRoot =
        state->board()->get<std::string>(Blackboard::kBanditRootKey, ".");
    auto readFolder = fmt::format("{}/bwapi-data/read", bwapiRoot);
    auto writeFolder = fmt::format("{}/bwapi-data/write", bwapiRoot);
    model::EnemyHistory history(enemyName, readFolder, writeFolder);
    for (auto buildOrder : acceptable) {
      // use history if exists
      auto count_iter = history.buildOrderCounts.find(buildOrder);
      if (count_iter != history.buildOrderCounts.end()) {
        acceptableCounts[buildOrder] = count_iter->second;
      }
      // update the buildOrderCount with the current configuration
      acceptableCounts[buildOrder].config = allBuildOrders[buildOrder];
    }
  } else {
    for (auto buildOrder : acceptable) {
      acceptableCounts[buildOrder].config = allBuildOrders[buildOrder];
    }
  }
  // Possible improvement: filter here on config.priority > 0, instead of in
  // score functions

  std::string selectedName = model::score::chooseBuildOrder(
      acceptableCounts,
      FLAGS_bandit,
      FLAGS_ucb1_c,
      FLAGS_bandit_gamma,
      FLAGS_thompson_a,
      FLAGS_thompson_b,
      FLAGS_moo_mult);

  for (auto& buildOrder : allBuildOrders) {
    if (buildOrder.first == selectedName) {
      VLOG(5) << "Found build " << buildOrder.first;
      if (!buildOrder.second.switchEnabled()) {
        VLOG(0) << "This build order disables BOS";
        state->board()->post(Blackboard::kBuildOrderSwitchEnabledKey, false);
      }
    }
  }

  return selectedName;
}

#ifdef HAVE_TORCH
std::unique_ptr<bos::ModelRunner> StrategyModule::makeBosRunner(State* state) {
  if (!state->board()->get<bool>(
          Blackboard::kBuildOrderSwitchEnabledKey, true)) {
    return nullptr;
  }

  // For now, disbale BOS on random race opponents as we haven't seen them
  // during training. A workaround would be to buffer all samples and then do
  // the remaining forwards
  auto race = state->board()->get<int>(Blackboard::kEnemyRaceKey);
  switch (race) {
    case +tc::BW::Race::Zerg:
    case +tc::BW::Race::Terran:
    case +tc::BW::Race::Protoss:
      break;
    default:
      VLOG(0) << "Disabling BOS against opponent playing "
              << tc::BW::Race::_from_integral(race)._to_string();
      return nullptr;
  }

  if (FLAGS_bos_model.empty()) {
    return nullptr;
  }

  ag::Container model;
  try {
    model = bos::modelMakeFromCli();
  } catch (std::exception const& ex) {
    LOG(WARNING) << "Error constructing BOS model: " << ex.what();
    return nullptr;
  }

  if (model) {
    try {
      ag::load(FLAGS_bos_model, model);
      VLOG(0) << "Loaded BOS model from " << FLAGS_bos_model;
      if (common::gpuAvailable()) {
        model->to(torch::kCUDA);
      }
      model->eval();
    } catch (std::exception const& ex) {
      LOG(WARNING) << "Error loading BOS model from " << FLAGS_bos_model << ": "
                   << ex.what();
      return nullptr;
    }
  }

  if (model) {
    std::unique_ptr<bos::ModelRunner> runner;
    try {
      runner = bos::makeModelRunner(model, FLAGS_bos_model_type);
    } catch (std::exception const& ex) {
      LOG(WARNING) << "Error constructing BOS model runner: " << ex.what();
    }

    auto enemyName =
        state->board()->get<std::string>(Blackboard::kEnemyNameKey);
    if (utils::stringToLower(enemyName).find("saida") == std::string::npos) {
      runner->blacklistBuildOrder(bos::addRacePrefix("zvtantimech", 'T'));
      runner->blacklistBuildOrder(bos::addRacePrefix("zvtantimech", 'P'));
    }
    return runner;
  }
  return nullptr;
}

std::string StrategyModule::stepBos(State* state) {
  if (bosRunner_ == nullptr) {
    return currentBuildOrder_;
  }

  ag::Variant output;
  if (state->currentFrame() >= nextBosForwardFrame_) {
    auto sample = bosRunner_->takeSample(state);
    // Lazy check for currently supported maps
    if (!bosMapVerified_) {
      auto mapId = sample.featurize(bos::BosFeature::MapId).item<int64_t>();
      if (mapId < 1) {
        VLOG(0) << "Disabling BOS on unknown map " << state->mapName();
        bosRunner_ = nullptr;
        return currentBuildOrder_;
      }
      bosMapVerified_ = true;
    }
    output = bosRunner_->forward(sample);
    if (VLOG_IS_ON(1)) {
      auto heads = output["vHeads"].squeeze().to(at::kCPU);
      auto probs = std::map<std::string, float>();
      for (auto const& it : bos::buildOrderMap()) {
        auto p = heads[it.second].item<float>();
        if (p > 0.0f) {
          probs[it.first] = p;
        }
      }
      VLOG(1) << probs;
    }
    nextBosForwardFrame_ = state->currentFrame() + FLAGS_bos_interval;
  } else {
    return currentBuildOrder_;
  }

  if (!shouldListenToBos(state)) {
    return currentBuildOrder_;
  }

  auto build = output.getDict()["build"].getString();
  auto prefixedBuild = bos::addRacePrefix(
      build, state->board()->get<int>(Blackboard::kEnemyRaceKey));
  auto pbuild = output["pwin"].item<float>();
  auto adv = output["advantage"].item<float>();
  if (adv <= 0) {
    return currentBuildOrder_;
  }
  if (adv < FLAGS_bos_min_advantage) {
    VLOG(1) << fmt::format(
        "Advantage of {} {} too small, current value {}",
        prefixedBuild,
        adv,
        pbuild - adv);
    return currentBuildOrder_;
  }

  VLOG(0) << fmt::format(
      "Selected {} with v {} A {}", prefixedBuild, pbuild, adv);
  return build;
}

bool StrategyModule::shouldListenToBos(State* state) {
  if (state->currentGameTime() >= bosStartTime_) {
    return true;
  }

  // If the opponent proxies or attacks, start BOS immediately
  for (Unit* u : state->unitsInfo().enemyUnits()) {
    if (!u->type->isWorker && !u->type->supplyProvided &&
        !u->type->isRefinery) {
      float baseDistance = kfInfty;
      for (Position pos : state->areaInfo().candidateEnemyStartLocations()) {
        float d = state->areaInfo().walkPathLength(u->pos(), pos);
        if (d < baseDistance) {
          baseDistance = d;
        }
      }
      float myBaseDistance = state->areaInfo().walkPathLength(
          u->pos(), state->areaInfo().myStartLocation());
      if (myBaseDistance < baseDistance * 2.0f) {
        VLOG(0) << "Proxy or attack detected, starting BOS";
        bosStartTime_ = state->currentGameTime();
        return true;
      }
    }
  }
  return false;
}
#endif // HAVE_TORCH

std::shared_ptr<ProxyTask> StrategyModule::getProxyTaskWithCommand(
    State* state,
    Command command) {
  for (auto& task : state->board()->tasksOfModule(this)) {
    auto ptask = std::static_pointer_cast<StrategyProxyTask>(task);
    if (ptask->command == command) {
      return ptask;
    }
  }
  return nullptr;
}

} // namespace cherrypi
