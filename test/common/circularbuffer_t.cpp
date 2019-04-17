/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "common/circularbuffer.h"

SCENARIO("circularbuffer/wrap") {
  GIVEN("An empty buffer of ints with capacity 5") {
    common::CircularBuffer<int> buf(5);
    EXPECT(buf.size() == size_t(0));

    WHEN("3 items are inserted") {
      for (int i = 0; i < 3; i++) {
        buf.push(i);
      }
      THEN("size is 3") {
        EXPECT(buf.size() == size_t(3));
      }
    }

    WHEN("7 items are inserted") {
      for (int i = 0; i < 7; i++) {
        buf.push(i);
      }
      THEN("size is 5") {
        EXPECT(buf.size() == size_t(5));
      }
      AND_THEN("can retrieve last element") {
        EXPECT(buf.at(0) == 6);
      }
      AND_THEN("can retrieve past 4 elements") {
        EXPECT(buf.at(-1) == 5);
        EXPECT(buf.at(-2) == 4);
        EXPECT(buf.at(-3) == 3);
        EXPECT(buf.at(-4) == 2);
      }
    }
  }
}

SCENARIO("circularbuffer/push") {
  GIVEN("An empty buffer of std::vector<int>") {
    common::CircularBuffer<std::vector<int>> buf(2);
    EXPECT(buf.size() == size_t(0));

    WHEN("an empty is pushed") {
      buf.push();
      THEN("size is 1 and last element looks right") {
        EXPECT(buf.size() == size_t(1));
        EXPECT(buf.at(0).empty());
      }
    }
    WHEN("a vector is pushed") {
      std::vector<int> v{1, 2, 3};
      buf.push(v);
      THEN("size is 1 and last element looks right") {
        EXPECT(buf.size() == size_t(1));
        EXPECT(buf.at(0) == v);
      }
    }
    WHEN("a const vector is pushed") {
      buf.push(std::vector<int>());
      THEN("size is 1 and last element looks right") {
        EXPECT(buf.size() == size_t(1));
        EXPECT(buf.at(0).empty());
      }
    }
  }
}
