/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <hiredis/hiredis.h>

#include <string_view>
#include <vector>

namespace cpid {

class RedisReply;

/**
 * Simple, synchronous C++ wrapper for the Hiredis Redis client.
 *
 * Functionality is provided to:
 * - format commands according to the Redis protocol
 * - send commands and retrieve replies (a blocking operation)
 * - pipelining of commands
 *
 * Whenever an error is encountered, functions will throw runtime_errors.
 * Note that this class is *not* thread-safe.
 */
class RedisClient {
 public:
  RedisClient(
      std::string_view host,
      int port = 6379,
      std::string_view name = std::string_view());
  ~RedisClient();

  std::string_view host() const;
  int port() const;
  bool isConnected() const;
  void reconnect() const;

  std::string format(char const* fmt, ...);
  std::string format(std::initializer_list<std::string_view> args);
  std::string format(std::vector<std::string_view> const& args);
  RedisReply command(char const* fmt, ...);
  RedisReply command(std::initializer_list<std::string_view> args);
  RedisReply command(std::vector<std::string_view> const& args);
  RedisReply command(std::string_view cmd);
  std::vector<RedisReply> commands(std::vector<std::string> const& cmds);
  RedisReply getReply();

  // Convenience wrappers
  bool ping();
  RedisReply set(std::string_view key, std::string_view value);
  RedisReply get(std::string_view key);

  redisContext* ctx();

 private:
  redisContext* redis_;
};

/**
 * Wrapper class for redisReply from Hiredis.
 *
 * This class provides a typed interface to replies from a Redis server.
 * Replies can be nested, so there's some basic functionality to access data in
 * array replies (size()/at()/begin()/end()). Nested replies can only be
 * accessed by reference; if the top-level reply is destructed the nested
 * replies will be destructed as well.
 *
 * If the type of the underlying reply is not the type expected by a function
 * (e.g. calling string() on a reply holding an integer), you'll get
 * runtime_error exceptions.
 */
class RedisReply {
 public:
  using Iterator = std::vector<RedisReply>::iterator;

  RedisReply() = default;
  RedisReply(RedisReply const&) = delete;
  RedisReply(RedisReply&& other);
  ~RedisReply();
  RedisReply& operator=(RedisReply const&) = delete;
  RedisReply& operator=(RedisReply&& other);

  bool isString() const;
  bool isArray() const;
  bool isInteger() const;
  bool isNil() const;
  bool isStatus() const;
  bool isError() const;

  std::string string() const;
  std::string_view stringv() const;
  std::vector<std::string_view> stringvs() const;
  int64_t integer() const;
  std::string status() const;
  std::string_view statusv() const;
  std::string error() const;
  bool ok() const;

  size_t size() const;
  RedisReply& at(size_t index);
  Iterator begin();
  Iterator end();

 private:
  RedisReply(void* reply, bool own = true);

  void ensureType(int type) const;

  redisReply* reply_ = nullptr;
  bool owns_ = true;
  std::vector<RedisReply> elements_;
  friend class RedisClient;
};

} // namespace cpid
