/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "fifopipes.h"

#ifndef WITHOUT_POSIX
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <common/fsutils.h>
#include <glog/logging.h>

namespace fsutils = common::fsutils;

namespace cherrypi {

namespace detail {

FifoPipes::FifoPipes() {
#ifndef WITHOUT_POSIX
  root_ = fsutils::mktempd();
  pipe1 = root_ + "/1";
  pipe2 = root_ + "/2";
  if (mkfifo(pipe1.c_str(), 0666) != 0) {
    LOG(ERROR) << "Cannot create named pipe at " << pipe1;
    fsutils::rmrf(root_);
    throw std::system_error(errno, std::system_category());
  }
  if (mkfifo(pipe2.c_str(), 0666) != 0) {
    LOG(ERROR) << "Cannot create named pipe at " << pipe2;
    fsutils::rmrf(root_);
    throw std::system_error(errno, std::system_category());
  }
#else
  throw std::runtime_error("Not available for windows");
#endif // !WITHOUT_POSIX
}

FifoPipes::~FifoPipes() {
#ifndef WITHOUT_POSIX
  fsutils::rmrf(root_);
#endif // !WITHOUT_POSIX
}

} // namespace detail

} // namespace cherrypi