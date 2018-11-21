/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "common/rand.h"
#include "common/serialization.h"
#include "common/zstdstream.h"
#include "fsutils.h"
#include "utils.h"

#include <algorithm>

using namespace cherrypi;
namespace zstd = common::zstd;

std::initializer_list<uint64_t> constexpr kSizes =
    {0U, 1U, 2U, 7U, 128U, 1000U, 10000U, 100000U};

CASE("zstdstream/membuf/singlewrite") {
  for (auto size : kSizes) {
    std::vector<char> d(size);
    std::generate(
        d.begin(), d.end(), common::Rand::makeRandEngine<std::minstd_rand>());

    common::OMembuf obuf;
    {
      zstd::ostream os(&obuf);
      os.write(d.data(), d.size());
    }

    common::IMembuf ibuf(obuf.data());
    zstd::istream is(&ibuf);
    std::vector<char> r(size);
    is.read(r.data(), r.size());

    bool dataIsSame = (d == r); // Don't spam -pass output
    EXPECT(dataIsSame);
  }
}

CASE("zstdstream/membuf/multiwrite") {
  for (auto size : kSizes) {
    std::vector<char> d(size);
    std::generate(
        d.begin(), d.end(), common::Rand::makeRandEngine<std::minstd_rand>());

    common::OMembuf obuf;
    {
      zstd::ostream os(&obuf);
      auto pos = 0U;
      while (pos < d.size()) {
        auto chunk = std::min(
            d.size() - pos,
            static_cast<size_t>(
                1 + common::Rand::rand() % std::max(uint64_t{2}, (size / 4))));
        os.write(d.data() + pos, chunk);
        pos += chunk;
      }
    }

    common::IMembuf ibuf(obuf.data());
    zstd::istream is(&ibuf);
    std::vector<char> r(size);
    is.read(r.data(), r.size());

    bool dataIsSame = (d == r); // Don't spam -pass output
    EXPECT(dataIsSame);
  }
}

CASE("zstdstream/membuf/multiframe") {
  for (auto size : kSizes) {
    std::vector<char> d(size);
    std::generate(
        d.begin(), d.end(), common::Rand::makeRandEngine<std::minstd_rand>());

    common::OMembuf obuf;
    {
      zstd::ostream os(&obuf);
      // Write multiple frames by calling flush() in between writes.
      // Decompression should not be affected
      auto pos = 0U;
      auto frames = 0U;
      while (pos < d.size()) {
        auto chunk = std::min(
            d.size() - pos,
            static_cast<size_t>(
                1 + common::Rand::rand() % std::max(uint64_t{2}, (size / 4))));
        os.write(d.data() + pos, chunk);
        os.flush();
        EXPECT(os.good());
        pos += chunk;
        frames++;
      }

      // Compare to single-frame compression. The size for multi-frame
      // compression should be larger
      if (frames > 1) {
        common::OMembuf obufs;
        zstd::ostream oss(&obufs);
        oss.write(d.data(), d.size());
        oss.flush();
        EXPECT(obuf.data().size() >= obufs.data().size());
      }
    }

    common::IMembuf ibuf(obuf.data());
    zstd::istream is(&ibuf);
    std::vector<char> r(size);
    is.read(r.data(), r.size());

    bool dataIsSame = (d == r); // Don't spam -pass output
    EXPECT(dataIsSame);
  }
}

CASE("zstdstream/membuf/cereal") {
  std::unordered_map<std::string, std::string> d;
  d["foo"] = "bar";
  d["a"] = "b";
  d["hello"] = "world";

  common::OMembuf obuf;
  {
    zstd::ostream os(&obuf);
    cereal::BinaryOutputArchive ar(os);
    ar(d);
  }

  common::IMembuf ibuf(obuf.data());
  std::unordered_map<std::string, std::string> v;
  {
    zstd::istream is(&ibuf);
    cereal::BinaryInputArchive ar(is);
    ar(v);
  }

  EXPECT(d == v);
}

CASE("zstdstream/membuf/cereal/multi") {
  std::unordered_map<std::string, std::string> d1;
  d1["foo"] = "bar";
  d1["a"] = "b";
  d1["hello"] = "world";
  std::vector<int> d2 = {1, 2, 3, 4, 0, -10};

  common::OMembuf obuf;
  zstd::ostream os(&obuf);
  {
    cereal::BinaryOutputArchive ar(os);
    ar(d1);
  }
  {
    cereal::BinaryOutputArchive ar(os);
    ar(d2);
  }
  os.flush();

  common::IMembuf ibuf(obuf.data());
  zstd::istream is(&ibuf);
  std::unordered_map<std::string, std::string> v1;
  std::vector<int> v2;
  {
    cereal::BinaryInputArchive ar(is);
    ar(v1);
  }
  {
    cereal::BinaryInputArchive ar(is);
    ar(v2);
  }

  EXPECT(d1 == v1);
  EXPECT(d2 == v2);
}

CASE("zstdstream/fileio") {
  auto tdir = fsutils::mktempd();
  auto guard = utils::makeGuard([&] { fsutils::rmrf(tdir); });

  for (auto size : kSizes) {
    std::vector<char> d(size);
    std::generate(
        d.begin(), d.end(), common::Rand::makeRandEngine<std::minstd_rand>());

    auto path = tdir + "/" + std::to_string(size);
    {
      zstd::ofstream os(path);
      os.write(d.data(), d.size());
    }

    zstd::ifstream is(path);
    std::vector<char> r(size);
    is.read(r.data(), r.size());

    bool dataIsSame = (d == r); // Don't spam -pass output
    EXPECT(dataIsSame);
  }
}

CASE("zstdstream/fileio/multiframe") {
  auto tdir = fsutils::mktempd();
  auto guard = utils::makeGuard([&] { fsutils::rmrf(tdir); });

  for (auto size : kSizes) {
    std::vector<char> d(size);
    std::generate(
        d.begin(), d.end(), common::Rand::makeRandEngine<std::minstd_rand>());

    auto path = tdir + "/" + std::to_string(size);
    {
      zstd::ofstream os(path);
      auto pos = 0U;
      while (pos < d.size()) {
        auto chunk = std::min(
            d.size() - pos,
            static_cast<size_t>(
                1 + common::Rand::rand() % std::max(uint64_t{2}, (size / 4))));
        os.write(d.data() + pos, chunk);
        pos += chunk;
      }
    }

    zstd::ifstream is(path);
    std::vector<char> r(size);
    is.read(r.data(), r.size());

    bool dataIsSame = (d == r); // Don't spam -pass output
    EXPECT(dataIsSame);
  }
}
