/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "bandit.h"
#include <fmt/format.h>

namespace cherrypi {

namespace model {

namespace {
const tc::BW::Race terran = tc::BW::Race::Terran;
const tc::BW::Race protoss = tc::BW::Race::Protoss;
const tc::BW::Race zerg = tc::BW::Race::Zerg;
const tc::BW::Race unknown = tc::BW::Race::Unknown;
const std::vector<tc::BW::Race> allRaces{terran, protoss, zerg, unknown};
} // namespace

// Update the tournament configuration by modifying this function
BuildOrderConfigurations buildOrdersForTournament(
    const std::string& rawOpponentName) {
  // Start with the default builds, so we know what's valid for Build Order
  // Switch.
  // Then disable all builds as openings, and re-enable them selectively.
  std::unordered_map<std::string, BuildOrderConfig> builds =
      buildOrdersForTraining();
  for (auto& build : builds) {
    build.second.validOpening(false).enemyRaces({});
  }

  auto opponentName = utils::stringToLower(rawOpponentName);
  auto enable = [&](std::vector<tc::BW::Race> addedRaces,
                    const char* buildName) {
    if (!utils::contains(builds, buildName)) {
      VLOG(0) << "WARNING: Trying to enable build not found in training "
                 "configuration: "
              << buildName;
    }
    auto& build = builds[buildName];
    build.validOpening(true);
    std::vector<tc::BW::Race> finalRaces(build.enemyRaces());
    for (auto addedRace : addedRaces) {
      finalRaces.push_back(addedRace);
    }
    build.enemyRaces(std::move(finalRaces));
  };
  auto enableAllRaces = [&](const char* buildName) {
    enable(allRaces, buildName);
  };
  auto isOpponent = [&](const char* name) {
    auto nameString = utils::stringToLower(std::string(name));
    bool output = opponentName.find(nameString) != std::string::npos;
    if (output) {
      VLOG(0) << fmt::format(
          "Found build configuration named {} matching opponent {}",
          name,
          rawOpponentName,
          opponentName);
    }
    return output;
  };

  // Returning opponents
  //
  if (isOpponent("AILien")) {
    enableAllRaces("zve9poolspeed");
    enableAllRaces("zvz9poolspeed");
    return builds;
  } else if (isOpponent("AIUR")) {
    enableAllRaces("zvtmacro");
    enableAllRaces("zvpohydras");
    enableAllRaces("zvp10hatch");
    return builds;
  } else if (isOpponent("Arrakhammer")) {
    enableAllRaces("10hatchling");
    enableAllRaces("zvz9poolspeed");
    return builds;
  } else if (isOpponent("Iron")) {
    // BOS disabled for this specific build because the model hasn't seen it.
    enableAllRaces("hydracheese");
    return builds;
  } else if (isOpponent("UAlbertaBot")) {
    enableAllRaces("zve9poolspeed"); // 99% with BOS 4 runs
    enableAllRaces("9poolspeedlingmuta"); // 60 with BOS 4 runs
    return builds;
  } else if (isOpponent("Ximp")) {
    // All these builds tested at 100%
    enableAllRaces("zvpohydras");
    enableAllRaces("zvtmacro");
    enableAllRaces("zvp3hatchhydra");
    return builds;
  }

  // Opponents we have some expectations for
  //
  if (isOpponent("Microwave")) {
    enableAllRaces("zvzoverpool");
    enableAllRaces("zvz9poolspeed");
    enableAllRaces("zvz9gas10pool");
    return builds;
  } else if (isOpponent("Steamhammer")) {
    enableAllRaces("zve9poolspeed");
    enableAllRaces("zvz9poolspeed");
    enableAllRaces("zvz12poolhydras");
    enableAllRaces("10hatchling");
    return builds;
  } else if (isOpponent("ZZZKBot")) {
    enableAllRaces("9poolspeedlingmuta");
    enableAllRaces("10hatchling");
    enableAllRaces("zvz9poolspeed");
    enableAllRaces("zvzoverpool");
    return builds;
  } else if (
      isOpponent("ISAMind") || isOpponent("Locutus") || isOpponent("McRave") ||
      isOpponent("DaQin")) {
    enableAllRaces("zvtmacro");
    enableAllRaces("zvp6hatchhydra");
    enableAllRaces("3basepoollings");
    enableAllRaces("zvpomutas");
    return builds;
  } else if (isOpponent("CUNYBot")) {
    enableAllRaces("zvzoverpoolplus1");
    enableAllRaces("zvz9gas10pool");
    enableAllRaces("zvz9poolspeed");
    return builds;
  } else if (isOpponent("HannesBredberg")) {
    enableAllRaces("zvtp1hatchlurker");
    enableAllRaces("zvt2baseultra");
    enableAllRaces("zvt3hatchlurker");
    enableAllRaces("zvp10hatch");
    return builds;
  } else if (isOpponent("LetaBot")) {
    // Top builds across LetaBot versions
    enableAllRaces("zvtmacro");
    enableAllRaces("3basepoollings");
    enableAllRaces("zvt2baseguardian");
    enableAllRaces("zve9poolspeed");
    enableAllRaces("10hatchling");
    return builds;
  } else if (
      isOpponent("MetaBot") || isOpponent("MegaBot") || isOpponent("Skynet")) {
    // MetaBot appears to be updated MegaBot, which essentialy plays as Skynet
    enableAllRaces("zvtmacro");
    enableAllRaces("zvpohydras");
    enableAllRaces("zvpomutas");
    enableAllRaces("zve9poolspeed");
    return builds;
  } else if (isOpponent("WillyT")) {
    enableAllRaces("zvt2baseultra");
    enableAllRaces("12poolmuta");
    enableAllRaces("2hatchmuta");
    // TODO no 9 pool?
    return builds;
  } else if (isOpponent("SAIDA")) {
    enableAllRaces("zvtantimech");
    enableAllRaces("zvt2baseultra");
    enableAllRaces("zvt3hatchlurker");
    enableAllRaces("zvp10hatch");
    return builds;
  }

  // Default builds per race

  VLOG(0) << "Using default tournament bandit configuration";

  // ZvT
  {
    auto race = terran;
    enable({race}, "zvt2baseultra");
    enable({race}, "zvtmacro");
    enable({race}, "zvt3hatchlurker");
    enable({race}, "zve9poolspeed");
    enable({race}, "zvp10hatch");
  }

  // ZvP
  {
    auto race = protoss;
    enable({race}, "zve9poolspeed");
    enable({race}, "zvtmacro");
    enable({race}, "zvp10hatch");
    enable({race}, "zvpohydras");
  }

  // ZvZ
  {
    auto race = zerg;
    enable({race}, "10hatchling");
    enable({race}, "zve9poolspeed");
    enable({race}, "zvz9poolspeed");
    enable({race}, "zvzoverpool");
  }

  // ZvR
  {
    auto race = unknown;
    enable({race}, "10hatchling");
    enable({race}, "zve9poolspeed");
    enable({race}, "9poolspeedlingmuta");
  }

  return builds;
}

// Update the training configuration by modifying this function
BuildOrderConfigurations buildOrdersForTraining() {
  std::unordered_map<std::string, BuildOrderConfig> builds;

  // clang-format off
  builds["10hatchling"]
      .validOpening(true)
      .validSwitch(true);

  builds["12hatchhydras"]
      .validSwitch(true)
      .enemyRaces({terran, protoss});

  builds["12poolmuta"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({terran, protoss});

  builds["2basemutas"]
      .enemyRaces({terran});

  builds["2hatchmuta"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({terran, protoss});

  builds["3basepoollings"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({terran, protoss});

  builds["5pool"]
      .switchEnabled(false);

  builds["9poolspeedlingmuta"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({zerg, unknown});

  builds["delayed4pool"];
  builds["hydracheese"]
      .switchEnabled(false);

  builds["hydras"]
      .validSwitch(true);

  builds["ultras"]
      .validSwitch(true);

  builds["midmassling"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({terran, protoss});

  builds["zve9poolspeed"]
      .validOpening(true)
      .validSwitch(true);

  builds["zvp10hatch"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({terran, protoss});

  builds["zvp3hatchhydra"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({protoss});

  builds["zvp6hatchhydra"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({protoss});

  builds["zvpmutas"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({protoss});

  builds["zvpohydras"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({protoss});

  builds["zvpomutas"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({protoss});

  builds["zvt2basedefiler"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({terran});
  
  builds["zvt2baseultra"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({terran});

  builds["zvt2baseguardian"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({terran});

  builds["zvtp1hatchlurker"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({terran, protoss});

  builds["zvt3hatchlurker"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({terran});

  builds["zvtmacro"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({terran, protoss});

  builds["zvtantimech"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({terran, protoss});
      
  builds["zvzoverpoolplus1"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({zerg});

  builds["zvzoverpool"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({zerg});

  builds["zvz9gas10pool"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({zerg});

  builds["zvz9poolspeed"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({zerg});

  builds["zvz12poolhydras"]
      .validOpening(true)
      .validSwitch(true)
      .enemyRaces({zerg});

  builds["pve2gate1012"]
      .validOpening(true)
      .ourRaces({protoss});
      
  builds["pvp2gatedt"]
      .validOpening(true)
      .ourRaces({protoss})
      .enemyRaces({protoss});
      
  builds["pve4gate"]
      .validOpening(true)
      .ourRaces({protoss});
  
  // Can't function without Protoss-aware building placement
  builds["pvzffe5gategoon"]
      .validOpening(false)
      .ourRaces({protoss})
      .enemyRaces({zerg});

  builds["t5rax"]
      .validOpening(true)
      .ourRaces({terran});

  builds["tvtz2portwraith"]
      .validOpening(true)
      .ourRaces({terran})
      .enemyRaces({terran, zerg});      
  
  builds["tvpjoyorush"]
      .validOpening(true)
      .ourRaces({terran})
      .enemyRaces({protoss});

  // clang-format on
  return builds;
}

} // namespace model

} // namespace cherrypi
