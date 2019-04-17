/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "netutils.h"

#include <stdexcept>
#include <system_error>

#ifndef WITHOUT_POSIX
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/types.h>
#endif

namespace cpid {
namespace netutils {

std::string sockaddrToString(struct ::sockaddr* addr) {
#ifndef WITHOUT_POSIX
  char address[INET6_ADDRSTRLEN + 1];
  if (addr->sa_family == AF_INET) {
    struct sockaddr_in* s = reinterpret_cast<struct sockaddr_in*>(addr);
    if (::inet_ntop(AF_INET, &(s->sin_addr), address, INET_ADDRSTRLEN) ==
        nullptr) {
      throw std::system_error(errno, std::system_category());
    }
    address[INET_ADDRSTRLEN] = '\0';
  } else if (addr->sa_family == AF_INET6) {
    struct sockaddr_in6* s = reinterpret_cast<struct sockaddr_in6*>(addr);
    if (::inet_ntop(AF_INET6, &(s->sin6_addr), address, INET6_ADDRSTRLEN) ==
        nullptr) {
      throw std::system_error(errno, std::system_category());
    }
    address[INET6_ADDRSTRLEN] = '\0';
  } else {
    throw std::runtime_error("unsupported protocol");
  }
  return address;
#else // WITHOUT_POSIX
  throw std::runtime_error("sockaddrToString() not implemented");
  return "";
#endif // WITHOUT_POSIX
}

std::vector<std::string> getInterfaceAddresses() {
#ifndef WITHOUT_POSIX
  struct ifaddrs* ifa;
  if (::getifaddrs(&ifa) != 0) {
    throw std::system_error(errno, std::system_category());
  }
  try {
    std::vector<std::string> addresses;
    auto ptr = ifa;
    while (ptr != nullptr) {
      struct sockaddr* addr = ptr->ifa_addr;
      if (addr) {
        bool is_loopback = ptr->ifa_flags & IFF_LOOPBACK;
        bool is_ip = addr->sa_family == AF_INET || addr->sa_family == AF_INET6;
        if (is_ip && !is_loopback) {
          try {
            addresses.push_back(sockaddrToString(addr));
          } catch (...) {
          }
        }
      }
      ptr = ptr->ifa_next;
    }

    ::freeifaddrs(ifa);
    return addresses;
  } catch (...) {
    ::freeifaddrs(ifa);
    throw;
  }
#else // WITHOUT_POSIX
  throw std::runtime_error("getInterfaceAddresses() not implemented");
  return std::vector<std::string>();
#endif // WITHOUT_POSIX
}

} // namespace netutils
} // namespace cpid
