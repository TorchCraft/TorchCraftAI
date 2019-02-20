/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace common {

/**
 * Utility functions for interacting with the file system.
 *
 * These are a few simple syscall wrappers for file system interaction. The
 * design here follows a few simple rules:
 * - Short names that closely resemble well-known *nix shell commands, e.g.
 *   "rm -rf" becomes rmrf(), there's cd() etc.
 * - Simple interfaces in favor of micro-optimization. For example, there might
 *   be a future ls() call that returns a vector of strings rather than a file
 *   system iterator.
 * - Syscall errors result in exceptions (unless otherwise noted in the function
 *   documentation), so make sure to catch them if necessary (i.e. in core bot
 *   code).
 */
namespace fsutils {

#ifdef WITHOUT_POSIX
char constexpr kPathSep = '\\';
#else
char constexpr kPathSep = '/';
#endif

/// Returns the current working directory
std::string pwd();

/// Equivalent to basename(1)
std::string basename(
    std::string const& path,
    std::string const& ext = std::string());

/// Equivalent to dirname(1)
std::string dirname(std::string const& path);

/// Locates an executable on the system
std::string which(std::string const& executable);

/// Change working directory
void cd(std::string const& path);

/// Checks if a filesystem entry the given path exists.
/// If modeMask is non-zero, this function also checks if it applies to the
/// entry's permission bits.
bool exists(std::string const& path, int modeMask = 0);

/// Checks if the given path is a directory.
/// If modeMask is non-zero, this function also checks if it applies to the
/// directory's permission bits.
bool isdir(std::string const& path, int modeMask = 0);

/// Creates a directory at the given path.
/// If required, intermediate directories will be created.
void mkdir(std::string const& path, int mode = 0777);

/// Creates a directory at a suitable temporary location and returns its name.
/// Note: If tmpdir is not specified, OSX will always use /tmp because of the
/// long directory name sometimes causes problems...
std::string mktempd(
    std::string const& prefix = "tmp",
    std::string const& tmpdir = "");

/// Creates a file at a suitable temporary location and returns its name.
/// Note: OSX will always use /tmp because of the long directory name sometimes
/// causes problems...
std::string mktemp(
    std::string const& prefix = "cherrypi.tmp",
    std::string const& tmpdir = "");

/// Update file access and modification times.
/// This is a simplified version of touch(1) that changes both access and
/// modification time to "now".
void touch(std::string const& path);

/// Recursively remove a file system entry.
/// Roughly corresponds to rm -rf and thus also silently swallows any errors.
void rmrf(std::string const& path);

/// moves a file system entry.
/// Roughly corresponds to mv source dest
void mv(std::string const& source, std::string const& dest);

/// Find files matching a pattern (non-recursively).
std::vector<std::string> find(
    std::string const& path,
    std::string const& pattern);

/// Find files matching a pattern (recursively).
std::vector<std::string> findr(
    std::string const& path,
    std::string const& pattern);

/// File globbing
std::vector<std::string> glob(std::string const& pattern);

/// Get the size of the file in bytes
size_t size(std::string const& path);

/// Get the last modification time of the file
std::chrono::system_clock::time_point mtime(std::string const& path);

/// Reads data from a given path and splits it into separate lines.
std::vector<std::string> readLines(std::string const& path);

/// writes data to a given path in different lines
void writeLines(std::string const& path, std::vector<std::string>);

/// Reads data from a given path and splits it into separate lines.
/// This version will return a given partition of all lines. Specifically, it
/// returns every kth line for which `(k % numPartitions) == partition`.
std::vector<std::string>
readLinesPartition(std::string const& path, int partition, int numPartitions);

} // namespace fsutils
} // namespace common
