/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <streambuf>

// Place these here for convenience
#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/vector.hpp>

namespace common {

/**
 * A stream buffer for reading from a vector of bytes.
 * This can be used to construct a std::istream from a given binary blob as
 * follows:
 *
```
  std::vector<char> data = getDataFromSomewhere();
  IMembuf mbuf(data);
  std::istream is(&mbuf);
  // Extract data from istream as usual.
```
 */
class IMembuf : public std::streambuf {
 public:
  explicit IMembuf(std::vector<char> const& data);
};

/**
 * A stream buffer for writing to an accessible vector of bytes.
 * This can be used to construct a std::ostream as follows:
 *
```
  OMembuf mbuf;
  std::ostream os(&mbuf);
  // Write data to ostream as usual
  os.flush();
  auto& data = mbuf.data(); // Obtain data without extra copy
```
 */
class OMembuf : public std::streambuf {
 public:
  OMembuf() {}

  std::vector<char>& data();
  std::vector<char> takeData();

  using int_type = typename std::streambuf::int_type;
  virtual int_type overflow(int_type ch = traits_type::eof());

  virtual std::streamsize xsputn(char const* s, std::streamsize num);

 private:
  std::vector<char> buffer_;
};
} // namespace common
