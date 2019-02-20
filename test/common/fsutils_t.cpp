/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "utils.h"

#include <common/fsutils.h>

#include <fcntl.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <fstream>
#include <thread>

using namespace cherrypi;
using namespace common;

namespace {
char const* kNonExistentPath = "/proc/this/directory/should/not/exist";

auto pushEnv(char const* env, std::string const& value) {
  std::string oldVal;
  char* ptr = ::getenv(env);
  if (ptr != nullptr) {
    oldVal = ptr;
  }
  if (value.empty()) {
    ::unsetenv(env);
  } else {
    ::setenv(env, value.c_str(), 1);
  }
  return utils::makeGuard(
      [env = std::string(env), oldVal = std::move(oldVal)]() {
        if (!oldVal.empty()) {
          ::setenv(env.c_str(), oldVal.c_str(), 1);
        } else {
          ::unsetenv(env.c_str());
        }
      });
}
} // namespace

CASE("fsutils/cd_pwd") {
  auto home = ::getenv("HOME");
  auto curdir = fsutils::pwd();
  fsutils::cd(home);
  EXPECT(fsutils::pwd() == home);
  fsutils::cd(curdir);
  EXPECT(fsutils::pwd() == curdir);

  EXPECT_THROWS(fsutils::cd(kNonExistentPath));
  EXPECT(fsutils::pwd() == curdir);
}

CASE("fsutils/basename") {
  using fsutils::basename;
  EXPECT(basename("") == "");
  EXPECT(basename("/") == "/");
  EXPECT(basename("////") == "/");
  EXPECT(basename("/a") == "a");
  EXPECT(basename("////a") == "a");
  EXPECT(basename("/a/") == "a");
  EXPECT(basename("/a///") == "a");
  EXPECT(basename("///a///") == "a");
  EXPECT(basename("a///") == "a");
  EXPECT(basename("///bar///") == "bar");
  EXPECT(basename("/./a") == "a");
  EXPECT(basename("/.a") == ".a");
  EXPECT(basename(".///") == ".");
  EXPECT(basename("foo/bar") == "bar");
  EXPECT(basename("/foo/bar") == "bar");
  EXPECT(basename("foo////bar") == "bar");
  EXPECT(basename("//foo////bar") == "bar");
  EXPECT(basename("foo////bar/") == "bar");
  EXPECT(basename("foo////bar////") == "bar");
  EXPECT(basename("foo/bar.ext") == "bar.ext");
  EXPECT(basename("foo/bar.ext", ".ext") == "bar");
  EXPECT(basename("foo/bar.ext", "xt") == "bar.e");
  EXPECT(basename("foo/bar.ext", "bla") == "bar.ext");
  EXPECT(basename("foo/bar.ext", "ar.ext") == "b");
  EXPECT(basename("foo/bar.ext", "bar.ext") == "bar.ext");
  EXPECT(basename("foo/bar.ext/", ".ext") == "bar");
  EXPECT(basename("foo/bar.ext///", ".ext") == "bar");
  EXPECT(basename("/a/b/c/d/e/f/g/foo") == "foo");
}

CASE("fsutils/dirname") {
  using fsutils::dirname;
  EXPECT(dirname("") == ".");
  EXPECT(dirname("/") == "/");
  EXPECT(dirname("////") == "/");
  EXPECT(dirname("/a") == "/");
  EXPECT(dirname("////a") == "/");
  EXPECT(dirname("/a/") == "/");
  EXPECT(dirname("/a///") == "/");
  EXPECT(dirname("///a///") == "/");
  EXPECT(dirname("a///") == ".");
  EXPECT(dirname("///bar///") == "/");
  EXPECT(dirname("/./a") == "/.");
  EXPECT(dirname("/.a") == "/");
  EXPECT(dirname(".///") == ".");
  EXPECT(dirname("    a//") == ".");
  EXPECT(dirname("    /a//") == "    ");
  EXPECT(dirname("foo/bar") == "foo");
  EXPECT(dirname("foo////bar") == "foo");
  EXPECT(dirname("/foo/bar") == "/foo");
  EXPECT(dirname("////foo/bar") == "////foo");
  EXPECT(dirname("foo////bar///") == "foo");
  EXPECT(dirname("/a/b/c/d/e/f/g/foo") == "/a/b/c/d/e/f/g");
}

CASE("fsutils/which") {
  auto dir = fsutils::mktempd();
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(dir); });

  // bin1 is executable, bin2 is not
  fsutils::touch(dir + "/bin1");
  ::chmod((dir + "/bin1").c_str(), 0777);
  fsutils::touch(dir + "/bin2");
  ::chmod((dir + "/bin2").c_str(), 0666);

  // Full path always works
  EXPECT(fsutils::which(dir + "/bin1") == dir + "/bin1");
  // Not executable -> not found
  EXPECT(fsutils::which(dir + "/bin2") == std::string());

  {
    auto handle = pushEnv("PATH", "");
    // Not found with empty path
    EXPECT(fsutils::which("bin1") == std::string());
  }
  {
    auto handle = pushEnv("PATH", dir + ":/some/other/path:/foo/bar");
    EXPECT(fsutils::which("bin1") == dir + "/bin1");
    EXPECT(fsutils::which("bin2") == std::string());
  }
  {
    auto handle = pushEnv("PATH", "/some/other/path:/foo/bar:" + dir);
    EXPECT(fsutils::which("bin1") == dir + "/bin1");
  }
}

CASE("fsutils/exists") {
  // Assume POSIX
  EXPECT(fsutils::exists(kNonExistentPath) == false);
  EXPECT(fsutils::exists("/tmp") == true);
  EXPECT(fsutils::exists("/home") == true);
  EXPECT(fsutils::exists("/bin/sh") == true);
  EXPECT(fsutils::exists("/bin/truebla") == false);
}

CASE("fsutils/isdir") {
  // Assume POSIX
  EXPECT(fsutils::isdir(kNonExistentPath) == false);
  EXPECT(fsutils::isdir("/tmp") == true);
  EXPECT(fsutils::isdir("/home") == true);
  EXPECT(fsutils::isdir("/bin/true") == false);
  EXPECT(fsutils::isdir("/bin/truebla") == false);

  // Create a few not-so-usual files
  auto dir = fsutils::mktempd();
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(dir); });
  EXPECT(fsutils::isdir(dir) == true);
  EXPECT(fsutils::isdir(dir, S_IRWXU) == true);
  EXPECT(fsutils::isdir(dir, S_IRWXO) == false);

  int fd = ::open((dir + "/a").c_str(), O_RDWR | O_CREAT, 0600);
  EXPECT(fd >= 0);
  close(fd);
  EXPECT(fsutils::isdir(dir + "/a") == false);

  fd = ::open((dir + "/b").c_str(), O_RDWR | O_CREAT | O_NONBLOCK, 0600);
  EXPECT(fd >= 0);
  close(fd);
  EXPECT(fsutils::isdir(dir + "/b") == false);

  {
    int sd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    EXPECT(sd >= 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, (dir + "/c").c_str(), sizeof(addr.sun_path) - 1);
    int ret = ::bind(sd, (struct sockaddr*)&addr, sizeof(struct sockaddr_un));
    EXPECT(ret != -1);

    EXPECT(fsutils::isdir(dir + "/c") == false);
  }
}

CASE("fsutils/rmrf_simple") {
  // Set up some dummy directory structure. No special file things, no
  // undeletable things. Should be added for some more robustness.
  char buf[] = "/tmp/tmp.XXXXXX";
  EXPECT(::mkdtemp(buf) != nullptr);
  std::string sbuf(buf);
  for (auto d : {"/a", "/b", "/b/c", "/b/c/d"}) {
    EXPECT(::mkdir((sbuf + d).c_str(), 0777) == 0);
  }
  for (auto f : {"/a/f1", "/a/f2", "/a/a1", "/b/c/d/g1"}) {
    int fd = ::open((sbuf + f).c_str(), O_RDWR | O_CREAT, 0600);
    EXPECT(fd >= 0);
    close(fd);
  }
  EXPECT(::mkdir((sbuf + "/e").c_str(), 0777) == 0);

  // Bogus path -- no exception
  EXPECT((fsutils::rmrf("/proc/this/directory/should/not/exist"), true));

  // Single file
  fsutils::rmrf(sbuf + "/a/f1");
  EXPECT(fsutils::exists(sbuf + "/a/f1") == false);

  // Whole tree
  fsutils::rmrf(sbuf);
  for (auto d : {"/a", "/b", "/b/c", "/b/c/d", "/e"}) {
    EXPECT(fsutils::exists(sbuf + d) == false);
  }
  for (auto f : {"/a/f2", "/a/a1", "/b/c/d/g1"}) {
    EXPECT(fsutils::exists(sbuf + f) == false);
  }
}

CASE("fsutils/mktempd") {
  // When no TMPDIR, use /tmp
  {
    auto handle = pushEnv("TMPDIR", "");
    auto dir = fsutils::mktempd();
    EXPECT(dir.compare(0, 5, "/tmp/") == 0);
    EXPECT(dir.size() > 5u);
    fsutils::rmrf(dir);
  }

  // Otherwise, use TMPDIR
  {
    char buf[] = "/tmp/tmp.XXXXXX";
    EXPECT(::mkdtemp(buf) != nullptr);
    auto handle = pushEnv("TMPDIR", buf);
    auto dir = fsutils::mktempd();
    EXPECT(dir.compare(0, strlen(buf), buf) == 0);
    EXPECT(dir.size() > strlen(buf));
    fsutils::rmrf(buf);
  }

  // Or if an argument is passed, use that
  {
    char buf[] = "/tmp/tmp.XXXXXX";
    EXPECT(::mkdtemp(buf) != nullptr);
    auto dir = fsutils::mktempd("tmp", buf);
    EXPECT(dir.compare(0, strlen(buf), buf) == 0);
    EXPECT(dir.size() > strlen(buf));
    fsutils::rmrf(buf);
  }

  // If not able to create, throw since ::mkdtemp() will fail
  {
    auto handle = pushEnv("TMPDIR", kNonExistentPath);
    EXPECT_THROWS(fsutils::mktempd());
  }
}

CASE("fsutils/mktemp") {
  // When no TMPDIR, use /tmp
  {
    auto handle = pushEnv("TMPDIR", "");
    auto fn = fsutils::mktemp();
    EXPECT(fn.compare(0, 5, "/tmp/") == 0);
    EXPECT(fn.size() > 5u);
    EXPECT(fsutils::exists(fn));
    fsutils::rmrf(fn);
  }

  // Otherwise, use TMPDIR
  {
    char buf[] = "/tmp/tmp.XXXXXX";
    EXPECT(::mkdtemp(buf) != nullptr);
    auto handle = pushEnv("TMPDIR", buf);
    auto fn = fsutils::mktemp();
    EXPECT(fn.compare(0, strlen(buf), buf) == 0);
    EXPECT(fn.size() > strlen(buf));
    EXPECT(fsutils::exists(fn));
    fsutils::rmrf(buf);
  }
  {
    char buf[] = "/tmp/tmp.XXXXXX";
    EXPECT(::mkdtemp(buf) != nullptr);
    auto fn = fsutils::mktemp("name", buf);
    EXPECT(fn.compare(0, strlen(buf), buf) == 0);
    EXPECT(fn.size() > strlen(buf));
    EXPECT(fsutils::exists(fn));
    fsutils::rmrf(buf);
  }

  // If not able to create, throw since ::mkstemp() will fail
  {
    auto handle = pushEnv("TMPDIR", kNonExistentPath);
    EXPECT_THROWS(fsutils::mktemp());
  }
}

CASE("fsutils/mkdir") {
  auto dir = fsutils::mktempd();
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(dir); });

  // One level
  auto sdir1 = dir + "/dir1";
  fsutils::mkdir(sdir1);
  EXPECT(fsutils::isdir(sdir1));

  // Ending in slashes
  auto sdir2 = dir + "/dir2///";
  fsutils::mkdir(sdir2);
  EXPECT(fsutils::isdir(dir + "/dir2"));

  // Starting with slashes
  auto sdir3 = "/////" + dir + "/dir3";
  fsutils::mkdir(sdir3);
  EXPECT(fsutils::isdir(dir + "/dir3"));

  // Multiple levels
  auto sdir4 = dir + "/dir2/dir3/dir4/dir5/abcd efg hijok erere/here";
  fsutils::mkdir(sdir4);
  EXPECT(fsutils::isdir(sdir4));

  // syscall will fail
  EXPECT_THROWS(fsutils::mkdir(kNonExistentPath));
}

// This test is too flaky on CI
CASE("fsutils/touch[hide]") {
  auto dir = fsutils::mktempd();
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(dir); });

  EXPECT(!fsutils::exists(dir + "/file1"));
  fsutils::touch(dir + "/file1");
  EXPECT(fsutils::exists(dir + "/file1"));
  EXPECT_THROWS(fsutils::touch(dir + "/no/such/dir/file2"));

  // Access and modification time changed for file
  auto path2 = dir + "/file2";
  std::ofstream ofs(path2);
  ofs << "hello world";
  ofs.close();
  struct stat buf1, buf2;
  ::stat(path2.c_str(), &buf1);
  std::this_thread::sleep_for(std::chrono::milliseconds(2500));
  fsutils::touch(path2);
  ::stat(path2.c_str(), &buf2);
#if __APPLE__
  EXPECT(buf1.st_atimespec.tv_sec < buf2.st_atimespec.tv_sec);
  EXPECT(buf1.st_mtimespec.tv_sec < buf2.st_mtimespec.tv_sec);
#else // __APPLE__
  EXPECT(buf1.st_atim.tv_sec < buf2.st_atim.tv_sec);
  EXPECT(buf1.st_mtim.tv_sec < buf2.st_mtim.tv_sec);
#endif // __APPLE__

  // Access and modification time changed for directory
  auto path3 = dir + "/dir3";
  ::stat(path3.c_str(), &buf1);
  std::this_thread::sleep_for(std::chrono::milliseconds(2500));
  fsutils::touch(path3);
  ::stat(path3.c_str(), &buf2);
#if __APPLE__
  EXPECT(buf1.st_atimespec.tv_sec < buf2.st_atimespec.tv_sec);
  EXPECT(buf1.st_mtimespec.tv_sec < buf2.st_mtimespec.tv_sec);
#else // __APPLE__
  EXPECT(buf1.st_atim.tv_sec < buf2.st_atim.tv_sec);
  EXPECT(buf1.st_mtim.tv_sec < buf2.st_mtim.tv_sec);
#endif // __APPLE__
}

CASE("fsutils/find_findr") {
  auto dir = fsutils::mktempd();
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(dir); });

  fsutils::mkdir(dir + "/dir1/subdir1");
  fsutils::mkdir(dir + "/dir2");
  for (auto& name : {"file1", "file2", "other3"}) {
    fsutils::touch(dir + "/" + name);
    fsutils::touch(dir + "/dir1/" + name);
  }
  fsutils::touch(dir + "/dir1/subdir1/file4");
  fsutils::touch(dir + "/dir2/other5");

  auto sorted = [](std::vector<std::string> const& v) {
    auto a = v;
    std::sort(a.begin(), a.end());
    return a;
  };

  auto output1 = std::vector<std::string>{dir + "/file1", dir + "/file2"};
  EXPECT(sorted(fsutils::find(dir, "file*")) == sorted(output1));
  auto output2 = std::vector<std::string>{};
  EXPECT(fsutils::find(dir, "*nomatch*") == output2);
  auto output3 = std::vector<std::string>{dir + "/other3"};
  EXPECT(fsutils::find(dir, "other*") == output3);
  EXPECT(fsutils::find(dir, "other3") == output3);
  EXPECT(fsutils::find(dir, "other4") == output2);

  auto output4 = std::vector<std::string>{dir + "/dir1/file1",
                                          dir + "/dir1/file2",
                                          dir + "/dir1/subdir1/file4",
                                          dir + "/file1",
                                          dir + "/file2"};
  EXPECT(sorted(fsutils::findr(dir, "file*")) == sorted(output4));
  auto output5 = std::vector<std::string>{};
  EXPECT(fsutils::findr(dir, "*nomatch*") == output5);
  auto output6 = std::vector<std::string>{
      dir + "/dir1/other3", dir + "/dir2/other5", dir + "/other3"};
  EXPECT(sorted(fsutils::findr(dir, "other*")) == sorted(output6));
  auto output7 = std::vector<std::string>{dir + "/dir1/file1", dir + "/file1"};
  EXPECT(sorted(fsutils::findr(dir, "file1")) == sorted(output7));
}

CASE("fsutils/size") {
  auto dir = fsutils::mktempd();
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(dir); });
  auto path = dir + "/test";

  auto test = [&](size_t size) {
    {
      std::ofstream os;
      os.open(path, std::ios::binary);
      std::string output(size, '0');
      os << output;
    }
    EXPECT(fsutils::size(path) == size);
  };

  test(10);
  test(4096);
  test(0);
}

CASE("fsutils/mtime") {
  auto dir = fsutils::mktempd();
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(dir); });

  using namespace std::chrono_literals;
  using namespace std::chrono;
  auto path = dir + "/test";
  fsutils::touch(path);

  auto test = [&](auto interval) {
    auto now = system_clock::now();
    auto modtime = fsutils::mtime(path);
    auto ms =
        duration_cast<milliseconds>(modtime - now).count() - interval * 1000;
    return ms > 0 && ms < 1000;
  };

  std::this_thread::sleep_for(2s);
  test(2);
  std::this_thread::sleep_for(3s);
  test(5);
  std::this_thread::sleep_for(5s);
  test(10);
}

CASE("fsutils/mv") {
  auto dir = fsutils::mktempd();
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(dir); });

  auto f1 = dir + "/test";
  auto f2 = dir + "/test2";
  fsutils::touch(f1);
  fsutils::mv(f1, f2);
  EXPECT(!fsutils::exists(f1));
  EXPECT(fsutils::exists(f2));

  auto dir2 = fsutils::mktempd("tmp", dir);
  fsutils::mv(f2, dir2);
  EXPECT(!fsutils::exists(f2));
  EXPECT(fsutils::exists(dir2 + "/test2"));
}

CASE("fsutils/glob") {
  auto dir = fsutils::mktempd();
  auto cleanup = utils::makeGuard([&]() { fsutils::rmrf(dir); });
  fsutils::touch(dir + "/file1");
  fsutils::touch(dir + "/file2");
  fsutils::touch(dir + "/nope");
  fsutils::mkdir(dir + "/dir1");
  fsutils::touch(dir + "/dir1/file3");
  fsutils::mkdir(dir + "/dir1/sub1");
  fsutils::touch(dir + "/dir1/sub1/file4");

  {
    auto result = fsutils::glob(dir + "/file*");
    auto expected = std::vector<std::string>{dir + "/file1", dir + "/file2"};
    EXPECT(result == expected);
    auto result2 = fsutils::glob(dir + "/file[12]");
    EXPECT(result2 == expected);
    auto result3 = fsutils::glob(dir + "/file?");
    EXPECT(result3 == expected);
  }

  {
    auto result = fsutils::glob(dir + "/{.,dir1}/file*");
    auto expected = std::vector<std::string>{
        dir + "/./file1", dir + "/./file2", dir + "/dir1/file3"};
    EXPECT(result == expected);
  }

  {
    auto result = fsutils::glob(dir + "/dir*");
    auto expected = std::vector<std::string>{dir + "/dir1"};
    EXPECT(result == expected);
  }

  {
    auto result = fsutils::glob(dir + "/*/sub1/*");
    auto expected = std::vector<std::string>{dir + "/dir1/sub1/file4"};
    EXPECT(result == expected);
  }
}
