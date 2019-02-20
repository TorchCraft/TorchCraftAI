/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "gameutils/openbwprocess.h"
#include "gameutils/scenario.h"
#include "gameutils/selfplayscenario.h"
#include "player.h"
#include "utils.h"

#include <fcntl.h>
#include <unistd.h>

using namespace cherrypi;

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

template <typename Func>
void testWithFork(Func&& func) {
  func();
  OpenBwProcess::startForkServer();
  func();
  OpenBwProcess::endForkServer();
}

} // namespace

/*
 * We frequently produced file descriptor leaks in OpenBwProcess which
 * surfaced when repeatedly instantiating scenarios.
 * The following tests simulate a few cases of scenario creation and verify that
 * the number of open files stays constant.
 */
CASE("openbwprocess/no_fd_leaks/base") {
  testWithFork([&]() {
    int numFdBefore = countOpenFiles();

    for (int i = 0; i < 5; i++) {
      auto testSetup = [] {
        Scenario scenario("test/maps/eco-base-terran.scm", "Zerg");
        Player player(scenario.makeClient());
        player.init();
        player.step();
      };
      EXPECT((testSetup(), true));
    }

    int numFdAfter = countOpenFiles();
    EXPECT(numFdAfter == numFdBefore);
  });
}

CASE("openbwprocess/no_fd_leaks/bad_map") {
  int numFdBefore = countOpenFiles();

  for (int i = 0; i < 5; i++) {
    auto testSetup = [] {
      Scenario scenario("test/maps/this-map-does-not.exist", "Zerg");
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
  testWithFork([&]() {
    int numFdBefore = countOpenFiles();

    for (int i = 0; i < 5; i++) {
      auto testSetup = [] {
        SelfPlayScenario scenario(
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
  });
}

CASE("openbwprocess/bwapilauncher_not_in_path") {
  std::string oldPath = ::getenv("PATH");
  ::setenv("PATH", "/some/path/that/does/not/exist", 1);
  testWithFork([&]() {
    EXPECT_THROWS(Scenario("test/maps/eco-base-terran.scm", "Zerg"));
  });
  ::setenv("PATH", oldPath.c_str(), 1);
}
