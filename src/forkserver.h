/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cstdlib>
#include <mutex>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <cereal/archives/binary.hpp>

namespace cherrypi {

struct EnvVar {
  std::string key;
  std::string value;
  bool overwrite = false;
  template <class Archive>
  void serialize(Archive& archive) {
    archive(key, value, overwrite);
  }
};

class EnvironmentBuilder {
 public:
  EnvironmentBuilder(bool copyEnv = true);
  ~EnvironmentBuilder();

  void setenv(
      std::string const& name,
      std::string const& value,
      bool overwrite = false);
  char* const* const getEnv();

 private:
  std::unordered_map<std::string, std::string> environ_;
  char** env_ = nullptr;

  void freeEnv();
};

/**
 * File descriptors passed as arguments to ForkServer::fork *must* be wrapped
 * in this class. The actual file descriptor number will not be the same in
 * the forked process.
 */
class FileDescriptor {
 public:
  FileDescriptor(int fd) : fd(fd) {}
  operator int() {
    return fd;
  }

 private:
  int fd;
};

/**
 * This class lets us fork when using MPI.
 * You must call ForkServer::startForkServer() before mpi/gloo is initialized,
 * and before any threads are created. The best place to call it is in main,
 * after parsing command line arguments, and before cherrypi::init().
 * Example usage:
 *
 * ForkServer::startForkServer();
 * int pid = ForkServer::instance().fork([](std::string str) {
 *   VLOG(0) << "This is a new process with message: " << str;
 * }, std::string("hello world"));
 * ForkServer::instance().waitpid(pid);
 *
 */
class ForkServer {
 public:
  ForkServer();
  ~ForkServer();

  static ForkServer& instance();
  static void startForkServer();
  static void endForkServer();

  /// Execute command with environment.
  /// returns rfd, wfd, pid, where rfd and wfd is the read and write descriptor
  /// to stdout of the new process.
  std::tuple<int, int, int> execute(
      std::vector<std::string> const& command,
      std::vector<EnvVar> const& env);

  /// fork and call f with the specified arguments. f must be trivially
  /// copyable, args must be cereal serializable.
  /// You should not pass any pointers or references (either through
  /// argument or lambda capture), except to globals, since they will
  /// not be valid in the new process.
  /// There are no restrictions on what code can be executed in the function,
  /// but keep in mind that it runs in a new process with a single thread, as-if
  /// running from the point in the program where startForkServer was called.
  /// It is highly recommended to call waitpid at some point with the returned
  /// pid, because linux requires it in order to reap children and avoid a
  /// defunct process for every fork.
  /// returns pid
  template <typename F, typename... Args>
  int fork(F&& f, Args&&... args) {
    static_assert(
        std::is_trivially_copyable<F>::value, "f must be trivially copyable!");
    std::lock_guard<std::mutex> lock(mutex_);
    std::stringstream oss;
    cereal::BinaryOutputArchive ar(oss);
    std::vector<int> (*ptrReadFds)(int sock) = &forkReadFds<F, Args...>;
    ar.saveBinary(&ptrReadFds, sizeof(ptrReadFds));
    void (*ptr)(cereal::BinaryInputArchive & ar, const std::vector<int>& fds) =
        &forkEntry<F, Args...>;
    ar.saveBinary(&ptr, sizeof(ptr));
    ar.saveBinary(&f, sizeof(f));
    forkSerialize(ar, std::forward<Args>(args)...);
    return forkSendCommand(oss.str());
  }

  // Blocks and waits until pid exits. Linux will not release process resources
  // until either this is called or the parent exits.
  int waitpid(int pid);

 private:
  std::mutex mutex_;
  int forkServerRFd_ = -1; // Socket to read data from the fork server
  int forkServerWFd_ = -1; // Socket to send data to the fork server
  int forkServerSock_ = -1; // UNIX domain socket to recv file descriptors

  static void sendfd(int sock, int fd);
  static int recvfd(int sock);

  int forkSendCommand(const std::string& data);

  template <typename T>
  void forkSerialize(T& ar) {}
  template <typename T, typename A, typename... Args>
  void forkSerialize(T& ar, A&& a, Args&&... args) {
    ar(std::forward<A>(a));
    forkSerialize(ar, std::forward<Args>(args)...);
  }
  template <typename T, typename... Args>
  void forkSerialize(T& ar, FileDescriptor fd, Args&&... args) {
    sendfd(forkServerSock_, (int)fd);
    forkSerialize(ar, std::forward<Args>(args)...);
  }

  template <typename T>
  struct typeResolver {};

  template <typename T>
  static T forkDeserialize(
      typeResolver<T>,
      cereal::BinaryInputArchive& ar,
      const std::vector<int>& fds,
      size_t& fdsIndex) {
    T r;
    ar(r);
    return r;
  }
  static int forkDeserialize(
      typeResolver<FileDescriptor>,
      cereal::BinaryInputArchive& ar,
      const std::vector<int>& fds,
      size_t& fdsIndex) {
    return fds.at(fdsIndex++);
  }

  template <typename T>
  static int
  forkDeserializeFds(typeResolver<T>, int sock, std::vector<int>& result) {
    return 0;
  }
  static int forkDeserializeFds(
      typeResolver<FileDescriptor>,
      int sock,
      std::vector<int>& result) {
    result.push_back(recvfd(sock));
    return 0;
  }

  template <typename F, typename... Args>
  static std::vector<int> forkReadFds(int sock) {
    std::vector<int> r;
    // Force evaluation order with {}
    auto x = {
        forkDeserializeFds(typeResolver<std::decay_t<Args>>{}, sock, r)...};
    (void)x;
    return r;
  }

  template <typename F, typename Tuple, size_t... I>
  static void applyImpl(F&& f, Tuple tuple, std::index_sequence<I...>) {
    std::forward<F>(f)(std::move(std::get<I>(tuple))...);
  }

  template <typename F, typename Tuple>
  static void apply(F&& f, Tuple tuple) {
    applyImpl(
        std::forward<F>(f),
        std::move(tuple),
        std::make_index_sequence<std::tuple_size<Tuple>::value>{});
  }

  template <typename F, typename... Args>
  static void forkEntry(
      cereal::BinaryInputArchive& ar,
      const std::vector<int>& fds) {
    typename std::aligned_storage<sizeof(F), alignof(F)>::type buf;
    ar.loadBinary(&buf, sizeof(buf));
    F& f = (F&)buf;
    size_t fdsIndex = 0;
    apply(
        f,
        std::tuple<std::decay_t<Args>...>{forkDeserialize(
            typeResolver<std::decay_t<Args>>{}, ar, fds, fdsIndex)...});
    std::_Exit(0);
  }
};
} // namespace cherrypi
