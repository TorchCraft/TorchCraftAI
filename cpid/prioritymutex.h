/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#include <array>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <torch/torch.h>

namespace cpid {

/// This exactly a unique lock that doesn't unlock on delete
template <typename Mutex_t>
class permanent_lock {
  Mutex_t* m = nullptr;
  bool locked = false;

 public:
  permanent_lock() = default;
  explicit permanent_lock(Mutex_t& m_) : m(&m_), locked(true) {
    m->lock();
  }
  permanent_lock(const permanent_lock&) = delete;
  permanent_lock& operator=(const permanent_lock&) = delete;
  ~permanent_lock() {}

  void operator=(permanent_lock&& o);
  void lock();
  void unlock();
  bool owns_lock();
};

/** This class implements a mutex that offers some control over the
 * priority of the waiting threads.
 * If several threads are waiting to obtain the lock, it is guaranteed that the
 * one with the highest priority will get it first. If there are several threads
 * with the same priority level, then the outcome is up to the pthread
 * implementation. Note that if there are always high priority threads in the
 * queue, it will create starvation on the lower priority ones.
 * If used inside a lock from the standard library, this will default to locking
 * with the lowest priority
 */
class priority_mutex {
 public:
  /// Constructs a mutex
  /// @param maxPrio is the maximal priority level accepted
  priority_mutex(int maxPrio) : maxPrio_(maxPrio) {
    queueCount_.resize(maxPrio_ + 1, 0);
  }
  priority_mutex(const priority_mutex&) = delete;
  priority_mutex& operator=(const priority_mutex&) = delete;
  priority_mutex(priority_mutex&&) = delete;

  void lock(int prio = 0) {
    if (prio < 0 || prio > maxPrio_) {
      throw std::runtime_error("Invalid priority level");
    }
    {
      std::lock_guard lock(queueMutex_);
      queueCount_[prio]++;
    }
    permanent_lock<std::mutex> lock(dataMutex_);
    queueCV_.wait(lock, [this, prio]() { return canGo(prio); });

    {
      std::lock_guard lock(queueMutex_);
      queueCount_[prio]--;
    }
  }

  bool try_lock(int prio = 0) {
    if (prio < 0 || prio > maxPrio_) {
      throw std::runtime_error("Invalid priority level");
    }
    // we can ignore the prio, because if there is a thread holding the
    // dataMutex, we can't lock, no matter the priority
    return dataMutex_.try_lock();
  }

  void unlock() {
    dataMutex_.unlock();
    queueCV_.notify_all();
  }

 protected:
  bool canGo(int prio) {
    std::lock_guard lock(queueMutex_);
    for (int i = prio + 1; i <= maxPrio_; ++i) {
      if (queueCount_[i] != 0)
        return false;
    }
    return true;
  }

  std::mutex queueMutex_, dataMutex_;
  std::condition_variable_any queueCV_;
  std::vector<int> queueCount_;

  int maxPrio_;
};

/// This is exactly an unique_lock without automatic lock, except that the lock
/// functions accepts a priority
class priority_lock {
  priority_mutex* m = nullptr;
  bool locked = false;
  int default_prio_ = 0;

 public:
  priority_lock() = default;
  explicit priority_lock(priority_mutex& m, int default_prio = 0)
      : m(&m), locked(false), default_prio_(default_prio) {}
  priority_lock(const priority_lock&) = delete;
  priority_lock& operator=(const priority_lock&) = delete;

  ~priority_lock() {
    if (locked) {
      m->unlock();
    }
  }

  void operator=(priority_lock&& o) {
    if (owns_lock())
      m->unlock();
    m = o.m;
    locked = o.locked;
    o.m = nullptr;
    o.locked = false;
  }

  void lock(int prio) {
    m->lock(prio);
    locked = true;
  }

  void lock() {
    lock(default_prio_);
  }

  void unlock() {
    m->unlock();
    locked = false;
  }

  bool try_lock(int prio) {
    if (m->try_lock(prio)) {
      locked = true;
      return true;
    }
    return false;
  }

  bool owns_lock() {
    return locked;
  }
};

template <typename Mutex_t>
void permanent_lock<Mutex_t>::operator=(permanent_lock&& o) {
  if (owns_lock())
    m->unlock();
  m = o.m;
  locked = o.locked;
  o.m = nullptr;
  o.locked = false;
}

template <typename Mutex_t>
void permanent_lock<Mutex_t>::lock() {
  m->lock();
  locked = true;
}

template <typename Mutex_t>
void permanent_lock<Mutex_t>::unlock() {
  m->unlock();
  locked = false;
}

template <typename Mutex_t>
bool permanent_lock<Mutex_t>::owns_lock() {
  return locked;
}

} // namespace cpid
