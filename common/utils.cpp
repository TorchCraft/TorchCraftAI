/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "utils.h"

#include <fstream>

#ifdef __linux__
#include <unistd.h>
#endif // __linux__

namespace common {

double memoryUsage() {
#ifdef __linux__
  // 'file' stat seems to give the most reliable results
  std::ifstream stat_stream("/proc/self/stat", std::ios_base::in);

  // The executable name could be evil, so we match it
  std::ifstream comm_stream("/proc/self/comm", std::ios_base::in);
  std::string comm;
  comm_stream >> comm;

  stat_stream.ignore(2048, ' '); // pid
  stat_stream.ignore(comm.length() + 3); // comm, parens, and the extra space
  stat_stream.ignore(2048, ' '); // state
  stat_stream.ignore(2048, ' '); // ppid
  stat_stream.ignore(2048, ' '); // pgrp
  stat_stream.ignore(2048, ' '); // session
  stat_stream.ignore(2048, ' '); // tty_nr
  stat_stream.ignore(2048, ' '); // tpgid
  stat_stream.ignore(2048, ' '); // flags
  stat_stream.ignore(2048, ' '); // minflt
  stat_stream.ignore(2048, ' '); // cminflt
  stat_stream.ignore(2048, ' '); // majflt
  stat_stream.ignore(2048, ' '); // cmajflt
  stat_stream.ignore(2048, ' '); // utime
  stat_stream.ignore(2048, ' '); // stime
  stat_stream.ignore(2048, ' '); // cutime
  stat_stream.ignore(2048, ' '); // cstime
  stat_stream.ignore(2048, ' '); // priority
  stat_stream.ignore(2048, ' '); // nice
  stat_stream.ignore(2048, ' '); // O
  stat_stream.ignore(2048, ' '); // itrealvalue
  stat_stream.ignore(2048, ' '); // starttime
  stat_stream.ignore(2048, ' '); // vsize
  long rss;

  stat_stream >> rss;

  stat_stream.close();

  // in case x86-64 is configured to use 2MB pages
  int64_t page_size_kb = sysconf(_SC_PAGE_SIZE) / 1024;
  return rss * page_size_kb;
#else // __linux__
  throw std::runtime_error("Can only get memory usage on UNIX");
#endif // __linux__
}

} // namespace common
