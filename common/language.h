/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace common {

template <typename Enumeration>
auto enumAsInt(Enumeration const value) ->
    typename std::underlying_type<Enumeration>::type {
  return static_cast<typename std::underlying_type<Enumeration>::type>(value);
}

// From https://gist.github.com/mrts/5890888, which is based on Alex Andrescu's
// implementation at http://bit.ly/2wfJnWn.
template <class Function>
class ScopeGuard {
 public:
  ScopeGuard(Function f) : guardFunction_(std::move(f)), active_(true) {}

  ~ScopeGuard() {
    if (active_) {
      guardFunction_();
    }
  }

  ScopeGuard(ScopeGuard&& rhs)
      : guardFunction_(std::move(rhs.guardFunction_)), active_(rhs.active_) {
    rhs.dismiss();
  }

  void dismiss() {
    active_ = false;
  }

 private:
  Function guardFunction_;
  bool active_;

  ScopeGuard() = delete;
  ScopeGuard(const ScopeGuard&) = delete;
  ScopeGuard& operator=(const ScopeGuard&) = delete;
};

template <class Function>
ScopeGuard<Function> makeGuard(Function f) {
  return ScopeGuard<Function>(std::move(f));
}

template <class Function>
class [[nodiscard]] TimeoutGuard {
  using clock = std::chrono::steady_clock;

 public:
  TimeoutGuard(Function f, std::chrono::milliseconds duration)
      : duration_(duration), start_(clock::now()), done_(false) {
    timeoutHelper_ =
        std::thread([this, f = std::move(f), until = clock::now() + duration] {
          while (!this->done_.load()) {
            std::unique_lock<std::mutex> lk(this->mut_);
            this->cv_.wait_until(lk, until);
            if (this->done_.load()) {
              return;
            } else if (clock::now() > until) {
              f();
              return;
            }
          }
        });
  }

  ~TimeoutGuard() {
    done_.store(true);
    cv_.notify_all();
    timeoutHelper_.join();
  }

 private:
  std::chrono::milliseconds duration_;
  clock::time_point start_;
  std::condition_variable cv_;
  std::mutex mut_;
  std::thread timeoutHelper_;
  std::atomic_bool done_;

  TimeoutGuard() = delete;
  TimeoutGuard(TimeoutGuard && rhs) = delete;
  TimeoutGuard(const TimeoutGuard&) = delete;
  TimeoutGuard& operator=(const TimeoutGuard&) = delete;
};

template <typename K, typename V>
class LRUCache {
  // store keys of cache
  std::list<K> dq_;

  // store references of key in cache
  std::unordered_map<
      K,
      std::pair<typename std::list<K>::iterator, std::unique_ptr<V>>>
      map_;

  size_t csize_; // maximum capacity of cache

 public:
  LRUCache(int n) : csize_(n) {}

  inline V* put(K k, std::unique_ptr<V>&& v) {
    if (map_.find(k) == map_.end()) {
      // Not in cache, cache size too big
      if (dq_.size() == csize_) {
        map_.erase(dq_.back());
        dq_.pop_back();
      }
    } else {
      dq_.erase(map_[k].first);
    }

    dq_.push_front(k);
    map_[k] = std::make_pair(dq_.begin(), std::move(v));
    return map_[k].second.get();
  }

  inline V* get(K const& k) {
    if (map_.find(k) == map_.end()) {
      return nullptr;
    } else {
      // Move list node to front
      auto& it = map_[k].first;
      dq_.splice(dq_.begin(), dq_, it);
      return map_[k].second.get();
    }
  }
};

} // namespace common

#define MAKE_STRING1(x) #x
#define MAKE_STRING(x) MAKE_STRING1(x)
