/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "common/datareader.h"
#include "fsutils.h"
#include "utils.h"

#include <atomic>

using namespace cherrypi;
using namespace common;

namespace {

template <typename T, typename OStream = std::ofstream>
std::pair<std::string, std::vector<std::string>> createTestData(
    std::vector<std::pair<std::string, T>> data) {
  auto dir = fsutils::mktempd();
  std::vector<std::string> paths;
  for (auto const& d : data) {
    OStream os(dir + "/" + d.first);
    cereal::BinaryOutputArchive archive(os);
    archive(d.second);
    paths.push_back(d.first);
  }
  return std::make_pair(dir, std::move(paths));
}

// We still want to know the directory for subsequent cleanup
template <typename T, typename OStream = std::ofstream>
std::pair<std::string, std::vector<std::string>> createTestDataNoPrefix(
    std::vector<std::pair<std::string, T>> data) {
  auto res = createTestData<T, OStream>(std::move(data));
  std::vector<std::string> paths;
  for (auto const& path : res.second) {
    paths.emplace_back(res.first + "/" + path);
  }
  return std::make_pair(res.first, std::move(paths));
}

} // namespace

CASE("datareader/simple") {
  auto paths = createTestDataNoPrefix<int>(
      {
          {"f0", 0},
          {"f1", 1},
          {"f2", 2},
          {"f3", 3},
          {"f4", 4},
          {"f5", 5},
          {"f6", 6},
          {"f7", 7},
          {"f8", 8},
          {"f9", 9},
          {"f10", 10},
      });
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(paths.first); });

  auto reader = DataReader<int>(paths.second, 2, 4);
  auto it = reader.iterator();
  EXPECT(it->hasNext() == true);
  auto d = it->next();
  EXPECT(d.size() == 4u);
  EXPECT(d[0] == 0);
  EXPECT(d[1] == 1);
  EXPECT(d[2] == 2);
  EXPECT(d[3] == 3);
  EXPECT((d = it->next(), true));
  EXPECT(d.size() == 4u);
  EXPECT(d[0] == 4);
  EXPECT(d[1] == 5);
  EXPECT(d[2] == 6);
  EXPECT(d[3] == 7);
  EXPECT((d = it->next(), true));
  EXPECT(d.size() == 3u);
  EXPECT(d[0] == 8);
  EXPECT(d[1] == 9);
  EXPECT(d[2] == 10);
  EXPECT(it->hasNext() == false);
  EXPECT_THROWS(it->next());

  reader.shuffle();
  it = reader.iterator();
  EXPECT(it->hasNext() == true);
  d = it->next();
  EXPECT(d.size() == 4u);
  // seed is fixed in main_test.cpp but we don't want to rely on the exact
  // result
  EXPECT(d != std::vector<int>({1, 2, 3, 4}));
}

CASE("datareader/prefix") {
  auto paths = createTestData<int>(
      {
          {"f0", 0}, {"f1", 1}, {"f2", 2}, {"f3", 3}, {"f4", 4},
      });
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(paths.first); });

  auto reader = DataReader<int>(paths.second, 2, 4, paths.first);
  auto it = reader.iterator();
  EXPECT(it->hasNext() == true);
  auto d = it->next();
  EXPECT(d.size() == 4u);
  EXPECT(d[0] == 0);
  EXPECT(d[1] == 1);
  EXPECT(d[2] == 2);
  EXPECT(d[3] == 3);
  EXPECT((d = it->next(), true));
  EXPECT(d.size() == 1u);
  EXPECT(d[0] == 4);
  EXPECT(it->hasNext() == false);
  EXPECT_THROWS(it->next());
}

CASE("datareader/single_thread") {
  auto paths = createTestDataNoPrefix<int>(
      {
          {"f0", 0},
          {"f1", 1},
          {"f2", 2},
          {"f3", 3},
          {"f4", 4},
          {"f5", 5},
          {"f6", 6},
          {"f7", 7},
          {"f8", 8},
          {"f9", 9},
          {"f10", 10},
      });
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(paths.first); });

  auto it = std::make_unique<DataReaderIterator<int>>(
      paths.second, 1, 4, std::string());
  EXPECT(it->hasNext() == true);
  auto d = it->next();
  EXPECT(d.size() == 4u);
  EXPECT(d[0] == 0);
  EXPECT(d[1] == 1);
  EXPECT(d[2] == 2);
  EXPECT(d[3] == 3);
  EXPECT((d = it->next(), true));
  EXPECT(d.size() == 4u);
  EXPECT(d[0] == 4);
  EXPECT(d[1] == 5);
  EXPECT(d[2] == 6);
  EXPECT(d[3] == 7);
  EXPECT((d = it->next(), true));
  EXPECT(d.size() == 3u);
  EXPECT(d[0] == 8);
  EXPECT(d[1] == 9);
  EXPECT(d[2] == 10);
  EXPECT(it->hasNext() == false);
  EXPECT_THROWS(it->next());
}

CASE("datareader/overthreaded") {
  auto paths = createTestDataNoPrefix<int>(
      {
          {"f0", 0},
          {"f1", 1},
          {"f2", 2},
          {"f3", 3},
          {"f4", 4},
          {"f5", 5},
          {"f6", 6},
          {"f7", 7},
          {"f8", 8},
          {"f9", 9},
          {"f10", 10},
      });
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(paths.first); });

  auto it = std::make_unique<DataReaderIterator<int>>(
      paths.second, 100, 4, std::string());
  EXPECT(it->hasNext() == true);
  auto d = it->next();
  EXPECT(d.size() == 4u);
  EXPECT(d[0] == 0);
  EXPECT(d[1] == 1);
  EXPECT(d[2] == 2);
  EXPECT(d[3] == 3);
  EXPECT((d = it->next(), true));
  EXPECT(d.size() == 4u);
  EXPECT(d[0] == 4);
  EXPECT(d[1] == 5);
  EXPECT(d[2] == 6);
  EXPECT(d[3] == 7);
  EXPECT((d = it->next(), true));
  EXPECT(d.size() == 3u);
  EXPECT(d[0] == 8);
  EXPECT(d[1] == 9);
  EXPECT(d[2] == 10);
  EXPECT(it->hasNext() == false);
  EXPECT_THROWS(it->next());
}

CASE("datareader/overbatched") {
  auto paths = createTestDataNoPrefix<int>(
      {
          {"f0", 0}, {"f1", 1}, {"f2", 2},
      });
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(paths.first); });

  auto it = std::make_unique<DataReaderIterator<int>>(
      paths.second, 2, 100, std::string());
  EXPECT(it->hasNext() == true);
  auto d = it->next();
  EXPECT(d.size() == 3u);
  EXPECT(d[0] == 0);
  EXPECT(d[1] == 1);
  EXPECT(d[2] == 2);
  EXPECT(it->hasNext() == false);
  EXPECT_THROWS(it->next());
}

CASE("datareader/early_destruction") {
  auto paths = createTestDataNoPrefix<int>(
      {
          {"f0", 0},
          {"f1", 1},
          {"f2", 2},
          {"f3", 3},
          {"f4", 4},
          {"f5", 4},
          {"f6", 4},
      });
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(paths.first); });

  auto it = std::make_unique<DataReaderIterator<int>>(
      paths.second, 2, 3, std::string());
  EXPECT(it->hasNext() == true);
  auto d = it->next();
  EXPECT(d.size() == 3u);
  EXPECT(d[0] == 0);
  EXPECT(d[1] == 1);
  EXPECT(d[2] == 2);
  EXPECT((it.reset(), true));

  it = std::make_unique<DataReaderIterator<int>>(
      paths.second, 100, 3, std::string());
  EXPECT((it.reset(), true));

  // Delay start of worker thread with a slow init function
  it = std::make_unique<DataReaderIterator<int>>(
      paths.second, 10, 3, std::string(), []() {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      });
  EXPECT((it.reset(), true));

  // Once again with a transform
  auto itt = makeDataReaderTransform(
      std::make_unique<DataReaderIterator<int>>(
          paths.second, 2, 3, std::string()),
      [](std::vector<int> const& v) { return v; },
      []() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); });
  EXPECT((itt.reset(), true));
}

CASE("datareader/zstd") {
  auto paths = createTestDataNoPrefix<int, zstd::ofstream>(
      {
          {"f0", 0}, {"f1", 1}, {"f2", 2}, {"f3", 3},
      });
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(paths.first); });

  auto it = std::make_unique<DataReaderIterator<int>>(
      paths.second, 2, 4, std::string());
  EXPECT(it->hasNext() == true);
  auto d = it->next();
  EXPECT(d.size() == 4u);
  EXPECT(d[0] == 0);
  EXPECT(d[1] == 1);
  EXPECT(d[2] == 2);
  EXPECT(d[3] == 3);
  EXPECT(it->hasNext() == false);
  EXPECT_THROWS(it->next());
}

CASE("datareader/non_existent_data") {
  auto paths = createTestData<int>(
      {
          {"f0", 0}, {"f1", 1},
      });
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(paths.first); });

  paths.second.insert(paths.second.begin(), "idontexist");
  paths.second.push_back("idontexisteither");

  auto it = std::make_unique<DataReaderIterator<int>>(
      paths.second, 2, 2, paths.first);
  EXPECT(it->hasNext() == true);
  auto d = it->next();
  EXPECT(d.size() == 2u);
  EXPECT(d[0] == 0);
  EXPECT(d[1] == 1);
  EXPECT(it->hasNext()); // Actually, there's one last path
  EXPECT((d = it->next(), true));
  EXPECT(d.size() == 0u);
  EXPECT(it->hasNext() == false);
  EXPECT_THROWS(it->next());
}

CASE("datareader/corrupt_wrong_data") {
  auto paths = createTestDataNoPrefix<int64_t>(
      {
          {"f0", 100}, {"f1", 200},
      });
  auto pathsStr = createTestDataNoPrefix<std::string>(
      {
          {"f0", "foo"}, {"f1", "bar"},
      });

  auto cleanup = utils::makeGuard([&]() {
    fsutils::rmrf(paths.first);
    fsutils::rmrf(pathsStr.first);
  });

  paths.second.insert(
      paths.second.end(), pathsStr.second.begin(), pathsStr.second.end());
  {
    std::ofstream ofs(paths.first + "/garbage");
    ofs << char(10) << char(22);
  }
  paths.second.push_back(paths.first + "/garbage");

  auto it = std::make_unique<DataReaderIterator<std::string>>(
      paths.second, 2, 1, std::string());
  EXPECT(it->hasNext() == true);
  auto d = it->next();
  EXPECT(d.size() == 1u);
  EXPECT(d[0] == "foo");
  EXPECT((d = it->next(), true));
  EXPECT(d.size() == 1u);
  EXPECT(d[0] == "bar");
  EXPECT(it->hasNext()); // Data at the end: just two bytes
  EXPECT((d = it->next(), true));
  EXPECT(d.size() == 0u);
  EXPECT(it->hasNext() == false);
  EXPECT_THROWS(it->next());
}

CASE("datareader/transform") {
  auto paths = createTestDataNoPrefix<int>(
      {
          {"f0", 0}, {"f1", 1}, {"f2", 2}, {"f3", 3},
      });
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(paths.first); });

  auto reader = makeDataReader<int>(
      paths.second, 2, 3, [](std::vector<int> const& x) -> std::string {
        std::string res;
        for (auto& i : x) {
          res += std::to_string(i);
        }
        return res;
      });
  auto it = reader.iterator();

  EXPECT(it->hasNext() == true);
  auto d = it->next();
  EXPECT(d == "012");
  EXPECT((d = it->next(), true));
  EXPECT(d == "3");
  EXPECT(it->hasNext() == false);
  EXPECT_THROWS(it->next());
}

CASE("datareader/transform/manual") {
  auto paths = createTestDataNoPrefix<int>(
      {
          {"f0", 0}, {"f1", 1}, {"f2", 2}, {"f3", 3},
      });
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(paths.first); });

  auto it = std::make_unique<DataReaderIterator<int>>(
      paths.second, 2, 3, std::string());
  auto trit = makeDataReaderTransform(
      std::move(it), [](std::vector<int> const& x) -> std::string {
        std::string res;
        for (auto& i : x) {
          res += std::to_string(i);
        }
        return res;
      });

  EXPECT(trit->hasNext() == true);
  auto d = trit->next();
  EXPECT(d == "012");
  EXPECT((d = trit->next(), true));
  EXPECT(d == "3");
  EXPECT(!trit->hasNext());
  EXPECT_THROWS(trit->next());
}

CASE("datareader/init_fn") {
  auto paths = createTestDataNoPrefix<int>(
      {
          {"f0", 0}, {"f1", 1}, {"f2", 2}, {"f3", 3},
      });
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(paths.first); });

  std::atomic<int> numThreadsSpawned(0);

  auto reader = makeDataReader<int>(
      paths.second,
      2,
      3,
      [](std::vector<int> const& x) -> std::string {
        std::string res;
        for (auto& i : x) {
          res += std::to_string(i);
        }
        return res;
      },
      std::string(), // pathPrefix
      [&] { numThreadsSpawned++; });
  auto it = reader.iterator();

  EXPECT(it->hasNext() == true);
  auto d = it->next();
  EXPECT(d == "012");
  EXPECT((d = it->next(), true));
  EXPECT(d == "3");
  EXPECT(it->hasNext() == false);
  EXPECT_THROWS(it->next());

  // 3 threads expected: 2 reader threads, one transform
  EXPECT(numThreadsSpawned == 3);
}
