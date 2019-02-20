/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace common {

std::string toHex(std::vector<uint8_t> const& digest);

std::vector<uint8_t> sha256sum(void const* data, size_t len);
std::vector<uint8_t> sha256sum(std::string_view data) {
  return sha256sum(data.data(), data.size());
}
std::vector<uint8_t> sha256sum(std::vector<uint8_t> const& data) {
  return sha256sum(data.data(), data.size());
}

std::vector<uint8_t> md5sum(void const* data, size_t len);
std::vector<uint8_t> md5sum(std::string_view data) {
  return md5sum(data.data(), data.size());
}
std::vector<uint8_t> md5sum(std::vector<uint8_t> const& data) {
  return md5sum(data.data(), data.size());
}

} // namespace common
