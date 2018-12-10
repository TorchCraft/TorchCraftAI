/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "cherrypi.h"

#include "buildtype.h"

#include "features/features.h"

#include "common/rand.h"
#include "modules.h"

#include <backward/backward.hpp>
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

namespace cherrypi {

namespace {

#ifdef WITHOUT_POSIX
struct tm* localtime_r(time_t* _clock, struct tm* _result) {
  struct tm* p = localtime(_clock);
  if (p) {
    *(_result) = *p;
  }
  return p;
}
#endif // WITHOUT_POSIX

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
#endif // !WITHOUT_POSIX

int threadId() {
#if defined(_MSC_VER)
  return GetCurrentThreadId();
#elif defined(__APPLE__)
  uint64_t tid = 0;
  pthread_threadid_np(pthread_self(), &tid);
  return tid;
#else
  // This is from glog's utilities.cc
  static bool lacks_gettid = false;
  if (!lacks_gettid) {
    pid_t tid = syscall(__NR_gettid);
    if (tid != -1) {
      return tid;
    }
    // Technically, this variable has to be volatile, but there is a small
    // performance penalty in accessing volatile variables and there should
    // not be any serious adverse effect if a thread does not immediately see
    // the value change to "true".
    lacks_gettid = true;
  }
  return (pid_t)(uintptr_t)pthread_self();
#endif
}

std::ofstream logFilesForSink[google::NUM_SEVERITIES];

// Thread-local logging prefix, usually using 5 digits for the frame but keeping
// space for 6 just to be on the safe side.
// TODO: This does not work too well; need to check what glog does when used
// from multiple threads.
thread_local char logPrefix[7] = "XXXXX";

/// A custom log sink
class PrefixLogSink : public google::LogSink {
 public:
  virtual void send(
      google::LogSeverity severity,
      const char* fullFilename,
      const char* baseFilename,
      int line,
      const struct ::tm* tmTime,
      const char* message,
      size_t messageLen) override {
    // Log to stderr or file depending on flag
    auto& chan = (logToStderr_) ? std::cerr : logFilesForSink[severity];

    switch (severity) {
      case google::GLOG_INFO:
        chan << "I";
        break;
      case google::GLOG_WARNING:
        chan << "W";
        break;
      case google::GLOG_ERROR:
        chan << "E";
        break;
      case google::GLOG_FATAL:
        chan << "F";
        break;
      default:
        chan << "?";
        break;
    }

    chan << std::setfill('0') << std::setw(5) << threadId() << "/";
    chan << logPrefix << " [" << baseFilename << ":" << line << "] ";
    chan.write(message, messageLen);
    chan << std::endl;
  }

  void setSinkDestination(bool logToStderr) {
    logToStderr_ = logToStderr;
  }

  bool isSinkToStderr() const {
    return logToStderr_;
  }

 private:
  bool logToStderr_ = true;
};

PrefixLogSink logSink;

std::string createLogFileName(
    const char* argv0,
    std::string logSinkDir,
    google::LogSeverity severity) {
  // Logic shamelessly stolen from glog for compatibility
  const char* slash = strrchr(argv0, '/');
#if defined(_WIN32) || defined(_WIN64)
  if (!slash) {
    slash = strrchr(argv0, '\\');
  }
#endif

  std::string logFileName(slash ? slash + 1 : argv0);

  // Append time to log file's name to make it unique
  // Same format as glog
  std::time_t timeStamp =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  struct std::tm tmTime;
  localtime_r(&timeStamp, &tmTime);

  std::ostringstream timeStringStream;
  timeStringStream.fill('0');
  timeStringStream << 1900 + tmTime.tm_year << std::setw(2) << 1 + tmTime.tm_mon
                   << std::setw(2) << tmTime.tm_mday << '-' << std::setw(2)
                   << tmTime.tm_hour << std::setw(2) << tmTime.tm_min
                   << std::setw(2) << tmTime.tm_sec;

  logFileName += "." + timeStringStream.str();

  // Should I also append a process id?
  switch (severity) {
    case google::GLOG_INFO:
      logFileName += ".INFO";
      break;
    case google::GLOG_WARNING:
      logFileName += ".WARNING";
      break;
    case google::GLOG_ERROR:
      logFileName += ".ERROR";
      break;
    case google::GLOG_FATAL:
      logFileName += ".FATAL";
      break;
    default:
      logFileName += ".UNKNOWN";
      break;
  }

  if (!logSinkDir.empty()) {
    logFileName = logSinkDir + "/" + logFileName;
  }
  return logFileName;
}

} // namespace

void init(int64_t randomSeed) {
  static backward::SignalHandling sh;
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
    // React to termination of child processes. We'll need to reap them, and for
    // debugging purposes we also want to log their exit status (otherwise, we
    // could just ignore the signal).
    struct sigaction sa;
    sa.sa_handler = &handleSigChld;
    sigemptyset(&sa.sa_mask);
    // Restart OS functions when interrupted by a system call
    // Don't fire for stop/continue signals on child processes
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, 0) == -1) {
      throw std::system_error(errno, std::system_category());
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
  // Setup all the file handles for logging for our sink
  if (!logSinkToStderr) {
    for (int i = 0; i < google::NUM_SEVERITIES; ++i) {
      logFilesForSink[i] =
          std::ofstream(createLogFileName(execName, logSinkDir, i));
    }
    logSink.setSinkDestination(logSinkToStderr);
  }

  // Logging setup: use a custom sink and disable default log-to-file by setting
  // empty destinations for all levels
  google::AddLogSink(&logSink);
  for (int i = 0; i < google::NUM_SEVERITIES; i++) {
    google::SetLogDestination(i, "");
  }
}

void shutdown(bool logSinkToStderr) {
  if (!logSinkToStderr) {
    for (int i = 0; i < google::NUM_SEVERITIES; ++i) {
      logFilesForSink[i].close();
    }
  }

  google::ShutdownGoogleLogging();
}

void setLoggingFrame(int frame) {
  snprintf(logPrefix, sizeof(logPrefix), "%05d", frame);
}

void unsetLoggingFrame() {
  strcpy(logPrefix, "XXXXX");
}

} // namespace cherrypi
