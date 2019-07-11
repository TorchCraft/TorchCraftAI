/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "fsutils.h"

#include "assert.h"
#include "str.h"

#ifdef WITHOUT_POSIX

#ifdef _MSC_VER // Windows
#include <Windows.h>
#endif

#else
#include <common/checksum.h>
#include <common/language.h>
#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <glob.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif // WITHOUT_POSIX
#include <fmt/format.h>
#include <fstream>

namespace common {
namespace fsutils {

namespace {
std::string findTmpdir() {
  auto getTmp = [](char const* envVar, std::string& result) {
    char const* dir = getenv(envVar);
    result = dir == 0 ? "" : dir;
    return result;
  };
  std::string tmpDir;
  if (getTmp("TMPDIR", tmpDir).size() > 0) {
  } else if (getTmp("TMP", tmpDir).size() > 0) {
  } else if (getTmp("TEMP", tmpDir).size() > 0) {
  } else if (getTmp("TEMPDIR", tmpDir).size() > 0) {
  } else {
    tmpDir = "/tmp";
  }
  return tmpDir;
}
} // namespace

#ifdef WITHOUT_POSIX
#ifdef _MSC_VER // Windows
namespace {

void createDir(std::string const& path, int mode) {
  if (CreateDirectory(path.c_str(), NULL) ||
      ERROR_ALREADY_EXISTS == GetLastError()) {
  } else {
    throw std::system_error(GetLastError(), std::system_category());
  }
}

template <typename T>
void forEachInDir(std::string const& path, bool raiseErrors, T&& func) {
  throw std::exception("Not yet implemented");
}
} // namespace

std::string pwd() {
  throw std::exception("Not yet implemented");
}

std::string which(std::string const& executable) {
  throw std::exception("Not yet implemented");
}

void cd(std::string const& path) {
  throw std::exception("Not yet implemented");
}

bool exists(std::string const& path, int modeMask) {
  throw std::exception("Not yet implemented");
}

bool isdir(std::string const& path, int modeMask) {
  DWORD ftyp = GetFileAttributesA(path.c_str());
  if (ftyp == INVALID_FILE_ATTRIBUTES)
    return false; // something is wrong with the path

  return ftyp & FILE_ATTRIBUTE_DIRECTORY;
}

std::string mktempd(std::string const& prefix, std::string const& tmpdir) {
  throw std::exception("Not yet implemented");
}

std::string mktemp(std::string const& prefix, std::string const& tmpLoc) {
  throw std::exception("Not yet implemented");
}

void touch(std::string const& path) {
  throw std::exception("Not yet implemented");
}

void rmrf(std::string const& path) {
  throw std::exception("Not yet implemented");
}

std::vector<std::string> find(
    std::string const& path,
    std::string const& pattern) {
  throw std::exception("Not yet implemented");
}

std::vector<std::string> findr(
    std::string const& path,
    std::string const& pattern) {
  throw std::exception("Not yet implemented");
}

std::vector<std::string> glob(std::string const& pattern) {
  throw std::exception("Not yet implemented");
}
#endif // _MSC_VER

#else // WITHOUT_POSIX
namespace {

void createDir(std::string const& path, int mode) {
  if (::mkdir(path.c_str(), mode) < 0) {
    // Somebody might have created it in the meantime?
    if (!fsutils::isdir(path)) {
      throw std::system_error(errno, std::system_category());
    }
  }
}

template <typename T>
void forEachInDir(std::string const& path, bool raiseErrors, T&& func) {
  auto dir = ::opendir(path.c_str());
  if (dir == nullptr) {
    if (raiseErrors) {
      throw std::system_error(errno, std::system_category());
    } else {
      return;
    }
  }
  struct dirent entry;
  struct dirent* result;
  while (true) {
    if (::readdir_r(dir, &entry, &result) < 0) {
      if (raiseErrors) {
        throw std::system_error(errno, std::system_category());
      } else {
        continue;
      }
    }
    if (result == nullptr) {
      break;
    }
    if (!strcmp(result->d_name, ".") || !strcmp(result->d_name, "..")) {
      continue;
    }

    func(result);
  }

  if (::closedir(dir) != 0 && raiseErrors) {
    throw std::system_error(errno, std::system_category());
  }
}

} // namespace

std::string pwd() {
  char buf[MAXPATHLEN];
  if (::getcwd(buf, MAXPATHLEN) == nullptr) {
    throw std::system_error(errno, std::system_category());
  }
  return buf;
}

// Strongly motivated by FreeBSD's implementation:
// https://github.com/freebsd/freebsd/blob/master/usr.bin/which/which.c
std::string which(std::string const& executable) {
  auto isExecutable = [](char const* path) {
    if (::access(path, X_OK) != 0) {
      return false;
    }
    // The FreeBSD implementation mentions that there are possible false
    // positives with access(2) for the super user.
    struct stat buf;
    if (::stat(path, &buf) != 0) {
      return false;
    }
    if (!S_ISREG(buf.st_mode)) {
      return false;
    }
    if (getuid() == 0 && (buf.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0) {
      return false;
    }
    return true;
  };

  if (executable.find("/") != std::string::npos) {
    return isExecutable(executable.c_str()) ? executable : std::string();
  }

  char* pathEnv = getenv("PATH");
  if (pathEnv == nullptr) {
    return std::string();
  }
  auto elem = stringSplit(pathEnv, ':');
  for (auto const& dir : elem) {
    auto epath = dir + "/" + executable;
    if (isExecutable(epath.c_str())) {
      return epath;
    }
  }
  return std::string();
}

void cd(std::string const& path) {
  if (::chdir(path.c_str()) < 0) {
    throw std::system_error(errno, std::system_category());
  }
}

bool exists(std::string const& path, int modeMask) {
  struct stat buf;
  if (::stat(path.c_str(), &buf) < 0) {
    // Could also check errno values here...
    return false;
  }
  if (modeMask > 0 && (buf.st_mode & modeMask) == 0) {
    return false;
  }
  return true;
}

bool isdir(std::string const& path, int modeMask) {
  struct stat buf;
  if (::stat(path.c_str(), &buf) < 0) {
    // Could also check errno values here...
    return false;
  }
  if (!S_ISDIR(buf.st_mode)) {
    return false;
  }
  if (modeMask > 0 && (buf.st_mode & modeMask) == 0) {
    return false;
  }
  return true;
}
std::string mktempd(std::string const& prefix, std::string const& tmpdir) {
  auto tempDir = tmpdir.size() == 0 ? findTmpdir() : tmpdir;

  auto dirTemplate = tempDir + kPathSep + prefix + ".XXXXXX";
  char* buf = const_cast<char*>(dirTemplate.c_str());
  if (::mkdtemp(buf) == nullptr) {
    throw std::system_error(errno, std::system_category());
  }
  return dirTemplate;
}

std::string mktemp(std::string const& prefix, std::string const& tmpLoc) {
  // Look for a tmpdir
  //   ISO/IEC 9945 (POSIX): The path supplied by the first environment
  //   variable found in the list TMPDIR, TMP, TEMP, TEMPDIR.
  //   If none of these are found, "/tmp".
  auto tmpdir = tmpLoc.size() == 0 ? findTmpdir() : tmpLoc;
  auto tmpltString = fmt::format("{}{}{}.XXXXXX", tmpdir, kPathSep, prefix);

  // Create the socket
  char tmplt[tmpltString.size() + 1];
  std::memcpy(tmplt, tmpltString.c_str(), tmpltString.size() * sizeof(char));
  tmplt[tmpltString.size()] = 0;
  auto res = mkstemp(tmplt);
  if (res == -1) {
    throw std::system_error(errno, std::system_category());
  } else {
    if (close(res) == -1) {
      throw std::system_error(errno, std::system_category());
    }
  }
  return tmplt;
}

void touch(std::string const& path) {
  auto fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_NONBLOCK, 0666);
  if (fd < 0) {
    throw std::system_error(errno, std::system_category());
  }

  auto res = futimes(fd, nullptr);
  if (res < 0) {
    ::close(fd);
    throw std::system_error(errno, std::system_category());
  }
  ::close(fd);
}

void rmrf(std::string const& path) {
  if (!isdir(path)) {
    ::unlink(path.c_str());
    return;
  }

  forEachInDir(path, false, [&](struct dirent* ent) {
    auto entryPath = path + kPathSep + ent->d_name;
    if (ent->d_type == DT_DIR) {
      rmrf(entryPath);
    } else {
      ::unlink(entryPath.c_str());
    }
  });

  ::rmdir(path.c_str());
}

std::vector<std::string> find(
    std::string const& path,
    std::string const& pattern) {
  std::vector<std::string> matches;
  forEachInDir(path, false, [&](struct dirent* ent) {
    auto res = fnmatch(pattern.c_str(), ent->d_name, 0);
    if (res == 0) {
      auto entryPath = path + kPathSep + ent->d_name;
      matches.push_back(entryPath);
    } else if (res != FNM_NOMATCH) {
      throw std::system_error(errno, std::system_category());
    }
  });
  return matches;
}

std::vector<std::string> findr(
    std::string const& path,
    std::string const& pattern) {
  std::vector<std::string> matches;
  forEachInDir(path, false, [&](struct dirent* ent) {
    auto entryPath = path + kPathSep + ent->d_name;
    if (ent->d_type == DT_DIR) {
      auto subMatches = findr(entryPath, pattern);
      matches.insert(matches.end(), subMatches.begin(), subMatches.end());
    } else {
      auto res = fnmatch(pattern.c_str(), ent->d_name, 0);
      if (res == 0) {
        matches.emplace_back(entryPath);
      } else if (res != FNM_NOMATCH) {
        throw std::system_error(errno, std::system_category());
      }
    }
  });
  return matches;
}

std::vector<std::string> glob(std::string const& pattern) {
  glob_t buf;
  auto ret = glob(pattern.c_str(), GLOB_TILDE | GLOB_BRACE, nullptr, &buf);
  if (ret != 0) {
    globfree(&buf);
    throw std::system_error(errno, std::system_category());
  }

  std::vector<std::string> out;
  for (size_t i = 0; i < buf.gl_pathc; i++) {
    out.emplace_back(buf.gl_pathv[i]);
  }
  globfree(&buf);
  return out;
}

std::vector<unsigned char> md5(std::string const& path) {
  std::ifstream input(path);
  auto sz = size(path);

  auto fd = ::open(path.c_str(), O_RDONLY);
  auto guard = common::makeGuard([fd] { ::close(fd); });
  if (fd < 0) {
    throw std::system_error(errno, std::system_category());
  }

  auto fb = mmap(0, sz, PROT_READ, MAP_PRIVATE, fd, 0);
  if (fb == MAP_FAILED) {
    throw std::system_error(errno, std::system_category());
  }
  auto result = common::md5sum(fb, sz);
  ::munmap(fb, sz);

  return result;
}
#endif // WITHOUT_POSIX

std::string basename(std::string const& path, std::string const& ext) {
  // Find last non-separator
  auto end = path.find_last_not_of(kPathSep);
  if (end == std::string::npos) {
    end = 0;
  }
  // Find previous separator
  auto start =
      end == 0 ? std::string::npos : path.find_last_of(kPathSep, end - 1);
  // Extract basename component
  auto base =
      path.substr(start == std::string::npos ? 0 : start + 1, end - start);

  // Optionally strip extension
  if (ext.size() < base.size() &&
      base.compare(base.size() - ext.size(), ext.size(), ext) == 0) {
    base = base.substr(0, base.size() - ext.size());
  }
  return base;
}

std::string dirname(std::string const& path) {
  // Find last non-separator
  // Here, end is actually the character *after* the one we're looking for
  auto end = path.find_last_not_of(kPathSep);
  end = (end == std::string::npos ? 0 : end + 1);
  // Find previous separator
  if (end > 0) {
    end = path.find_last_of(kPathSep, end - 1);
    end = (end == std::string::npos ? 0 : end + 1);
  }
  // Skip all remaining separators
  if (end > 0) {
    end = path.find_last_not_of(kPathSep, end - 1);
    end = (end == std::string::npos ? 0 : end + 1);
  }

  // Return remaining portion. If empty, return "." or "/" depending on first
  // character in path.
  if (end > 0) {
    auto dir = path.substr(0, end);
    return (dir.empty() ? "." : dir);
  } else if (end == 0) {
    if (!path.empty() && path[0] == '/') {
      return "/";
    }
  }
  return ".";
}

void mkdir(std::string const& path, int mode) {
  if (path.empty() || path == "/" || path == "." || isdir(path)) {
    return;
  }
  auto parent = dirname(path);
  ASSERT(path != parent);
  mkdir(parent);
  createDir(path, mode);
}

void mv(std::string const& src, std::string const& dest) {
  if (!exists(src)) {
    throw std::runtime_error("File does not exist");
  }
  auto actualDest =
      fsutils::isdir(dest) ? dest + kPathSep + fsutils::basename(src) : dest;
  if (std::rename(src.c_str(), actualDest.c_str()) < 0) {
    // TODO handle cross device moves, common for us to move across NFS systems
    // This is simply a copy if we detect errno EXDEV
    throw std::system_error(errno, std::system_category());
  }
}

size_t size(std::string const& path) {
  struct stat buf;
  if (::stat(path.c_str(), &buf) != 0) {
    throw std::runtime_error("File does not exist!");
  }
  return buf.st_size;
}

std::chrono::system_clock::time_point mtime(std::string const& path) {
  struct stat buf;
  if (::stat(path.c_str(), &buf) != 0) {
    throw std::runtime_error("File does not exist!");
  }
  return std::chrono::system_clock::from_time_t(buf.st_mtime);
}

void writeLines(std::string const& path, std::vector<std::string> data) {
  std::ofstream os;
  os.open(path);
  for (auto& line : data) {
    os << line << std::endl;
  }
}

std::vector<std::string> readLines(std::string const& path) {
  return readLinesPartition(path, -1, -1);
}

std::vector<std::string>
readLinesPartition(std::string const& path, int partition, int numPartitions) {
  std::ifstream input(path);
  if (!input) {
    if (!fsutils::exists(path)) {
      throw std::runtime_error("No such file: " + path);
    } else {
      throw std::runtime_error("Error reading from file: " + path);
    }
  }

  std::vector<std::string> result;
  if (partition >= 0 && numPartitions > 0) {
    int i = 0;
    for (std::string line; std::getline(input, line);) {
      if (i++ % numPartitions == partition) {
        result.push_back(line);
      }
    }
  } else {
    for (std::string line; std::getline(input, line);) {
      result.push_back(line);
    }
  }
  return result;
}

} // namespace fsutils
} // namespace common
