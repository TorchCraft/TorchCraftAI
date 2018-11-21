/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>
#include <vector>

struct sockaddr;

namespace cpid {
namespace netutils {

std::string sockaddrToString(struct ::sockaddr* addr);

/// Returns a list of network interface addresses
std::vector<std::string> getInterfaceAddresses();

} // namespace netutils
} // namespace cpid
