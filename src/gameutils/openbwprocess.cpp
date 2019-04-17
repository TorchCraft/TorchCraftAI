/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "openbwprocess.h"
#include "utils.h"

#include <common/fsutils.h>

#include <torchcraft/client.h>

#include <array>
#include <cstdlib>
#include <fcntl.h>
#include <signal.h>
#ifndef WITHOUT_POSIX
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif // WITHOUT_POSIX

#include <cereal/archives/binary.hpp>
#include <cereal/types/vector.hpp>
#include <fmt/format.h>
#include <glog/logging.h>
#include <prettyprint/prettyprint.hpp>

namespace fsutils = common::fsutils;

DEFINE_string(
    bwapilauncher_directory,
    "",
    "Directory where to find BWAPILauncher. If empty will use PATH");
extern char** environ;

namespace cherrypi {

namespace {

int constexpr kPollTimeout = 1000;
int constexpr kMaxTimedoutPolls = 10;
auto constexpr kDtorGraceTime = std::chrono::milliseconds(500);

bool isExiting = false; // Dont start new forks if this is true

const std::unordered_map<std::string, std::string> versionMap_ = {
    {"420", ""},
    {"412", "-4.1.2"},
    {"374", "-3.7.4"},
};
const std::unordered_map<std::string, std::string> raceMap_ = {
    {"Z", "Zerg"},
    {"P", "Protoss"},
    {"T", "Terran"},
};

struct AIInfo {
  AIInfo(std::string const& bot) {
    if (bot.empty()) {
      auto bwenvPath = getenv("BWENV_PATH");
      if (bwenvPath != nullptr) {
        aipath = bwenvPath;
      } else {
#ifdef __APPLE__
        std::vector tryPaths = {
            "build/3rdparty/torchcraft/BWEnv/BWEnv.dylib",
            "3rdparty/torchcraft/BWEnv/BWEnv.dylib",
        };
#else
        std::vector tryPaths = {
            "build/3rdparty/torchcraft/BWEnv/BWEnv.so",
            "3rdparty/torchcraft/BWEnv/BWEnv.so",
        };
#endif
        aipath = "";
        for (auto const& p : tryPaths) {
          if (fsutils::exists(p)) {
            aipath = p;
          }
        }
        if (aipath.empty()) {
          throw std::runtime_error(fmt::format(
              "Unable to find BWEnv library. Tried {}",
              common::joinVector(tryPaths, ',')));
        }
      }
      ainame = "BWEnv";
    } else {
      if (bot.find(".dll") == std::string::npos) {
        throw std::runtime_error("Cannot play with non-dll bots");
      }
      auto basename = fsutils::basename(bot, ".dll");
      auto splits = utils::stringSplit(basename, '_', 2);
      if (splits.size() != 3) {
        throw std::runtime_error(
            "Bot name must be VERSION_RACE_NAME, like 412_T_Ironbot.dll");
      }

      auto versionPrefix = versionMap_.find(splits[0]);
      if (versionPrefix == versionMap_.end()) {
        throw std::runtime_error(
            "Version must be 374, 412, or 420, not " + splits[0]);
      }
      bwapisuffix = versionPrefix->second;

      auto race = raceMap_.find(splits[1]);
      if (race == raceMap_.end()) {
        throw std::runtime_error("Race must be P, T, or Z, not " + splits[1]);
      }
      airace = race->second;

      auto aiPathPrefix =
          versionPrefix->second.size() == 0 ? "" : "/starcraft/bwloader.so:";
      aipath = aiPathPrefix + bot;
      ainame = splits.back();

      if (!fsutils::exists("msvcrt.dll")) {
        throw std::runtime_error(
            "You don't have the DLLs for running bots available! ");
      }
    }
  }
  std::string aipath;
  std::string ainame;
  std::string bwapisuffix;
  std::string airace;
};

std::string generateBwapiCommand(AIInfo const& aiinfo) {
  auto bwapicmd = "BWAPILauncher" + aiinfo.bwapisuffix;
  if (!FLAGS_bwapilauncher_directory.empty()) {
    bwapicmd = FLAGS_bwapilauncher_directory + "/" + bwapicmd;
    if (!fsutils::exists(bwapicmd)) {
      auto fallback = fsutils::which("BWAPILauncher" + aiinfo.bwapisuffix);
      LOG(WARNING) << "No such file " << bwapicmd << ". Falling back to "
                   << fallback;
      bwapicmd = std::move(fallback);
    }
  }
  if (fsutils::which(bwapicmd).empty()) {
    throw std::runtime_error(fmt::format(
        "No such executable: {}. Please add BWAPILauncher to the PATH, or "
        "specify its directory with -bwapilauncher_directory",
        bwapicmd));
  }
  return bwapicmd;
}
} // namespace

OpenBwProcess::OpenBwProcess(std::vector<cherrypi::EnvVar> const& vars)
    : OpenBwProcess("", vars) {}

OpenBwProcess::OpenBwProcess(
    std::string bot,
    std::vector<cherrypi::EnvVar> const& vars) {
#ifdef WITHOUT_POSIX
  throw std::runtime_error("OpenBwProcess: Not implemented");
#else // WITHOUT_POSIX
  if (isExiting) {
    throw std::runtime_error("OpenBwProcess: exit in progress");
  }

  AIInfo aiinfo(bot);
  std::string bwapicmd = generateBwapiCommand(aiinfo);
  socketPath_ = fsutils::mktemp("cherrypi-openbwprocess.socket");

  // Set a couple of default variables
  std::vector<cherrypi::EnvVar> env = {
      {"OPENBW_ENABLE_UI", "0", false},
      {"TORCHCRAFT_FILE_SOCKET", socketPath_.c_str(), true},
      {"BWAPI_CONFIG_AUTO_MENU__CHARACTER_NAME", aiinfo.ainame.c_str(), true},
      {"BWAPI_CONFIG_AUTO_MENU__AUTO_MENU", "SINGLE_PLAYER", true},
      {"BWAPI_CONFIG_AUTO_MENU__GAME_TYPE", "USE_MAP_SETTINGS", true},
      {"BWAPI_CONFIG_AUTO_MENU__AUTO_RESTART", "OFF", true},
  };
  // Users shouldn't be able to change these...
  std::vector<cherrypi::EnvVar> postEnv = {
      {"BWAPI_CONFIG_AI__AI", aiinfo.aipath.c_str(), true},
  };
  if (aiinfo.airace.size() != 0) {
    postEnv.push_back(
        {"BWAPI_CONFIG_AUTO_MENU__RACE", aiinfo.airace.c_str(), true});
  }
  env.insert(env.end(), vars.begin(), vars.end());
  env.insert(env.end(), postEnv.begin(), postEnv.end());
  std::tie(fd_, wfd_, pid_) = ForkServer::instance().execute({bwapicmd}, env);

  if (bot == "") {
    running_.store(true);
    goodf_ = goodp_.get_future();
    outputThread_ =
        std::async(std::launch::async, &OpenBwProcess::redirectOutput, this);
  }
#endif // WITHOUT_POSIX
}

OpenBwProcess::~OpenBwProcess() {
#ifndef WITHOUT_POSIX
  running_.store(false);
  // This write should wake up the redirection thread if it's polling
  if (write(wfd_, "\0", 1) < 0) {
    // We don't really care
  }

  // Give the process a bit of time to exit by itself
  auto waitUntil = hires_clock::now() + kDtorGraceTime;
  bool alive = true;
  do {
    if (kill(pid_, 0) != 0 && errno == ESRCH) {
      alive = false;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  } while (hires_clock::now() < waitUntil);

  // Alright, let's not wait forever.
  // Kill the process group (-piwd_) in any case.
  int ret = kill(-pid_, SIGKILL);
  if (ret != 0 && alive) {
    VLOG(1) << "Cannot kill BWAPILauncher(" << pid_
            << "): " << google::StrError(errno);
  }
  if (outputThread_.valid()) {
    outputThread_.wait();
  }

  if (close(wfd_) < 0) {
    // Who cares?
  }
  if (close(fd_) < 0) {
    // Who cares?
  }
  if (socketPath_.size() > 0) {
    fsutils::rmrf(socketPath_);
    int err = errno;
    if (fsutils::exists(socketPath_)) {
      VLOG(0) << "Unable to remove " << socketPath_ << " "
              << google::StrError(err);
    } else {
      VLOG(2) << socketPath_ << " successfully deleted";
    }
  }
#endif // !WITHOUT_POSIX
}

bool OpenBwProcess::connect(torchcraft::Client* client, int timeoutMs) {
  if (goodf_.valid()) {
    VLOG(2) << "Trying to connect to " << socketPath_;
    auto good = [&]() {
      // Make sure we call get() on the future so that exceptions are properly
      // propagated
      if (timeoutMs < 0) {
        goodf_.get();
        return true;
      } else {
        if (goodf_.wait_for(std::chrono::milliseconds(timeoutMs)) !=
            std::future_status::ready) {
          return false;
        }
        goodf_.get();
        return true;
      }
    }();
    if (good) {
      VLOG(2) << "Connected to " << socketPath_;
      return client->connect(socketPath_, timeoutMs);
    }
  }
  return false;
}

/// Reads the BWEnv port and logs all BWAPILauncher output (-v 2)
void OpenBwProcess::redirectOutput() {
#ifndef WITHOUT_POSIX
  common::setCurrentThreadName("redirectOutput");

  std::array<char, 256> buf;
  std::vector<char> linebuf(buf.size());
  size_t lpos = 0;
  bool readSocket = false;

  // Make pipe to BWAPILauncher process non-blocking; use poll() instead.
  int flags = fcntl(fd_, F_GETFL, 0);
  flags |= O_NONBLOCK;
  fcntl(fd_, F_SETFL, flags);

  struct pollfd pfd;
  pfd.fd = fd_;
  pfd.events = POLLIN;
  int numTimedoutPolls = 0;

  while (running_.load()) {
    // Check if child process is still alive
    if (kill(pid_, 0) != 0 && errno == ESRCH) {
      VLOG(1) << "BWAPILauncher(" << pid_ << ") is gone";
      if (!readSocket) {
        goodp_.set_exception(std::make_exception_ptr(std::runtime_error(
            "BWAPILauncher(" + std::to_string(pid_) + ") died prematurely")));
      }
      break;
    }

    // Poll for new data on pipe
    auto pret = poll(&pfd, 1, kPollTimeout);
    if (pret < 0) {
      if (errno == EINTR) {
        VLOG(4) << "Polling was interrupted";
        continue;
      }
      LOG(ERROR) << "Error polling BWAPILauncher pipe: "
                 << google::StrError(errno);
      if (!readSocket) {
        goodp_.set_exception(std::make_exception_ptr(
            std::runtime_error("Error reading BWAPILauncher output")));
      }
      break;
    } else if (pret == 0) {
      VLOG(4) << "Poll timeout";
      if (++numTimedoutPolls >= kMaxTimedoutPolls && !readSocket) {
        goodp_.set_exception(std::make_exception_ptr(
            std::runtime_error("Timeout parsing BWAPILauncher output")));
        break;
      }
      continue;
    } else if (!(pfd.revents & POLLIN)) {
      VLOG(4) << "No data available";
      continue;
    }
    numTimedoutPolls = 0;

    // Process each line individually for convenience
    char sockPath[4096];
    auto readline = [&](char const* line) {
      if (!strncasecmp(line, "Error:", 6)) {
        LOG(ERROR) << "BWAPILauncher(" << pid_ << "): " << line;
      } else {
        VLOG(2) << "BWAPILauncher(" << pid_ << "): " << line;
      }
      if (!readSocket &&
          std::sscanf(
              line, "TorchCraft server listening on socket %s", sockPath) > 0) {
        goodp_.set_value();
        readSocket = true;
      }
    };

    // Read available data
    ssize_t nread = 0;
    while (true) {
      nread = read(fd_, buf.data(), buf.size());
      if (nread <= 0) {
        break;
      }
      ssize_t pos = 0;
      while (pos < nread) {
        if (lpos >= linebuf.size()) {
          linebuf.resize(linebuf.size() + buf.size());
        }
        if (buf[pos] == '\n') {
          linebuf[lpos] = '\0';
          readline(linebuf.data());
          lpos = 0;
          pos++;
        } else {
          linebuf[lpos++] = buf[pos++];
        }
      }
    }

    if (nread < 0 && errno != EAGAIN) {
      LOG(ERROR) << "Error reading from BWAPILauncher pipe: " << errno;
      if (!readSocket) {
        goodp_.set_exception(std::make_exception_ptr(std::system_error(
            errno,
            std::system_category(),
            "Error reading BWAPILauncher output")));
      }
      break;
    } else if (nread == 0) {
      VLOG(2) << "EOF while reading from BWAPILauncher pipe";
      break;
    } else if (readSocket && std::string(sockPath) != socketPath_) {
      goodp_.set_exception(
          std::make_exception_ptr(std::runtime_error(fmt::format(
              "Expected socket path {}, got {}", socketPath_, sockPath))));
      break;
    }
  }
#endif
}

void OpenBwProcess::preventFurtherProcesses() {
  isExiting = true;
}
} // namespace cherrypi
