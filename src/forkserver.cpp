/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "forkserver.h"
#include "utils.h"

#include <common/fsutils.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <vector>

#ifndef WITHOUT_POSIX
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif // WITHOUT_POSIX

#ifdef __linux__
#include <linux/prctl.h>
#include <sys/prctl.h>
#endif

#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <fmt/format.h>
#include <glog/logging.h>
#include <prettyprint/prettyprint.hpp>

namespace fsutils = common::fsutils;

extern char** environ;

namespace cherrypi {

namespace {

int constexpr kFdR = 0;
int constexpr kFdW = 1;
char constexpr kQuitCommand = 'Q';
char constexpr kForkCommand = 'F';
char constexpr kExecuteCommand = 'X';
char constexpr kWaitPidCommand = 'W';
} // namespace

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

ssize_t readFull(int fd, void* buf, size_t count) {
  return wrapFull(read, fd, buf, count);
}

ssize_t writeFull(int fd, const void* buf, size_t count) {
  return wrapFull(write, fd, const_cast<void*>(buf), count);
}

std::string readData(int fd) {
  unsigned int length = 0;
  auto err = readFull(fd, &length, sizeof(length));
  if (err != sizeof(length)) {
    LOG(ERROR) << "readFull failed with error " << errno;
    throw std::system_error(errno, std::system_category());
  }

  char buffer[length + 1];
  err = readFull(fd, buffer, length);
  if (err != length) {
    LOG(ERROR) << "readFull failed with error " << errno;
    throw std::system_error(errno, std::system_category());
  }
  return std::string(buffer, length);
}

void sendData(int fd, std::string const& data) {
  unsigned int length = data.length();
  auto err = writeFull(fd, &length, sizeof(length));
  if (err != sizeof(length)) {
    LOG(ERROR) << "writeFull failed with error " << errno;
    throw std::system_error(errno, std::system_category());
  }

  err = writeFull(fd, data.c_str(), length);
  if (err != length) {
    LOG(ERROR) << "writeFull failed with error " << errno;
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
    LOG(ERROR) << "pipe failed with error " << errno;
    throw std::system_error(
        errno, std::system_category(), "ForkServer: pipe failed");
  }
}

// Spawn a process similar to popen(3) but return its process ID.
//
// This method is adapted from the work of RPGillespie from
// https://stackoverflow.com/a/26852210
// and is used under CC BY-SA: https://creativecommons.org/licenses/by-sa/2.0/
int popen2(
    std::vector<std::string> const& command,
    std::vector<EnvVar> const& env,
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

#ifdef __linux__
  pid_t ppid_before_fork = getpid();
#endif
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
    LOG(ERROR) << "fork failed with error " << errno;
    throw std::system_error(
        forkErrno, std::system_category(), "ForkServer: fork() failed");
  } else if (pid == 0) {
    // Children
#ifdef __linux__
    // This ensures that child processes die when the parent dies
    // See https://stackoverflow.com/a/36945270 for explanation
    int r = prctl(PR_SET_PDEATHSIG, SIGHUP);
    if (r == -1) {
      perror(0);
      exit(1);
    }
    // test in case the original parent exited just
    // before the prctl() call
    if (getppid() != ppid_before_fork) {
      exit(1);
    }
#endif

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
#endif // WITHOUT_POSIX

// returns fd_, wfd_, pid_
std::tuple<int, int, int> ForkServer::execute(
    std::vector<std::string> const& command,
    std::vector<EnvVar> const& env) {
#ifdef WITHOUT_POSIX
  throw std::runtime_error("ForkServer: Not implemented");
#else // WITHOUT_POSIX
  std::lock_guard<std::mutex> lock(mutex_);
  // Send data to fork server
  std::stringstream ss;
  {
    cereal::BinaryOutputArchive ar(ss);
    ar(kExecuteCommand, command, env);
  }
  VLOG(4) << "ForkServer execute: Sending arguments to server";
  sendData(forkServerWFd_, ss.str());

  // Receive fd_, wfd_, pid_
  VLOG(4) << "ForkServer execute: Receiving arguments from server";
  int fd = recvfd(forkServerSock_);
  int wfd = recvfd(forkServerSock_);
  auto result = readData(forkServerRFd_);
  int pid;
  std::stringstream iss(result);
  {
    cereal::BinaryInputArchive ar(iss);
    ar(pid);
  }
  VLOG(2) << fmt::format(
      "ForkServer client: Received: fd({}) wfd({}) pid({})", fd, wfd, pid);

  return std::make_tuple(fd, wfd, pid);
#endif // WITHOUT_POSIX
}

int ForkServer::waitpid(int pid) {
#ifdef WITHOUT_POSIX
  throw std::runtime_error("ForkServer: Not implemented");
  return 0;
#else // WITHOUT_POSIX
  std::lock_guard<std::mutex> lock(mutex_);
  std::stringstream ss;
  {
    cereal::BinaryOutputArchive ar(ss);
    ar(kWaitPidCommand, pid);
  }
  VLOG(4) << "ForkServer waitpid: Sending arguments to server";
  sendData(forkServerWFd_, ss.str());
  VLOG(4) << "ForkServer waitpid: Receiving arguments from server";
  auto result = readData(forkServerRFd_);
  std::stringstream iss(result);
  {
    cereal::BinaryInputArchive ar(iss);
    ar(pid);
  }
  VLOG(2) << fmt::format("ForkServer waitpid: Received: pid({})", pid);
  return pid;
#endif // WITHOUT_POSIX
}

void ForkServer::sendfd(int sock, int fd) {
#ifdef WITHOUT_POSIX
  throw std::runtime_error("ForkServer: Not implemented");
#else // WITHOUT_POSIX
  cherrypi::sendfd(sock, fd);
#endif // WITHOUT_POSIX
}

int ForkServer::recvfd(int sock) {
#ifdef WITHOUT_POSIX
  throw std::runtime_error("ForkServer: Not implemented");
  return 0;
#else // WITHOUT_POSIX
  return cherrypi::recvfd(sock);
#endif // WITHOUT_POSIX
}

int ForkServer::forkSendCommand(const std::string& data) {
#ifdef WITHOUT_POSIX
  throw std::runtime_error("ForkServer: Not implemented");
  return 0;
#else // WITHOUT_POSIX
  std::stringstream ss;
  {
    cereal::BinaryOutputArchive ar(ss);
    ar(kForkCommand);
  }
  VLOG(4) << "ForkServer fork: Sending arguments to server";
  sendData(forkServerWFd_, ss.str() + data);
  VLOG(4) << "ForkServer fork: Receiving arguments from server";
  int pid;
  auto result = readData(forkServerRFd_);
  std::stringstream iss(result);
  {
    cereal::BinaryInputArchive ar(iss);
    ar(pid);
  }
  VLOG(2) << fmt::format("ForkServer fork: Received: pid({})", pid);
  return pid;
#endif // WITHOUT_POSIX
}

namespace {
std::atomic<int> threadCounter;
thread_local int tlThreadCounter = (++threadCounter, 42);

#ifndef WITHOUT_POSIX
void serverProcess(int sock, int rfd, int wfd) {
  while (true) {
    // Receive some arguments
    auto data = readData(rfd);
    std::vector<std::string> command;
    std::vector<EnvVar> environment;
    std::stringstream iss(data);
    cereal::BinaryInputArchive iar(iss);
    char cmd;
    iar(cmd);
    if (cmd == kQuitCommand) {
      close(sock);
      close(rfd);
      close(wfd);
      exit(EXIT_SUCCESS);
    } else if (cmd == kExecuteCommand) {
      iar(command, environment);
      int processFd, processWfd, pid;

      pid = popen2(command, environment, nullptr, &processFd, &processWfd);
      if (pid < 0) {
        throw std::runtime_error("Failed to spawn process");
      }

      // Send the file descriptors back
      std::stringstream oss;
      {
        cereal::BinaryOutputArchive ar(oss);
        ar(pid);
      }
      VLOG(4) << fmt::format(
          "Server is sending back fd: {} wfd: {} pid: {}",
          processFd,
          processWfd,
          pid);
      sendData(wfd, oss.str());
      sendfd(sock, processFd);
      sendfd(sock, processWfd);
      close(processFd);
      close(processWfd);
    } else if (cmd == kForkCommand) {
      std::vector<int> (*ptrReadFds)(int);
      iar.loadBinary(&ptrReadFds, sizeof(ptrReadFds));
      auto fds = ptrReadFds(sock);
      int pid = ::fork();
      if (pid < 0) {
        throw std::runtime_error(
            "fork failed with error " + std::to_string(errno));
      }
      if (pid == 0) {
        close(wfd);
        close(rfd);
        void (*ptr)(cereal::BinaryInputArchive&, const std::vector<int>&);
        iar.loadBinary(&ptr, sizeof(ptr));
        ptr(iar, fds);
        std::_Exit(0);
      }
      for (int fd : fds) {
        close(fd);
      }
      std::stringstream oss;
      {
        cereal::BinaryOutputArchive ar(oss);
        ar(pid);
      }
      VLOG(4) << fmt::format("Server is sending back pid: {}", pid);
      sendData(wfd, oss.str());
    } else if (cmd == kWaitPidCommand) {
      int pid;
      iar(pid);
      pid = ::waitpid(pid, nullptr, 0);
      while (pid == -1 && errno == EINTR) {
        pid = ::waitpid(pid, nullptr, 0);
      }
      std::stringstream oss;
      {
        cereal::BinaryOutputArchive ar(oss);
        ar(pid);
      }
      VLOG(4) << fmt::format("Server is sending back pid: {}", pid);
      sendData(wfd, oss.str());
    } else {
      throw std::runtime_error("ForkServer: unknown command");
    }
  }
}
#endif
} // namespace

ForkServer::ForkServer() {
  (void)tlThreadCounter;
  if (threadCounter != 1) {
    LOG(FATAL) << "ForkServer must be started before any threads are created! ("
               << threadCounter << " threads have been created)";
  }
#ifdef WITHOUT_POSIX
  throw std::runtime_error("ForkServer: Not implemented");
#else // WITHOUT_POSIX

  int processToServerFd[2], serverToProcessFd[2], domainSocket[2];
  checkedPipe(processToServerFd);
  checkedPipe(serverToProcessFd);
  if (socketpair(AF_UNIX, SOCK_DGRAM, 0, domainSocket) != 0) {
    LOG(FATAL) << "Failed to create Unix-domain socket pair";
  }

  pid_t forkServerPid;

  if ((forkServerPid = ::fork()) == -1) {
    LOG(FATAL) << "Unable to start forkserver";
  }

  // Server process (child)
  if (forkServerPid == 0) {
    close(serverToProcessFd[kFdR]);
    close(processToServerFd[kFdW]);
    close(domainSocket[1]);
    int sock = domainSocket[0];
    int rfd = processToServerFd[kFdR];
    int wfd = serverToProcessFd[kFdW];
    serverProcess(sock, rfd, wfd);
  } else {
    close(serverToProcessFd[kFdW]);
    close(processToServerFd[kFdR]);
    close(domainSocket[0]);
    forkServerRFd_ = serverToProcessFd[kFdR];
    forkServerWFd_ = processToServerFd[kFdW];
    forkServerSock_ = domainSocket[1];
  }
#endif // WITHOUT_POSIX
}

ForkServer::~ForkServer() {
#ifndef WITHOUT_POSIX
  std::stringstream ss;
  cereal::BinaryOutputArchive ar(ss);
  ar(kQuitCommand);
  sendData(forkServerWFd_, ss.str());
#endif // WITHOUT_POSIX
}

namespace {
std::unique_ptr<ForkServer> globalForkServer;
}

ForkServer& ForkServer::instance() {
  if (!globalForkServer) {
    LOG(FATAL) << "You must call ForkServer::startForkServer! Call it as early "
                  "as possible (in main, after parsing command line flags, but "
                  "before initializing gloo/mpi/anything else)!";
  }
  return *globalForkServer;
}

void ForkServer::startForkServer() {
  globalForkServer = std::make_unique<ForkServer>();
}

void ForkServer::endForkServer() {
  globalForkServer.reset();
}

EnvironmentBuilder::EnvironmentBuilder(bool copyEnv) {
  if (copyEnv) {
    for (char** it = environ; *it != nullptr; it++) {
      auto split = utils::stringSplit(*it, '=', 1);
      environ_[split[0]] = split[1];
    }
  }
}

EnvironmentBuilder::~EnvironmentBuilder() {
  freeEnv();
}

void EnvironmentBuilder::setenv(
    const std::string& name,
    const std::string& value,
    bool overwrite) {
  if (overwrite || environ_.find(name) == environ_.end()) {
    environ_[name] = value;
  }
}

char* const* const EnvironmentBuilder::getEnv() {
  freeEnv();
  env_ = new char*[environ_.size() + 1];
  auto i = 0;
  for (auto& p : environ_) {
    int s = p.first.size() + p.second.size() + 2; // "a" "=" "b" \0
    auto ptr = new char[s];
    if (snprintf(ptr, s, "%s=%s", p.first.c_str(), p.second.c_str()) != s - 1) {
      LOG(FATAL) << "Environment variable is too long";
    }
    env_[i++] = ptr;
  }
  env_[i] = nullptr;
  return env_;
}

void EnvironmentBuilder::freeEnv() {
  if (env_ != nullptr) {
    for (char** it = env_; *it != nullptr; it++) {
      delete[](*it);
    }
    delete[] env_;
  }
}
} // namespace cherrypi
