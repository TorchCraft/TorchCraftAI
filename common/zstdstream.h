/*
 * Copyright (c) 2015-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 *
 * STL stream classes for Zstd compression and decompression.
 * Partially inspired by https://github.com/mateidavid/zstr.
 */

#pragma once

#include <cassert>
#include <fstream>
#include <stdexcept>
#include <vector>

// Required to access ZSTD_isFrame()
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

#include "circularbuffer.h"

namespace common {
namespace zstd {

/**
 * Custom exception for zstd error codes
 */
class exception : public std::exception {
 public:
  explicit exception(int code);
  const char* what() const throw();

 private:
  std::string msg_;
};

inline size_t check(size_t code) {
  if (ZSTD_isError(code)) {
    throw exception(code);
  }
  return code;
}

/**
 * Provides stream compression functionality
 */
class cstream {
 public:
  static constexpr int defaultLevel = 5;

  cstream();
  ~cstream();

  size_t init(int level = defaultLevel);
  size_t compress(ZSTD_outBuffer* output, ZSTD_inBuffer* input);
  size_t flush(ZSTD_outBuffer* output);
  size_t end(ZSTD_outBuffer* output);

 private:
  ZSTD_CStream* cstrm_;
};

/**
 * Provides stream decompression functionality
 */
class dstream {
 public:
  dstream();
  ~dstream();

  size_t decompress(ZSTD_outBuffer* output, ZSTD_inBuffer* input);

 private:
  ZSTD_DStream* dstrm_;
};

/**
 * Zstd stream buffer for compression. Data is written in a single big frame.
 */
class ostreambuf : public std::streambuf {
 public:
  explicit ostreambuf(std::streambuf* sbuf, int level = cstream::defaultLevel);
  virtual ~ostreambuf();

  using int_type = typename std::streambuf::int_type;
  virtual int_type overflow(int_type ch = traits_type::eof());
  virtual int sync();

 private:
  ssize_t compress(size_t pos);

  std::streambuf* sbuf_;
  int clevel_;
  cstream strm_;
  std::vector<char> inbuf_;
  std::vector<char> outbuf_;
  size_t inhint_;
  bool strInit_;
};

/**
 * Zstd stream buffer for decompression. If input data is not compressed, this
 * stream will simply copy it.
 */
class istreambuf : public std::streambuf {
 public:
  explicit istreambuf(std::streambuf* sbuf);

  virtual std::streambuf::int_type underflow();

 private:
  std::streambuf* sbuf_;
  dstream strm_;
  std::vector<char> inbuf_;
  std::vector<char> outbuf_; // only needed if actually compressed
  size_t inhint_;
  size_t inpos_ = 0;
  size_t inavail_ = 0;
  bool detected_ = false;
  bool compressed_ = false;
};

// Input stream for Zstd-compressed data
class istream : public std::istream {
 public:
  istream(std::streambuf* sbuf);

  virtual ~istream();
};

/**
 * Output stream for Zstd-compressed data
 */
class ostream : public std::ostream {
 public:
  ostream(std::streambuf* sbuf);
  virtual ~ostream();
};

/**
 * This class enables [io]fstream below to inherit from [io]stream (required for
 * setting a custom streambuf) while still constructing a corresponding
 * [io]fstream first (required for initializing the Zstd streambufs).
 */
template <typename T>
class fsholder {
 public:
  explicit fsholder(
      const std::string& path,
      std::ios_base::openmode mode = std::ios_base::out)
      : fs_(path, mode) {}

 protected:
  T fs_;
};

/**
 * Output file stream that writes Zstd-compressed data
 */
class ofstream : private fsholder<std::ofstream>, public std::ostream {
 public:
  explicit ofstream(
      const std::string& path,
      std::ios_base::openmode mode = std::ios_base::out);
  virtual ~ofstream();

  virtual operator bool() const;
  void close();
};

/**
 * Input file stream for Zstd-compressed data
 */
class ifstream : private fsholder<std::ifstream>, public std::istream {
 public:
  explicit ifstream(
      const std::string& path,
      std::ios_base::openmode mode = std::ios_base::in);

  virtual ~ifstream();
  operator bool() const;
  void close();
};

} // namespace zstd
} // namespace common
