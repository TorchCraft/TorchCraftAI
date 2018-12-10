/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "playscript.h"

#include <torchcraft/client.h>

#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <glog/logging.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstring>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>

#ifndef WITHOUT_POSIX
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif // WITHOUT_POSIX

extern char** environ;

namespace cherrypi {

#ifndef WITHOUT_POSIX
namespace {
void checkedPipe(int* p) {
  if (pipe(p) != 0) {
    LOG(ERROR) << "pipe failed with error " << errno;
    throw std::system_error(errno, std::system_category());
  }
}
}
#endif // WITHOUT_POSIX

PlayScript::PlayScript(std::vector<EnvVar> const& vars, std::string script) {
#ifdef WITHOUT_POSIX
  throw std::runtime_error("PlayScript: Not implemented");
#else // WITHOUT_POSIX
  // pipe to signal that the script is ready to start a game
  std::array<int, 2> waitReadyPipe{};
  checkedPipe(waitReadyPipe.data());

  // pipe the script waits on before starting the game
  std::array<int, 2> waitPipe{};
  checkedPipe(waitPipe.data());

  // pipe through which we grab the tc port
  std::array<int, 2> readPipe{};
  checkedPipe(readPipe.data());

  // pipe which will be closed when the script exits
  std::array<int, 2> scriptTermPipe{};
  checkedPipe(scriptTermPipe.data());

  // Run the script
  scriptPid_ = ForkServer::instance().fork(
      [](std::vector<EnvVar> const& vars,
         std::string script,
         int readPipeFd,
         int waitReadyPipeFd,
         int waitPipeFd,
         int termPipeFd) {
        (void)termPipeFd;

        setpgid(0, 0);

        EnvironmentBuilder builder;
        for (auto& var : vars) {
          builder.setenv(var.key, var.value, var.overwrite);
        }
        builder.setenv("GAMES", "16777216", true);
        builder.setenv("CPI", "echo", true);
        builder.setenv(
            "CPI_OUTPUT", "/dev/fd/" + std::to_string(readPipeFd), true);
        builder.setenv(
            "PRE_GAME",
            "echo > /dev/fd/" + std::to_string(waitReadyPipeFd) +
                "; read -N 1 < /dev/fd/" + std::to_string(waitPipeFd) +
                " || die pipe dead",
            true);

        std::array<const char*, 2> argv{};
        argv[0] = script.c_str();

        execve(script.c_str(), (char**)argv.data(), builder.getEnv());
        perror(script.c_str());
        std::_Exit(1);
      },
      vars,
      script,
      FileDescriptor(readPipe[1]),
      FileDescriptor(waitReadyPipe[1]),
      FileDescriptor(waitPipe[0]),
      FileDescriptor(scriptTermPipe[1]));

  close(scriptTermPipe[1]);

  // pipe which will be closed when we exit
  std::array<int, 2> parentTermPipe{};
  checkedPipe(parentTermPipe.data());
  // Fork a small program to ensure we kill the script if we exit without
  // cleaning up
  termPid_ = ForkServer::instance().fork(
      [](int pid, int parentTermPipeFd, int scriptTermPipeFd) {
        static int spid = pid;
        static bool squit = false;
        auto termHandler = [](int signal) {
          auto savedErrno = errno;
          kill(-spid, signal);
          kill(spid, signal);
          squit = true;
          errno = savedErrno;
        };
        std::signal(SIGTERM, termHandler);
        std::signal(SIGINT, termHandler);

        std::array<pollfd, 2> pfd;
        pfd[0].fd = parentTermPipeFd;
        pfd[0].events = POLLIN;
        pfd[1].fd = scriptTermPipeFd;
        pfd[1].events = POLLIN;
        auto pret = poll(pfd.data(), 2, 60000);
        while ((pret == 0 || (pret == -1 && errno == EINTR)) && !squit) {
          pret = poll(pfd.data(), 2, 60000);
        }

        termHandler(SIGTERM);
        std::_Exit(0);
      },
      scriptPid_,
      FileDescriptor(parentTermPipe[0]),
      FileDescriptor(scriptTermPipe[0]));

  close(parentTermPipe[0]);
  close(scriptTermPipe[0]);
  termPipeFd_ = parentTermPipe[1];

  close(waitReadyPipe[1]);
  close(waitPipe[0]);
  close(readPipe[1]);

  waitReadyPipeFd_ = waitReadyPipe[0];
  waitPipeFd_ = waitPipe[1];
  readPipeFd_ = readPipe[0];
#endif // WITHOUT_POSIX
}

PlayScript::~PlayScript() {
#ifndef WITHOUT_POSIX
  kill(-scriptPid_, SIGTERM);
  kill(scriptPid_, SIGTERM);
  kill(termPid_, SIGTERM);
  close(waitPipeFd_);
  close(termPipeFd_);
  ForkServer::instance().waitpid(scriptPid_);
  ForkServer::instance().waitpid(termPid_);
#endif // WITHOUT_POSIX
}

bool PlayScript::connect(torchcraft::Client* client, int timeoutMs) {
#ifdef WITHOUT_POSIX
  throw std::runtime_error("PlayScript: Not implemented");
  return false;
#else // WITHOUT_POSIX
  if (nConnects_ == 0 && timeoutMs >= 0 && timeoutMs < 10000) {
    timeoutMs = 10000;
  }

  auto start = std::chrono::steady_clock::now();
  auto getTimeout = [&]() {
    if (timeoutMs <= 0) {
      return timeoutMs;
    }
    int r = timeoutMs -
        (int)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start)
            .count();
    return r < 0 ? 0 : r;
  };

  struct pollfd pfd;
  pfd.fd = waitReadyPipeFd_;
  pfd.events = POLLIN;

  // Wait until the script is ready
  auto pret = poll(&pfd, 1, getTimeout());
  if (pret == 0) {
    VLOG(2) << "connect: timed out on initial connection";
    return false;
  }
  while (pret < 0 && errno == EINTR) {
    pret = poll(&pfd, 1, getTimeout());
  }
  if (pret == -1) {
    PLOG(ERROR) << "poll failed";
    return false;
  }
  std::array<char, 0x1000> buf;
  int r;
  while ((r = read(waitReadyPipeFd_, buf.data(), 1)) != 1) {
    if (r == -1 && errno == EINTR) {
      continue;
    }
    if (r == 0) {
      LOG(WARNING) << "pipe: EOF";
      return false;
    }
    PLOG(ERROR) << "failed to read from pipe";
    return false;
  }

  // Tell the script to start the game
  buf[0] = '\n';
  while ((r = write(waitPipeFd_, buf.data(), 1)) != 1) {
    if (r == -1 && errno == EINTR) {
      continue;
    }
    if (r == 0) {
      LOG(WARNING) << "pipe: EOF";
      return false;
    }
    PLOG(ERROR) << "Failed to write to pipe";
    return false;
  }

  // Grab the tc port
  int port = 0;
  char* bufPtr = buf.data();
  const char* bufEnd = bufPtr + buf.size() - 1;
  while (bufPtr != bufEnd) {
    pfd.fd = readPipeFd_;
    pfd.events = POLLIN;
    pret = poll(&pfd, 1, getTimeout());
    if (pret == 0) {
      VLOG(2) << "connect: timed out while receiving data";
      return false;
    }
    while (pret == -1 && errno == EINTR) {
      pret = poll(&pfd, 1, getTimeout());
    }
    r = read(readPipeFd_, bufPtr, bufEnd - bufPtr);
    if (r == -1) {
      if (errno == EINTR) {
        continue;
      } else {
        PLOG(ERROR) << "pipe read failed";
        return false;
      }
    }
    if (r == 0) {
      LOG(WARNING) << "pipe: EOF";
      return false;
    }
    bufPtr += r;
    if (std::find(bufPtr - r, bufPtr, '\n') != bufPtr || bufPtr == bufEnd) {
      *bufPtr = 0;
      const char* substr = "-port ";
      const char* ptr = std::strstr(buf.data(), substr);
      if (ptr) {
        port = std::atoi(ptr + std::strlen(substr));
        break;
      }
      bufPtr = buf.data();
    }
  }

  if (port == 0) {
    LOG(ERROR) << "Failed to get TorchCraft port";
    return false;
  }

  return client->connect("127.0.0.1", port, getTimeout());
#endif // WITHOUT_POSIX
}
}
