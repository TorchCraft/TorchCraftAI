/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <common/language.h>

#include <autogradpp/autograd.h>
#include <nlohmann/json.hpp>

namespace cpid {

class Cpid2kWorker;

/**
 * A simple interface for key-value data blob stores.
 *
 * Keys are required to be unique, and calling `put()` twice with the same data
 * will result in an exception. The reasoning is that we want to do local
 * in-memory caching in ModelStorage and don't want to write cache invalidation
 * logic (well, *I* don't want to, at least).
 */
class BlobStorage {
 public:
  virtual ~BlobStorage() = default;
  virtual void put(std::string const& key, std::vector<char> const& data) = 0;
  virtual std::vector<char> get(std::string const& key) = 0;
};

class BlobStorageDisk : public BlobStorage {
 public:
  BlobStorageDisk(std::string root);
  virtual ~BlobStorageDisk() override = default;

  virtual void put(std::string const& key, std::vector<char> const& data)
      override;
  virtual std::vector<char> get(std::string const& key) override;

 private:
  std::string root_;
};

/**
 * Blob storage in Redis.
 *
 * Note that Cpid2kWorker is only used to obtain a database connection in a
 * thread-safe manner. Models are stored directly under the separately specified
 * prefix to ease data access across jobs.
 */
class BlobStorageRedis : public BlobStorage {
 public:
  BlobStorageRedis(
      std::shared_ptr<Cpid2kWorker> worker,
      std::string prefix = "blob");
  virtual ~BlobStorageRedis() override;

  virtual void put(std::string const& key, std::vector<char> const& data)
      override;
  virtual std::vector<char> get(std::string const& key) override;

 private:
  std::string dbkey(std::string const& key);

  std::shared_ptr<Cpid2kWorker> worker_;
  std::string prefix_;
};

} // namespace cpid
