#include "common/logging.h"

#include <fmt/format.h>
#include <fmt/time.h>
#include <glog/logging.h>

#if defined(_MSC_VER)
#include <Windows.h>
#elif defined(__APPLE__)
#include <pthread.h>
#else
#include <sys/syscall.h>
#endif

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>

DEFINE_string(
    vfilter,
    "",
    "verbose log filters based on the full path of a file");

namespace common {
#ifdef WITHOUT_POSIX
struct tm* localtime_r(time_t* _clock, struct tm* _result) {
  struct tm* p = localtime(_clock);
  if (p) {
    *(_result) = *p;
  }
  return p;
}
#endif // WITHOUT_POSIX

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
    static auto vfilter =
        std::regex(FLAGS_vfilter, std::regex_constants::egrep);
    if (severity == google::GLOG_INFO && !FLAGS_vfilter.empty() &&
        !std::regex_search(fullFilename, vfilter)) {
      return;
    }

    auto prefix = [](auto severity) {
      switch (severity) {
        case google::GLOG_INFO:
          return "I";
        case google::GLOG_WARNING:
          return "W";
        case google::GLOG_ERROR:
          return "E";
        case google::GLOG_FATAL:
          return "F";
        default:
          return "?";
      }
    };

    chan << fmt::format(
        "{}{:05}/{} {:%m/%d %T} [{}:{}] ",
        prefix(severity),
        threadId(),
        logPrefix,
        *tmTime,
        baseFilename,
        line);
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

void setLoggingFrame(int frame) {
  snprintf(logPrefix, sizeof(logPrefix), "%05d", frame);
}

void unsetLoggingFrame() {
  strcpy(logPrefix, "XXXXX");
}

void shutdownLogging(bool logSinkToStderr) {
  if (!logSinkToStderr) {
    for (int i = 0; i < google::NUM_SEVERITIES; ++i) {
      logFilesForSink[i].close();
    }
  }
}

} // namespace common
