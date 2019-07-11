/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "cherrypi.h"

#include "buildtype.h"

#include "features/features.h"

#include "common/logging.h"
#include "common/rand.h"
#include "modules.h"

#include <backward/backward.hpp>
#include <fmt/format.h>
#include <fmt/time.h>
#include <glog/logging.h>
#include <torchcraft/client.h>

#if defined(_MSC_VER)
#include <Windows.h>
#elif defined(__APPLE__)
#include <pthread.h>
#else
#include <sys/syscall.h>
#endif

#ifndef WITHOUT_POSIX
#include <signal.h>
#include <sys/wait.h>
#endif // !WITHOUT_POSIX

#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

// Detect TSAN
#if defined(__SANITIZE_THREAD__) // GCC
#define WITH_TSAN
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer) // clang
#define WITH_TSAN
#endif
#else
#undef WITH_TSAN
#endif

namespace cherrypi {

namespace {

#ifndef WITHOUT_POSIX
// A wrapper around write(2) that supresses compiler warnings regarding ignored
// return values. We don't care about that in signal handlers, and there's not
// much we could do anyway.
void writeIgnReturn(int fd, void const* buf, size_t count) {
  auto r = write(fd, buf, count);
  (void)r;
}

void handleSigChld(int sig) {
  // itoa() without memory allocations; should be safe to call in this signal
  // handler.
  auto myItoa = [](char* buf, int* len, int i) {
    char* out = buf;
    int q = std::abs(i);
    do {
      *out++ = "0123456789"[q % 10];
      q /= 10;
    } while (q);
    if (i < 0) {
      *out++ = '-';
    }
    // Reverse result
    char* end = out;
    *len = end - buf;
    while ((buf != out) && (buf != --out)) {
      char t = *buf;
      *buf = *out;
      *out = t;
      buf++;
    }
    *end = '\0';
  };

  int bak = errno;
  int status;
  char buf[32];
  int blen;
  while (true) {
    int pid = waitpid(-1, &status, WNOHANG);
    if (pid <= 0) {
      break;
    }
    if (VLOG_IS_ON(2)) {
      auto fd = STDERR_FILENO;
      char const msg1[] = "Child process ";
      writeIgnReturn(fd, msg1, sizeof(msg1));
      myItoa(buf, &blen, pid);
      writeIgnReturn(fd, buf, blen);
      char const msg2[] = " terminated";
      writeIgnReturn(fd, msg2, sizeof(msg2));
      if (WIFEXITED(status)) {
        char const msg3[] = ": exit status ";
        writeIgnReturn(fd, msg3, sizeof(msg3));
        myItoa(buf, &blen, WEXITSTATUS(status));
        writeIgnReturn(fd, buf, blen);
      } else if (WIFSIGNALED(status)) {
        char const msg3[] = ": received signal ";
        writeIgnReturn(fd, msg3, sizeof(msg3));
        myItoa(buf, &blen, WTERMSIG(status));
        writeIgnReturn(fd, buf, blen);
      }
      writeIgnReturn(fd, "\n", 1);
    }
  }
  errno = bak;
}

void handleBacktraceRequest(int sig) {
  backward::StackTrace st;
  st.load_here(64);
  backward::Printer p;
  p.print(st);
}

#endif // !WITHOUT_POSIX
} // namespace

void init(int64_t randomSeed) {
  // Don't use backward under TSAN to prevent infinite loops (signal-unsafe call
  // (i.e. malloc) inside of a signal)
#ifndef WITH_TSAN
  static backward::SignalHandling sh;
#endif // !WITH_TSAN
  // Needed for timers in our bot
  static_assert(
      hires_clock::is_steady, "Steady high-resolution clock required");

  common::Rand::setSeed(randomSeed);

  auto f = []() {
    ::torchcraft::init();
    buildtypes::initialize();

    features::initialize();
    installSignalHandlers();
  };

  static std::once_flag flag;
  std::call_once(flag, f);
}

void init() {
  init(common::Rand::defaultRandomSeed());
}

void installSignalHandlers() {
#ifndef WITHOUT_POSIX
  auto f = []() {
    {
      // React to termination of child processes. We'll need to reap them, and
      // for debugging purposes we also want to log their exit status
      // (otherwise, we could just ignore the signal).
      struct sigaction sa;
      sa.sa_handler = &handleSigChld;
      sigemptyset(&sa.sa_mask);
      // Restart OS functions when interrupted by a system call
      // Don't fire for stop/continue signals on child processes
      sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
      if (sigaction(SIGCHLD, &sa, 0) == -1) {
        throw std::system_error(errno, std::system_category());
      }
    }

    {
      // Print backtraces on USR2
      struct sigaction sa;
      sa.sa_handler = &handleBacktraceRequest;
      sigemptyset(&sa.sa_mask);
      // Restart OS functions when interrupted by a system call
      // Don't fire for stop/continue signals on child processes
      sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
      if (sigaction(SIGUSR2, &sa, 0) == -1) {
        throw std::system_error(errno, std::system_category());
      }
    }
  };

  static std::once_flag flag;
  std::call_once(flag, f);
#endif // !WITHOUT_POSIX
}

void initLogging(
    const char* execName,
    std::string logSinkDir,
    bool logSinkToStderr) {
  common::initLogging(execName, logSinkDir, logSinkToStderr);
}

void shutdown(bool logSinkToStderr) {
  common::shutdownLogging(logSinkToStderr);
  google::ShutdownGoogleLogging();
}

} // namespace cherrypi
