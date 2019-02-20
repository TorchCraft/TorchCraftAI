/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <algorithm>
#include <cstdlib>

#ifdef WITHOUT_POSIX
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif // WITHOUT_POSIX

namespace common {
template <typename T>
class CircularBuffer {
 public:
  CircularBuffer(size_t capacity) : pos_(-1), size_(0), max_(capacity) {
    buf_ = new T[capacity];
  }
  ~CircularBuffer() {
    delete[] buf_;
  }

  void push() {
    pos_ = (pos_ + 1) % max_;
    buf_[pos_] = T();
    size_ = std::min(size_ + 1, max_);
  }
  void push(T const& value) {
    pos_ = (pos_ + 1) % max_;
    buf_[pos_] = value;
    size_ = std::min(size_ + 1, max_);
  }
  void push(T& value) {
    pos_ = (pos_ + 1) % max_;
    buf_[pos_] = value;
    size_ = std::min(size_ + 1, max_);
  }

  size_t size() const {
    return size_;
  }

  // 0: get most recent element
  // <0: get past elements
  T const& at(ssize_t pos) const {
    pos = (pos + pos_) % max_;
    while (pos < 0) {
      pos = max_ + pos;
    }
    return buf_[pos];
  }
  T& at(ssize_t pos) {
    pos = (pos + pos_) % max_;
    while (pos < 0) {
      pos = max_ + pos;
    }
    return buf_[pos];
  }

 private:
  T* buf_;
  /*
   * max_ needs to be ssize_t for the module operator in at() to work correctly
   * with negative positions (which looks like a nice API). _size is >= 0 but
   * frequently compared to _max in std::min() which demands identical types for
   * its arguments.
   */
  ssize_t pos_, size_, max_;
};

} // namespace common
