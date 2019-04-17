/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "common.h"

#include "models/bos/sample.h"

#include "utils.h"

#include <common/fsutils.h>
#include <common/rand.h>

using namespace cpid;
namespace fsutils = common::fsutils;

namespace cherrypi {

std::unique_ptr<GameVsBotInWine> makeBosScenario(
    std::string const& maps,
    std::string const& opponents,
    std::string playOutputDir) {
  auto pool = mapPool(maps);
  std::shuffle(
      pool.begin(), pool.end(), common::Rand::makeRandEngine<std::mt19937>());
  auto opponent = selectRandomOpponent(opponents);
  auto scenario = std::make_unique<GameVsBotInWine>(
      pool, opponent, std::move(playOutputDir));
  scenario->setAutoDelete(true);
  return scenario;
}

std::vector<std::string> mapPool(std::string const& mapDirOrFile) {
  if (fsutils::isdir(mapDirOrFile) == false) {
    return {mapDirOrFile};
  }
  auto all = fsutils::findr(mapDirOrFile, "*.sc[xm]");
  // Remove duplicates
  std::unordered_set<std::string> names;
  for (auto it = all.begin(); it < all.end();) {
    auto base = fsutils::basename(*it);
    if (names.find(base) == names.end()) {
      names.insert(base);
      ++it;
    } else {
      it = all.erase(it);
    }
  }
  return all;
}

size_t numBuilds(std::string const& builds) {
  auto const& boMap = bos::buildOrderMap();
  if (builds.empty() || builds == "ALL") {
    return boMap.size();
  } else {
    return utils::stringSplit(builds, '_').size();
  }
}

std::string selectRandomBuild(
    std::string const& builds,
    std::string const& opponent) {
  std::vector<std::string> buildv;
  if (builds.empty() || builds == "ALL") {
    buildv = bos::targetBuilds();
  } else {
    buildv = utils::stringSplit(builds, '_');
  }
  auto opponentRace = bos::getOpponentRace(opponent);
  auto buildvFiltered = std::vector<std::string>();
  std::copy_if(
      buildv.begin(),
      buildv.end(),
      std::back_inserter(buildvFiltered),
      [&](const std::string build) {
        return build.rfind(opponentRace, 0) == 0;
      });
  if (buildvFiltered.empty()) {
    throw std::runtime_error("No build for this opponent!");
  }
  return utils::stringSplit(
             buildvFiltered[common::Rand::rand() % buildvFiltered.size()], '-')
      .at(1);
}

std::string selectRandomOpponent(std::string const& opponents) {
  auto opv = utils::stringSplit(opponents, ':');
  return opv[common::Rand::rand() % opv.size()];
}

} // namespace cherrypi
