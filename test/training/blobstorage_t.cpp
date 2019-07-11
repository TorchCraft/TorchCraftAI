/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include <cpid/blobstorage.h>
#include <cpid/cpid2kworker.h>
#include <cpid/redisclient.h>

#include <common/assert.h>
#include <common/checksum.h>
#include <common/fsutils.h>
#include <common/rand.h>

#include <fmt/format.h>
#include <glog/logging.h>

// From redisclient_t.cpp
DECLARE_string(redis_host);
DECLARE_int32(redis_port);

using namespace cpid;
using namespace common;

namespace {
std::vector<char> randBytes(size_t size) {
  auto eng = Rand::makeRandEngine<std::minstd_rand0>();
  auto dist = std::uniform_int_distribution<char>(
      std::numeric_limits<char>::min(), std::numeric_limits<char>::max());
  std::vector<char> bytes(size);
  for (auto i = 0U; i < size; i++) {
    bytes[i] = dist(eng);
  }
  return bytes;
}

std::string vmd5(std::vector<char> const& d) {
  return toHex(md5sum(d.data(), d.size()));
}

std::unique_ptr<Cpid2kWorker> makeWorker(
    std::string_view prefix,
    std::string_view id) {
  RedisClient cl(FLAGS_redis_host, FLAGS_redis_port);
  auto reply = cl.set(fmt::format("{}:boot:{}", prefix, id), "true");
  ASSERT(reply.ok());
  cl.command({"INCR", fmt::format("{}:peerv", prefix)});

  auto intervalMs = 100;
  auto info = Cpid2kWorkerInfo::withLocalIp();
  info.id = id;
  return std::make_unique<Cpid2kWorker>(
      info,
      std::string(prefix),
      FLAGS_redis_host,
      FLAGS_redis_port,
      intervalMs);
}

} // namespace

CASE("blobstoragedisk/basic") {
  auto dir = fsutils::mktempd();
  auto cleanup = makeGuard([&]() { fsutils::rmrf(dir); });
  auto i1 = randId(64);
  auto i2 = randId(32);
  auto i3 = randId(16);
  auto r1 = randBytes(16);
  auto r2 = randBytes(16 * 1024);
  auto r3 = randBytes(16 * 1024 * 1024);

  {
    BlobStorageDisk storage(dir);
    EXPECT_THROWS(storage.get("nonexistent"));

    EXPECT_NO_THROW(storage.put(i1, r1));
    EXPECT(vmd5(storage.get(i1)) == vmd5(r1));

    EXPECT_NO_THROW(storage.put(i2, r2));
    EXPECT(vmd5(storage.get(i2)) == vmd5(r2));
    EXPECT(vmd5(storage.get(i1)) == vmd5(r1));

    EXPECT_NO_THROW(storage.put(i3, r3));
    EXPECT(vmd5(storage.get(i3)) == vmd5(r3));
    EXPECT(vmd5(storage.get(i2)) == vmd5(r2));
    EXPECT(vmd5(storage.get(i1)) == vmd5(r1));

    // Keys with slashes work
    EXPECT_NO_THROW(storage.put("hello/world", r3));

    // Can't store files with keys that are too long
    EXPECT_THROWS(storage.put(randId(NAME_MAX * 2), r1));

    // We should have four files now
    EXPECT(fsutils::findr(dir, "*").size() == 4U);

    // Backups are created for duplicates
    EXPECT_NO_THROW(storage.put(i1, r1));
    EXPECT_NO_THROW(storage.put(i1, r1));
    EXPECT_NO_THROW(storage.put(i2, r2));
    EXPECT_NO_THROW(storage.put(i3, r3));

    EXPECT(fsutils::findr(dir, "*").size() == 8U);
  }

  // Store destructor does not delete files
  EXPECT(fsutils::findr(dir, "*").size() == 8U);

  {
    // Can simply instantiate again to accces the data
    BlobStorageDisk storage(dir);

    EXPECT(vmd5(storage.get(i3)) == vmd5(r3));
    EXPECT(vmd5(storage.get(i2)) == vmd5(r2));
    EXPECT(vmd5(storage.get(i1)) == vmd5(r1));

    // Backups are created for duplicates, still
    EXPECT_NO_THROW(storage.put(i1, r1));
    EXPECT(fsutils::findr(dir, "*").size() == 9U);
  }
}

CASE("blobstoragedisk/invalid_root") {
  BlobStorageDisk storage("/proc/this/directory/should/not/exist");
  EXPECT_THROWS(storage.put("key", std::vector<char>(16)));
}

CASE("blobstorageredis/basic[.redis]") {
  std::shared_ptr<Cpid2kWorker> worker =
      makeWorker("test_blobstorage", "worker");
  auto i1 = randId(64);
  auto i2 = randId(32);
  auto i3 = randId(16);
  auto r1 = randBytes(16);
  auto r2 = randBytes(16 * 1024);
  auto r3 = randBytes(16 * 1024 * 1024);
  auto prefix = "blob";

  {
    BlobStorageRedis storage(worker, prefix);
    EXPECT_THROWS(storage.get("nonexistent"));

    EXPECT_NO_THROW(storage.put(i1, r1));
    EXPECT(vmd5(storage.get(i1)) == vmd5(r1));

    EXPECT_NO_THROW(storage.put(i2, r2));
    EXPECT(vmd5(storage.get(i2)) == vmd5(r2));
    EXPECT(vmd5(storage.get(i1)) == vmd5(r1));

    EXPECT_NO_THROW(storage.put(i3, r3));
    EXPECT(vmd5(storage.get(i3)) == vmd5(r3));
    EXPECT(vmd5(storage.get(i2)) == vmd5(r2));
    EXPECT(vmd5(storage.get(i1)) == vmd5(r1));

    // Can store files with slashes and keys that are long
    EXPECT_NO_THROW(storage.put("hello/world", r3));
    EXPECT_NO_THROW(storage.put(randId(NAME_MAX * 2), r1));

    // Can't store very large blobs
    EXPECT_THROWS(
        storage.put("mesobig", std::vector<char>(1024 * 1024 * 1024)));

    EXPECT(
        worker->threadLocalClient()
            ->command({"KEYS", fmt::format("{}:*", prefix)})
            .size() == 5U);

    // Backups are created for duplicates
    EXPECT_NO_THROW(storage.put(i1, r1));
    EXPECT_NO_THROW(storage.put(i1, r1));
    EXPECT_NO_THROW(storage.put(i2, r2));
    EXPECT_NO_THROW(storage.put(i3, r3));

    EXPECT(
        worker->threadLocalClient()
            ->command({"KEYS", fmt::format("{}:*", prefix)})
            .size() == 9U);
  }

  {
    // Can simply instantiate again to accces the data
    BlobStorageRedis storage(worker, prefix);

    EXPECT(vmd5(storage.get(i3)) == vmd5(r3));
    EXPECT(vmd5(storage.get(i2)) == vmd5(r2));
    EXPECT(vmd5(storage.get(i1)) == vmd5(r1));

    // Backups are created for duplicates, still
    EXPECT_NO_THROW(storage.put(i1, r1));

    EXPECT(
        worker->threadLocalClient()
            ->command({"KEYS", fmt::format("{}:*", prefix)})
            .size() == 10U);
  }
}
