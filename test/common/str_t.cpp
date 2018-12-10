/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#include "test.h"

#include "common/str.h"

using namespace common;

CASE("common/string/split") {
  using strings = std::vector<std::string>;
  EXPECT(stringSplit("", ' ') == strings({""}));
  EXPECT(stringSplit("a::bcd:a", ':') == strings({"a", "", "bcd", "a"}));
  EXPECT(stringSplit("a::b:a", '_') == strings({"a::b:a"}));
  EXPECT(stringSplit("a::b:a_", '_') == strings({"a::b:a", ""}));
  EXPECT(stringSplit("a::b:a_", '_', 1) == strings({"a::b:a", ""}));
  EXPECT(stringSplit("a::b:a_", '_', 0) == strings({"a::b:a_"}));
}

CASE("common/string/startsWith") {
  EXPECT(startsWith("foobar", "foo") == true);
  EXPECT(startsWith("foobar", "fo0") == false);
  EXPECT(startsWith("foobar", "Foo") == false);
  EXPECT(startsWith("foo", "foo") == true);
  EXPECT(startsWith("foo", "fooooo") == false);
  EXPECT(startsWith("", "") == true);
}

CASE("common/string/endsWith") {
  EXPECT(endsWith("foobar", "bar") == true);
  EXPECT(endsWith("foobar", "baR") == false);
  EXPECT(endsWith("foobar", "Bar") == false);
  EXPECT(endsWith("foo", "foo") == true);
  EXPECT(endsWith("foo", "ffffoo") == false);
  EXPECT(endsWith("", "") == true);
}
