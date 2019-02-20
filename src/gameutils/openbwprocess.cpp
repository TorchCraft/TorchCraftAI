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
int constexpr kFdR = 0;
int constexpr kFdW = 1;
auto constexpr kQuitCommand = "\0";

struct ForkServerManager;
std::unique_ptr<ForkServerManager> forkServer = nullptr;
std::mutex mutex_;
int forkServerRFd = -1; // Socket to read data from the fork server
int forkServerWFd = -1; // Socket to send data to the fork server
int forkServerSock = -1; // UNIX domain socket to recv file descriptors

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

class EnvironmentBuilder {
 public:
  EnvironmentBuilder(bool copyEnv = true) {
    if (copyEnv) {
      for (char** it = environ; *it != nullptr; it++) {
        auto split = utils::stringSplit(*it, '=', 1);
        environ_[split[0]] = split[1];
      }
    }
  }

  ~EnvironmentBuilder() {
    freeEnv();
  }

  void setenv(
      std::string const& name,
      std::string const& value,
      bool overwrite = false) {
    if (overwrite || environ_.find(name) == environ_.end()) {
      environ_[name] = value;
    }
  }

  char* const* const getEnv() {
    freeEnv();
    env_ = new char*[environ_.size() + 1];
    auto i = 0;
    for (auto& p : environ_) {
      int s = p.first.size() + p.second.size() + 2; // "a" "=" "b" \0
      auto ptr = new char[s];
      if (snprintf(ptr, s, "%s=%s", p.first.c_str(), p.second.c_str()) !=
          s - 1) {
        LOG(FATAL) << "Environment variable is too long";
      }
      env_[i++] = ptr;
    }
    env_[i] = nullptr;
    return env_;
  }

 private:
  std::unordered_map<std::string, std::string> environ_;
  char** env_ = nullptr;

  void freeEnv() {
    if (env_ != nullptr) {
      for (char** it = env_; *it != nullptr; it++) {
        delete[](*it);
      }
      delete[] env_;
    }
  }
};

#ifndef WITHOUT_POSIX
// readFull and writeFull are adapted from Folly to provide a more robust
// readData/writeData functions
inline void incr(ssize_t /* n */) {}
inline void incr(ssize_t n, off_t& offset) {
  offset += off_t(n);
}

template <class F, class... Offset>
ssize_t wrapFull(F f, int fd, void* buf, size_t count, Offset... offset) {
  char* b = static_cast<char*>(buf);
  ssize_t totalBytes = 0;
  ssize_t r;
  do {
    r = f(fd, b, count, offset...);
    if (r == -1) {
      if (errno == EINTR) {
        continue;
      }
      return r;
    }

    totalBytes += r;
    b += r;
    count -= r;
    incr(r, offset...);
  } while (r != 0 && count); // 0 means EOF

  return totalBytes;
}

size_t readFull(int fd, void* buf, size_t count) {
  return wrapFull(read, fd, buf, count);
}

ssize_t writeFull(int fd, const void* buf, size_t count) {
  return wrapFull(write, fd, const_cast<void*>(buf), count);
}

std::string readData(int fd) {
  unsigned int length = 0;
  auto err = readFull(fd, &length, sizeof(length));
  if (err != sizeof(length)) {
    throw std::system_error(errno, std::system_category());
  }

  char buffer[length + 1];
  err = readFull(fd, buffer, length);
  if (err != length) {
    throw std::system_error(errno, std::system_category());
  }
  return std::string(buffer, length);
}

void sendData(int fd, std::string const& data) {
  unsigned int length = data.length();
  auto err = writeFull(fd, &length, sizeof(length));
  if (err != sizeof(length)) {
    throw std::system_error(errno, std::system_category());
  }

  err = writeFull(fd, data.c_str(), length);
  if (err != length) {
    throw std::system_error(errno, std::system_category());
  }
}

// Send/receive fd by unix socket. I have no idea what I'm doing and I'm just
// copying from
// https://stackoverflow.com/questions/28003921/sending-file-descriptor-by-linux-socket
void sendfd(int socket, int fd) {
  struct msghdr msg = {0};
  char buf[CMSG_SPACE(sizeof(fd))];
  memset(buf, '\0', sizeof(buf));

  /* On Mac OS X, the struct iovec is needed, even if it points to minimal data
   */
  char tmp[1]; // Let's allocate one byte for formality
  struct iovec io = {.iov_base = tmp, .iov_len = 1};

  msg.msg_iov = &io;
  msg.msg_iovlen = 1;
  msg.msg_control = buf;
  msg.msg_controllen = sizeof(buf);

  struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(fd));

  memmove(CMSG_DATA(cmsg), &fd, sizeof(fd));

  msg.msg_controllen = cmsg->cmsg_len;

  if (sendmsg(socket, &msg, 0) < 0) {
    LOG(WARNING) << "Failed to send FD: " << errno;
    throw std::system_error(errno, std::system_category());
  }
}

int recvfd(int socket) {
  struct msghdr msg = {0};

  /* On Mac OS X, the struct iovec is needed, even if it points to minimal data
   */
  char m_buffer[1];
  struct iovec io = {.iov_base = m_buffer, .iov_len = sizeof(m_buffer)};
  msg.msg_iov = &io;
  msg.msg_iovlen = 1;

  char c_buffer[256];
  msg.msg_control = c_buffer;
  msg.msg_controllen = sizeof(c_buffer);

  if (recvmsg(socket, &msg, 0) < 0) {
    LOG(WARNING) << "Failed to receive FD: " << errno;
    throw std::system_error(errno, std::system_category());
  }

  struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);

  int fd;
  memmove(&fd, CMSG_DATA(cmsg), sizeof(fd));

  return fd;
}

void checkedPipe(int* p) {
  if (pipe(p) != 0) {
    throw std::system_error(errno, std::system_category());
  }
}

// Spawn a process similar to popen(3) but return its process ID.
//
// This method is adapted from the work of RPGillespie from
// https://stackoverflow.com/a/26852210
// and is used under CC BY-SA: https://creativecommons.org/licenses/by-sa/2.0/
int popen2(
    std::vector<std::string> const& command,
    std::vector<OpenBwProcess::EnvVar> const& env,
    int* infp,
    int* outfp,
    int* outfpw) {
  int p_stdin[2], p_stdout[2];
  pid_t pid;

  // Lookup full path to executable
  if (command.empty()) {
    throw std::runtime_error("No command specified");
  }
  auto exePath = fsutils::which(command[0]);
  if (exePath.empty()) {
    throw std::runtime_error(command[0] + ": command not found");
  }

  // Prepare pipes for input and output redirection
  if (infp != nullptr) {
    checkedPipe(p_stdin);
  } else {
    p_stdin[0] = -1;
    p_stdin[1] = -1;
  }
  checkedPipe(p_stdout);

  // Prepare full input arguments
  std::stringstream ss;
  auto argv = std::vector<char*>(command.size() + 1, nullptr);
  auto exeBaseName = fsutils::basename(command[0]);
  argv[0] = const_cast<char*>(exeBaseName.c_str());
  for (auto i = 1U; i < command.size(); i++) {
    argv[i] = const_cast<char*>(command[i].c_str());
  }

  // Prepare environment
  auto builder = EnvironmentBuilder();
  for (auto& var : env) {
    builder.setenv(var.key, var.value, var.overwrite);
  }
  auto newEnviron = builder.getEnv();
  if (VLOG_IS_ON(4)) {
    std::stringstream debugstr;
    for (auto it = newEnviron; *it != nullptr; it++) {
      debugstr << *it << std::endl;
    }
    debugstr << command;
    VLOG(4) << debugstr.str();
  }

  pid = fork();
  if (pid < 0) {
    // Failed
    auto forkErrno = errno;
    if (infp != nullptr) {
      close(p_stdin[0]);
      close(p_stdin[1]);
    }
    close(p_stdout[0]);
    close(p_stdout[1]);
    throw std::system_error(forkErrno, std::system_category());
  } else if (pid == 0) {
    // Redirect stdin and stdout
    if (infp != nullptr) {
      close(0);
      close(p_stdin[1]);
      dup2(p_stdin[0], 0);
    }
    close(1);
    close(p_stdout[0]);
    dup2(p_stdout[1], 1);

    // Set process group ID to PID so that we can easily kill this process and
    // all its children later.
    setpgid(0, 0);

    execve(exePath.c_str(), argv.data(), newEnviron);
    perror("execve");
    exit(1);
  }

  // Close unused ends of pipes
  if (infp != nullptr) {
    *infp = p_stdin[1];
    close(p_stdin[0]);
  }

  if (outfp == nullptr) {
    close(p_stdout[0]);
  } else {
    *outfp = p_stdout[0];
  }
  if (outfpw == nullptr) {
    close(p_stdout[1]);
  } else {
    *outfpw = p_stdout[1];
  }

  return pid;
}

// returns fd_, wfd_, pid_, socketPath_
std::tuple<int, int, int, std::string> forkOpenBw(
    std::string bot,
    std::vector<OpenBwProcess::EnvVar> const& vars) {
  std::string aipath;
  std::string ainame;
  std::string airace;
  std::string bwapisuffix;
  std::string socketPath;
  int fd, wfd, pid;
  if (bot.empty()) {
    auto bwenvPath = getenv("BWENV_PATH");
    if (bwenvPath != nullptr) {
      aipath = bwenvPath;
    } else {
#ifdef __APPLE__
      aipath = "build/3rdparty/torchcraft/BWEnv/BWEnv.dylib";
#else
      aipath = "build/3rdparty/torchcraft/BWEnv/BWEnv.so";
#endif
    }
    ainame = "BWEnv";

    socketPath = fsutils::mktemp("cherrypi-openbwprocess.socket");

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

  // Set a couple of default variables
  std::vector<OpenBwProcess::EnvVar> env = {
      {"OPENBW_ENABLE_UI", "0", false},
      {"TORCHCRAFT_FILE_SOCKET", socketPath.c_str(), true},
      {"BWAPI_CONFIG_AUTO_MENU__CHARACTER_NAME", ainame.c_str(), true},
      {"BWAPI_CONFIG_AUTO_MENU__AUTO_MENU", "SINGLE_PLAYER", true},
      {"BWAPI_CONFIG_AUTO_MENU__GAME_TYPE", "USE_MAP_SETTINGS", true},
      {"BWAPI_CONFIG_AUTO_MENU__AUTO_RESTART", "OFF", true},
  };
  // Users shouldn't be able to change these...
  std::vector<OpenBwProcess::EnvVar> postEnv = {
      {"BWAPI_CONFIG_AI__AI", aipath.c_str(), true},
  };
  if (airace.size() != 0) {
    postEnv.push_back({"BWAPI_CONFIG_AUTO_MENU__RACE", airace.c_str(), true});
  }
  env.insert(env.end(), vars.begin(), vars.end());
  env.insert(env.end(), postEnv.begin(), postEnv.end());

  // Launch OpenBW via BWAPILauncher
  auto bwapicmd = "BWAPILauncher" + bwapisuffix;
  if (!FLAGS_bwapilauncher_directory.empty()) {
    bwapicmd = FLAGS_bwapilauncher_directory + "/" + bwapicmd;
    if (!fsutils::exists(bwapicmd)) {
      auto fallback = fsutils::which("BWAPILauncher" + bwapisuffix);
      LOG(WARNING) << "No such file " << bwapicmd << ". Falling back to "
                   << fallback;
      bwapicmd = std::move(fallback);
    }
  }
  pid = popen2({bwapicmd}, env, nullptr, &fd, &wfd);
  return std::make_tuple(fd, wfd, pid, socketPath);
}
#endif // WITHOUT_POSIX

struct ForkServerManager {
  ~ForkServerManager() {
#ifndef WITHOUT_POSIX
    sendData(forkServerWFd, kQuitCommand);
#endif // WITHOUT_POSIX
  }
};
} // namespace

OpenBwProcess::OpenBwProcess(std::vector<EnvVar> const& vars)
    : OpenBwProcess("", vars) {}

OpenBwProcess::OpenBwProcess(std::string bot, std::vector<EnvVar> const& vars) {
#ifdef WITHOUT_POSIX
  throw std::runtime_error("OpenBwProcess: Not implemented");
#else // WITHOUT_POSIX
  std::lock_guard<std::mutex> lock(mutex_);
  if (forkServer) {
    // Send data to fork server
    std::stringstream ss;
    {
      cereal::BinaryOutputArchive ar(ss);
      std::vector<EnvVar> copy(vars);
      ar(bot, copy);
    }
    VLOG(4) << "OpenBwProcessClient: Sending arguments to server";
    sendData(forkServerWFd, ss.str());

    // Receive fd_, wfd_, pid_
    VLOG(4) << "OpenBwProcessClient: Receiving arguments from server";
    auto result = readData(forkServerRFd);
    std::stringstream iss(result);
    {
      cereal::BinaryInputArchive ar(iss);
      ar(pid_, socketPath_);
    }
    if (pid_ >= 0) {
      fd_ = recvfd(forkServerSock);
      wfd_ = recvfd(forkServerSock);
      VLOG(2) << fmt::format(
          "OpenBwProcessClient: Received: fd({}) wfd({}) pid({}) "
          "socketPath({})",
          fd_,
          wfd_,
          pid_,
          socketPath_);
    } else {
      // In the case of an error, socketPath_ is actually an error message
      throw std::runtime_error("Failed to spawn process: " + socketPath_);
    }
    launchedWithForkServer_ = true;
  } else {
    std::tie(fd_, wfd_, pid_, socketPath_) = forkOpenBw(bot, vars);
    launchedWithForkServer_ = false;
  }

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

void OpenBwProcess::startForkServer() {
#ifdef WITHOUT_POSIX
  throw std::runtime_error("OpenBwProcess: Not implemented");
#else // WITHOUT_POSIX
  std::lock_guard<std::mutex> lock(mutex_);

  int processToServerFd[2], serverToProcessFd[2], domainSocket[2];
  checkedPipe(processToServerFd);
  checkedPipe(serverToProcessFd);
  if (socketpair(AF_UNIX, SOCK_DGRAM, 0, domainSocket) != 0) {
    LOG(ERROR) << "Failed to create Unix-domain socket pair";
  }

  pid_t forkServerPid;
  if ((forkServerPid = fork()) == -1) {
    LOG(ERROR) << "Unable to start OpenBwProcess forkserver";
  }

  if (forkServerPid == 0) {
    // Server process (child)
    close(serverToProcessFd[kFdR]);
    close(processToServerFd[kFdW]);
    close(domainSocket[1]);
    int sock = domainSocket[0];
    int rfd = processToServerFd[kFdR];
    int wfd = serverToProcessFd[kFdW];

    cherrypi::installSignalHandlers();
    while (true) {
      // Receive some arguments
      std::string bot;
      std::vector<EnvVar> vars;
      auto data = readData(rfd);
      if (data == kQuitCommand) {
        close(sock);
        close(rfd);
        close(wfd);
        exit(EXIT_SUCCESS);
      }
      std::stringstream iss(data);
      {
        cereal::BinaryInputArchive ar(iss);
        ar(bot, vars);
      }

      bool forkOk = true;
      int fd_ = -1, wfd_ = -1, pid_ = -1;
      std::string socketPath;
      std::string forkError;

      try {
        std::tie(fd_, wfd_, pid_, socketPath) = forkOpenBw(bot, vars);
      } catch (std::exception const& e) {
        LOG(WARNING) << "Exception in fork server: " << e.what();
        // Clean up and send back data indicating failure (pid < 0)
        pid_ = -1;
        fsutils::rmrf(socketPath);
        forkError = e.what();
        forkOk = false;
      }

      // Send reply
      if (forkOk) {
        VLOG(4) << fmt::format(
            "Fork server: sending back fd: {} wfd: {} pid: {} socketPath: {}",
            fd_,
            wfd_,
            pid_,
            socketPath);
      } else {
        VLOG(4) << fmt::format(
            "Fork server: sending back error notice: pid: {} error: {}",
            pid_,
            forkError);
      }

      std::stringstream oss;
      {
        cereal::BinaryOutputArchive ar(oss);
        ar(pid_, forkOk ? socketPath : forkError);
      }
      sendData(wfd, oss.str());
      if (forkOk) {
        // Send file descriptors if fork was successful
        sendfd(sock, fd_);
        sendfd(sock, wfd_);
        close(fd_);
        close(wfd_);
      }
    }
  } else {
    // Mark successful server startup
    forkServer = std::make_unique<ForkServerManager>();

    close(serverToProcessFd[kFdW]);
    close(processToServerFd[kFdR]);
    close(domainSocket[0]);
    forkServerRFd = serverToProcessFd[kFdR];
    forkServerWFd = processToServerFd[kFdW];
    forkServerSock = domainSocket[1];
  }
#endif // WITHOUT_POSIX
}

void OpenBwProcess::endForkServer() {
#ifdef WITHOUT_POSIX
  throw std::runtime_error("OpenBwProcess: Not implemented");
#else // WITHOUT_POSIX
  std::lock_guard<std::mutex> lock(mutex_);
  forkServer.reset(nullptr);
#endif // WITHOUT_POSIX
}
} // namespace cherrypi
