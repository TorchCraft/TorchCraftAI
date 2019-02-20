/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "serialization.h"

namespace common {

IMembuf::IMembuf(std::vector<char> const& data) {
  char* p(const_cast<char*>(data.data()));
  setg(p, p, p + data.size());
}

IMembuf::IMembuf(std::string_view sv) {
  char* p(const_cast<char*>(sv.data()));
  setg(p, p, p + sv.size());
}

std::vector<char>& OMembuf::data() {
  return buffer_;
}

std::vector<char> OMembuf::takeData() {
  std::vector<char> newBuf;
  std::swap(newBuf, buffer_);
  return newBuf;
}

OMembuf::int_type OMembuf::overflow(int_type ch) {
  if (ch != traits_type::eof()) {
    buffer_.push_back(static_cast<char>(ch));
  }
  return ch;
}

std::streamsize OMembuf::xsputn(char const* s, std::streamsize num) {
  buffer_.insert(buffer_.end(), s, s + num);
  return num;
}

} // namespace common
