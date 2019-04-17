/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "blobstorage.h"

#include "cpid2kworker.h"
#include "redisclient.h"

#include <common/assert.h>
#include <common/fsutils.h>
#include <common/serialization.h>
#include <common/zstdstream.h>

#include <autogradpp/serialization.h>
#include <fmt/format.h>

#include <fstream>

using namespace common;
using json = nlohmann::json;

namespace cpid {

namespace {
int constexpr kMaxOldVersions = 10;
} // namespace

BlobStorageDisk::BlobStorageDisk(std::string root) : root_(std::move(root)) {}

void BlobStorageDisk::put(
    std::string const& key,
    std::vector<char> const& data) {
  auto path = fmt::format("{}/{}.bin", root_, key);
  fsutils::mkdir(fsutils::dirname(path));
  if (fsutils::exists(path)) {
    int i = 0;
    while (++i <= kMaxOldVersions) {
      auto backupPath = fmt::format("{}.old-{}", path, i);
      if (!fsutils::exists(backupPath)) {
        LOG(INFO) << fmt::format(
            "Previous data found with key '{}' at '{}'; moving previous data "
            "to '{}'",
            key,
            path,
            backupPath);
        fsutils::mv(path, backupPath);
        break;
      }
    }
    if (i > kMaxOldVersions) {
      LOG(WARNING) << fmt::format(
          "Previous data found with key '{}' at '{}' and too many previous "
          "versions exist; overwriting'",
          key,
          path);
    }
  }

  std::ofstream ofs(path, std::ios::out | std::ios::binary);
  ofs << data.size();
  ofs.write(data.data(), data.size());
  ofs.close();
  if (!ofs) {
    throw std::runtime_error(
        fmt::format("Error writing {} bytes to '{}'", data.size(), path));
  }
}

std::vector<char> BlobStorageDisk::get(std::string const& key) {
  auto path = fmt::format("{}/{}.bin", root_, key);
  std::ifstream ifs(path, std::ios::in | std::ios::binary);
  if (!ifs) {
    throw std::runtime_error(
        fmt::format("No data found for key '{}' at '{}'", key, path));
  }

  size_t size;
  ifs >> size;
  std::vector<char> data(size);
  ifs.read(data.data(), size);
  if (!ifs) {
    throw std::runtime_error(
        fmt::format("Error reading {} bytes from '{}'", size, path));
  }
  return data;
}

BlobStorageRedis::BlobStorageRedis(
    std::shared_ptr<Cpid2kWorker> worker,
    std::string prefix)
    : worker_(std::move(worker)), prefix_(std::move(prefix)) {}

BlobStorageRedis::~BlobStorageRedis() {}

void BlobStorageRedis::put(
    std::string const& key,
    std::vector<char> const& data) {
  auto k = dbkey(key);
  auto v = std::string_view(data.data(), data.size());
  auto client = worker_->threadLocalClient();
  std::string setCmd = "SETNX";
  while (true) {
    auto reply = client->command({setCmd, k, v});
    if (reply.isError()) {
      throw std::runtime_error(fmt::format(
          "Error storing {} bytes with key '{}' at '{}': {}",
          data.size(),
          key,
          k,
          reply.error()));
    }
    ASSERT(reply.isInteger());
    if (reply.integer() == 1) {
      // All ok!
      return;
    }

    // Key already exists -- back it up
    std::vector<std::string> backupKeys;
    std::vector<std::string> cmds;
    for (int i = 1; i <= kMaxOldVersions; i++) {
      backupKeys.push_back(fmt::format("{}.old-{}", k, i));
      cmds.push_back(client->format({"RENAMENX", k, backupKeys.back()}));
    }

    bool renamed = false;
    auto replies = client->commands(cmds);
    ASSERT(replies.size() == cmds.size());
    for (auto i = 0U; i < replies.size(); i++) {
      if (replies[i].integer() == 1) {
        // Rename happened, we have our free key now; all other replies should
        // be errors now (because the source does not exist anymore)
        LOG(INFO) << fmt::format(
            "Previous data found with key '{}' at '{}'; moved previous data "
            "to '{}'",
            key,
            k,
            backupKeys[i]);
        renamed = true;
        break;
      }
    }

    if (!renamed) {
      LOG(WARNING) << fmt::format(
          "Previous data found with key '{}' at '{}' and too many previous "
          "versions exist; overwriting'",
          key,
          k);
      setCmd = "SET";
    }
  }
}

std::vector<char> BlobStorageRedis::get(std::string const& key) {
  auto k = dbkey(key);
  auto reply = worker_->threadLocalClient()->command({"GET", k});
  if (reply.isError()) {
    throw std::runtime_error(
        fmt::format("Error retreiving data from '{}': {}", k, reply.error()));
  }
  if (reply.isNil()) {
    throw std::runtime_error(
        fmt::format("No data found for key '{}' at '{}'", key, k));
  }
  ASSERT(reply.isString());
  auto sv = reply.stringv();
  std::vector<char> data(sv.data(), sv.data() + sv.size());
  return data;
}

std::string BlobStorageRedis::dbkey(std::string const& key) {
  return fmt::format("{}:{}", prefix_, key);
}

} // namespace cpid
