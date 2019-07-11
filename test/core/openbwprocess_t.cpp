/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "gameutils/game.h"
#include "gameutils/openbwprocess.h"
#include "player.h"
#include "utils.h"

#include <fcntl.h>
#include <unistd.h>

using namespace cherrypi;

DECLARE_string(bwapilauncher_directory);

namespace {

int countOpenFiles() {
  auto maxFd = getdtablesize();
  int n = 0;
  for (int i = 0; i < maxFd; i++) {
    auto fd = fcntl(i, F_GETFD);
    if (fd >= 0) {
      n++;
    }
  }
  return n;
}

} // namespace

/*
 * We frequently produced file descriptor leaks in OpenBwProcess which
 * surfaced when repeatedly instantiating scenarios.
 * The following tests simulate a few cases of scenario creation and verify that
 * the number of open files stays constant.
 */
CASE("openbwprocess/no_fd_leaks/base") {
  int numFdBefore = countOpenFiles();

  for (int i = 0; i < 5; i++) {
    auto testSetup = [] {
      auto scenario =
          GameSinglePlayerUMS("test/maps/eco-base-terran.scm", "Zerg");
      Player player(scenario.makeClient());
      player.init();
      player.step();
    };
    EXPECT((testSetup(), true));
  }

  int numFdAfter = countOpenFiles();
  EXPECT(numFdAfter == numFdBefore);
}

CASE("openbwprocess/no_fd_leaks/bad_map") {
  int numFdBefore = countOpenFiles();

  for (int i = 0; i < 5; i++) {
    auto testSetup = [] {
      auto scenario =
          GameSinglePlayerUMS("test/maps/this-map-does-not.exist", "Zerg");
      try {
        Player player(scenario.makeClient());
        player.init();
        player.step();
      } catch (std::runtime_error const& e) {
        char const* prefix = "BWAPILauncher(";
        char const* suffix = ") died prematurely";
        if (!utils::startsWith(e.what(), prefix) ||
            !utils::endsWith(e.what(), suffix)) {
          // We hit something else -- report
          throw;
        }
      }
    };
    EXPECT((testSetup(), true));
  }

  int numFdAfter = countOpenFiles();
  EXPECT(numFdAfter == numFdBefore);
}

CASE("openbwprocess/no_fd_leaks/selfplay") {
  int numFdBefore = countOpenFiles();

  for (int i = 0; i < 5; i++) {
    auto testSetup = [] {
      auto scenario = GameMultiPlayer(
          "maps/(4)Fighting Spirit.scx",
          tc::BW::Race::Zerg,
          tc::BW::Race::Zerg);
      Player player1(scenario.makeClient1());
      Player player2(scenario.makeClient2());
      player1.init();
      player2.init();
      player1.step();
      player2.step();
    };
    EXPECT((testSetup(), true));
  }
  int numFdAfter = countOpenFiles();
  EXPECT(numFdAfter == numFdBefore);
}

CASE("openbwprocess/bwapilauncher_not_in_path") {
  std::string oldPath = ::getenv("PATH");
  std::string oldDir = FLAGS_bwapilauncher_directory;
  ::setenv("PATH", "/some/path/that/does/not/exist", 1);
  FLAGS_bwapilauncher_directory = "/some/path/that/does/not/exist";
  EXPECT_THROWS(GameSinglePlayerUMS("test/maps/eco-base-terran.scm", "Zerg"));
  ::setenv("PATH", oldPath.c_str(), 1);
  FLAGS_bwapilauncher_directory = oldDir;
}

CASE("openbwprocess/player_name_too_long") {
  std::string playerName = std::string(500, 'a');
  GameSinglePlayer scenario(
      GameOptions("maps/(4)Fighting Spirit.scx"),
      GamePlayerOptions(tc::BW::Race::Zerg).name(playerName),
      GamePlayerOptions(tc::BW::Race::Terran));
  Player player1(scenario.makeClient());
  player1.init();
  for (int i = 0; i < 10; ++i) {
    player1.step();
  }
  player1.leave();
  while (!player1.state()->gameEnded()) {
    player1.step();
  }
  EXPECT(true == true);
}
