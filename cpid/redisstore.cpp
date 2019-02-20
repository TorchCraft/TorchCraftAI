/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "redisstore.h"

#include <common/utils.h>

#include <fmt/format.h>
#include <glog/logging.h>
#include <prettyprint/prettyprint.hpp>

#include <sstream>
#include <thread>

namespace cpid {

RedisStore::RedisStore(std::string prefix, std::string_view host, int port)
    : prefix_(std::move(prefix)) {
  redis_ = std::make_unique<RedisClient>(host, port);
}

RedisStore::~RedisStore() {
  // Delete previously set key
  std::vector<std::string_view> args;
  args.push_back("DEL");
  for (auto const& key : setKeys_) {
    args.push_back(key);
  }
  try {
    redis_->command(args);
  } catch (...) {
    // Ignore
  }
}

void RedisStore::set(
    std::string const& key,
    std::vector<uint8_t> const& value) {
  auto pkey = fmt::format("{}:{}", prefix_, key);
  auto reply = redis_->command(
      "SETNX %b %b",
      pkey.c_str(),
      (size_t)pkey.size(),
      value.data(),
      (size_t)value.size());
  if (reply.integer() != 1) {
    throw std::runtime_error("Key " + pkey + " already set");
  }
  setKeys_.push_back(pkey);
}

std::vector<uint8_t> RedisStore::get(std::string const& key) {
  // Block until key is set
  wait({key});

  // Get value
  auto pkey = fmt::format("{}:{}", prefix_, key);
  auto reply = redis_->command("GET %b", pkey.c_str(), (size_t)pkey.size());
  auto sv = reply.stringv();
  auto const* data = reinterpret_cast<uint8_t const*>(sv.data());
  return std::vector<uint8_t>(data, data + sv.size());
}

int64_t RedisStore::add(std::string const& key, int64_t value) {
  throw std::runtime_error(
      "RedisStore::add() not implemented -- who wants to have this?");
}

bool RedisStore::check(std::vector<std::string> const& keys) {
  std::vector<std::string> args;
  args.push_back("EXISTS");
  for (auto const& key : keys) {
    args.push_back(fmt::format("{}:{}", prefix_, key));
  }

  std::vector<char const*> argv;
  std::vector<size_t> argvlen;
  for (auto const& arg : args) {
    argv.push_back(arg.c_str());
    argvlen.push_back(arg.length());
  }

  auto argc = argv.size();
  char* target;
  int len = redisFormatCommandArgv(&target, argc, argv.data(), argvlen.data());
  if (len == -1) {
    throw std::runtime_error("Out of memory");
  } else if (len == -2) {
    throw std::runtime_error("Invalid format string");
  }
  auto guard = common::makeGuard([&] { redisFreeCommand(target); });

  auto reply = redis_->command(std::string_view(target, len));
  return reply.integer() == int64_t(keys.size());
}

void RedisStore::wait(std::vector<std::string> const& keys) {
  // Can't use c10d::Store::kDefaultTimeout due to linker issues
  // (static constexpr defined in header)
  wait(keys, std::chrono::seconds(300));
}

void RedisStore::wait(
    std::vector<std::string> const& keys,
    std::chrono::milliseconds const& timeout) {
  // Polling is fine for the typical rendezvous use case, as it is
  // only done at initialization time and  not at run time.
  const auto start = std::chrono::steady_clock::now();
  while (!check(keys)) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start);
    if (timeout != std::chrono::milliseconds::zero() && elapsed > timeout) {
      std::ostringstream oss;
      oss << keys;
      throw std::runtime_error(
          fmt::format("Wait timeout for key(s): {}", oss.str()));
    }
    /* sleep override */
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

} // namespace cpid
