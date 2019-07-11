/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "gameutils/mapmatcher.h"
#include "test.h"

CASE("mapmatcher/exact") {
  cherrypi::MapMatcher matcher;

  EXPECT(
      matcher.tryMatch("Andromeda 1.0_iCCup.scx") ==
      "maps/fuzzymatch/Andromeda 1.0_iCCup.scx");
}

CASE("mapmatcher/translated") {
  cherrypi::MapMatcher matcher;

  EXPECT(
      matcher.tryMatch("투혼") ==
      "maps/fuzzymatch/Fighting Spirit 1.3_iCCup.scx");

  EXPECT(
      matcher.tryMatch("[pOk] 투혼 1.3") ==
      "maps/fuzzymatch/Fighting Spirit 1.3_iCCup.scx");

  EXPECT(
      matcher.tryMatch("태양의 제국") ==
      "maps/fuzzymatch/Empire of the Sun 2.0_iCCup.scx");

  EXPECT(
      matcher.tryMatch("신 단장의 능선") ==
      "maps/fuzzymatch/Heartbreak Ridge 2.1_iCCup.scx");

  EXPECT(
      matcher.tryMatch("단장의 능선 1.1") ==
      "maps/fuzzymatch/Heartbreak Ridge 2.1_iCCup.scx");

  EXPECT(
      matcher.tryMatch("저격능선") ==
      "maps/fuzzymatch/Sniper Ridge 2.0_iCCup.scx");

  EXPECT(
      matcher.tryMatch("신 저격능선 2.0") ==
      "maps/fuzzymatch/Sniper Ridge 2.0_iCCup.scx");
}

CASE("mapmatcher/fuzzy") {
  cherrypi::MapMatcher matcher;

  EXPECT(
      matcher.tryMatch("| ICCUP | NeO AnDrOmeDa SE Obs 2.05_iCCup.scx") ==
      "maps/fuzzymatch/Andromeda 1.0_iCCup.scx");

  EXPECT(
      matcher.tryMatch("| iCCup | NeoMedusa 2.1") ==
      "maps/fuzzymatch/Medusa 2.2_iCCup.scx");

  EXPECT(
      matcher.tryMatch("| iCCup | ColosseumII 2.0") ==
      "maps/fuzzymatch/Colosseum 2.0_iCCup.scx");

  EXPECT(
      matcher.tryMatch("| iCCup | Fighting Spirit 1.3 (Ob)") ==
      "maps/fuzzymatch/Fighting Spirit 1.3_iCCup.scx");

  EXPECT(
      matcher.tryMatch("NeoMoonGlaive2.1") ==
      "maps/fuzzymatch/Moon Glaive 2.1_iCCup.scx");

  EXPECT(
      matcher.tryMatch("| iCCup | Circuit Breakers") ==
      "maps/fuzzymatch/Circuit Breaker 1.0_iCCup.scx");
}
