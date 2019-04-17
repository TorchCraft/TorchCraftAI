/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "common/checksum.h"

#include <fmt/format.h>

using namespace common;

CASE("checksum/toHex") {
  std::vector<uint8_t> d;
  for (int i = 0; i <= 0xFF; i++) {
    d.push_back(uint8_t(i));
  }
  auto hex = toHex(d);
  EXPECT(hex.size() == 512U);
  std::string target;
  for (auto c : d) {
    target += fmt::format("{:02x}", c);
  }
  EXPECT(hex == target);
}

CASE("checksum/sha256") {
  EXPECT(
      toHex(sha256sum(std::vector<uint8_t>{})) ==
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT(
      toHex(sha256sum("foobar")) ==
      "c3ab8ff13720e8ad9047dd39466b3c8974e592c2fa383d4a3960714caef0c4f2");

  std::vector<uint8_t> d;
  for (int i = 0; i <= 0xFF; i++) {
    d.push_back(uint8_t(i));
  }
  // for i in $(seq 0 255); do printf "\x$(printf %x $i)"; done | sha256sum
  EXPECT(
      toHex(sha256sum(d)) ==
      "40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880");
}

CASE("checksum/md5") {
  EXPECT(
      toHex(md5sum(std::vector<uint8_t>{})) ==
      "d41d8cd98f00b204e9800998ecf8427e");
  EXPECT(toHex(md5sum("foobar")) == "3858f62230ac3c915f300c664312c63f");

  std::vector<uint8_t> d;
  for (int i = 0; i <= 0xFF; i++) {
    d.push_back(uint8_t(i));
  }
  // for i in $(seq 0 255); do printf "\x$(printf %x $i)"; done | md5sum
  EXPECT(toHex(md5sum(d)) == "e2c865db4162bed963bfaa9ef6ac18f0");

  std::vector<uint8_t> d2;
  for (int i = 0; i <= 5000; i++) {
    d2.push_back(uint8_t(i % 256));
  }
  // for i in $(seq 0 5000); do printf "\x$(printf %x $((i % 256)))"; done |
  // md5sum -
  EXPECT(toHex(md5sum(d2)) == "393d25f8ed132b7880daf28e25b5c412");
}
