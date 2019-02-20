/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "redisclient.h"

#include <c10d/Store.hpp>

#include <memory>

namespace cpid {

/**
 * c10d::Store for rendez-vous backed by Redis.
 *
 * This is pretty much a copy of gloo's RedisStore:
 * https://github.com/facebookincubator/gloo/blob/master/gloo/rendezvous/redis_store.cc
 */
class RedisStore : public c10d::Store {
 public:
  RedisStore(std::string prefix, std::string_view host, int port);
  virtual ~RedisStore();

  void set(std::string const& key, std::vector<uint8_t> const& value) override;
  std::vector<uint8_t> get(std::string const& key) override;
  int64_t add(std::string const& key, int64_t value) override;
  bool check(std::vector<std::string> const& keys) override;
  void wait(std::vector<std::string> const& keys) override;
  void wait(
      std::vector<std::string> const& keys,
      std::chrono::milliseconds const& timeout) override;

 private:
  // TODO Remove this once c10d::PrefixStore wraps by shared_ptr
  std::string prefix_;
  std::unique_ptr<RedisClient> redis_;
  std::vector<std::string> setKeys_;
};

} // namespace cpid
