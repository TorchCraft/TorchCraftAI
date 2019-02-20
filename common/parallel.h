/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <type_traits>

namespace common {

/**
 * A simple producer/consumer class.
 *
 * This class is dead-simple, but sometimes useful. You specify the element type
 * for the queue in the type, and then instantiate it with functor which will
 * run in a separate thread. The main function of the class is enqueue(), which,
 * well, adds stuff to the queue. You also specify a maximum queue size on
 * construction; if that size is reached, enqueue() will block.
 *
 * As a special case, you can use this class with 0 threads. This means that the
 * supplied functor will be called directly in the thread calling enqueue().
 * Items will be buffered implicitly by enqueue() blocking until consumption.
 *
 * If you want to wait for the consumers to finish, call wait(). If you want to
 * stop the consumer threads, destruct the object.
 *
 * The implementation assumes that objects of type T are in a valid state (i.e.
 * can be destructed) after moving. If that's not the case for your type, go fix
 * your type.
 */
template <typename T>
class BufferedConsumer {
  using Function = std::function<void(T)>;

 public:
  using type = T;

  BufferedConsumer(uint8_t nthreads, size_t maxQueueSize, Function&& fn);

  /// Stops the consumers, discarding any items in the queue
  ~BufferedConsumer();

  /// Blocks until the queue is empty or the consumers are stopped
  void wait();

  /// Adds another item to the work queue, possibly blocking
  /// If the number of threads is zero, execute directly in the calling thread's
  /// context (and thus block).
  void enqueue(T arg);

  void run();

 private:
  size_t const maxQueueSize_;
  bool stop_ = false;
  int64_t consuming_ = 0;
  Function fn_;
  std::vector<std::thread> threads_;
  std::queue<T> queue_;
  std::mutex mutex_;
  std::condition_variable itemReady_;
  std::condition_variable itemDone_;
};

/**
 * A simple producer class.
 *
 * You specify a function that will generate data for you somehow, ending when
 * it returns an optional without a value, and this producer will multithread it
 * for you automatically.  The function should be thread-safe, and data is not
 * guaranteed to arrive in the same order it was generated in, unless you do it
 * yourself.  If you want to stop the consumer threads, destruct the object.  If
 * you try destructing the object while get() is still being called, it will
 * result in a runtime error.
 */
template <typename T>
class BufferedProducer {
  using Function = std::function<std::optional<T>()>;

 public:
  using type = T;

  // Use uint8 because we don't expect more than 256 threads
  BufferedProducer(uint8_t nthreads, size_t maxQueueSize, Function&& fn);

  /// Stops the producers, discarding any items in the queue
  ~BufferedProducer();

  std::optional<T> get();

  void run(Function fn);

 private:
  size_t const maxQueueSize_;
  bool stop_ = false;
  int working_ = 0;
  uint8_t nThreads_;
  std::atomic_int running_;
  std::vector<std::thread> threads_;
  std::queue<std::future<std::optional<T>>> queue_;
  std::mutex mutex_;
  std::condition_variable queueCV_;
};

/************************ IMPLEMENTATION ***********************/

template <typename T>
BufferedConsumer<T>::BufferedConsumer(
    uint8_t nthreads,
    size_t maxQueueSize,
    Function&& fn)
    : maxQueueSize_(maxQueueSize), fn_(fn) {
  if (maxQueueSize_ == 0 && nthreads > 0) {
    throw std::runtime_error(
        "Cannot construct BufferedConsumer with > 0 threads but zero-sized "
        "queue");
  }
  for (auto i = nthreads; i > 0; i--) {
    threads_.emplace_back(&BufferedConsumer::run, this);
  }
}

/// Stops the consumers, discarding any items in the queue
template <typename T>
BufferedConsumer<T>::~BufferedConsumer() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  itemReady_.notify_all();
  itemDone_.notify_all();
  for (auto& th : threads_) {
    th.join();
  }
}

/// Blocks until the queue is empty or the consumers are stopped
template <typename T>
void BufferedConsumer<T>::wait() {
  std::unique_lock<std::mutex> lock(mutex_);
  itemDone_.wait(
      lock, [&] { return stop_ || (queue_.empty() && consuming_ == 0); });
}

template <typename T>
void BufferedConsumer<T>::enqueue(T arg) {
  if (!threads_.empty()) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      itemDone_.wait(
          lock, [&] { return stop_ || queue_.size() < maxQueueSize_; });
      if (stop_) {
        throw std::runtime_error("BufferedConsumer not active");
      }
      queue_.push(std::move(arg));
    }
    itemReady_.notify_one();
  } else {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (stop_) {
        throw std::runtime_error("BufferedConsumer not active");
      }
      consuming_++;
      fn_(std::move(arg));
      consuming_--;
    }
    itemDone_.notify_all();
  }
}

template <typename T>
void BufferedConsumer<T>::run() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (true) {
    itemReady_.wait(lock, [&] { return stop_ || !queue_.empty(); });
    if (stop_) {
      break;
    }
    if (queue_.empty()) {
      continue;
    }

    T item = std::move(queue_.front());
    queue_.pop();

    consuming_++;
    lock.unlock();
    fn_(std::move(item));
    lock.lock();
    consuming_--;

    // Only remove the item from the queue once it has been consumed
    // Ideally we'd do the notification without holding the lock, but doing so
    // we save one lock/unlock cycle. Let's trust implementations to recognize
    // this scenario (cf.
    // https://en.cppreference.com/w/cpp/thread/condition_variable/notify_one)
    itemDone_.notify_all();
  }
}

template <typename T>
BufferedProducer<T>::BufferedProducer(
    uint8_t nThreads,
    size_t maxQueueSize,
    Function&& fn)
    : maxQueueSize_(maxQueueSize), nThreads_(nThreads) {
  if (nThreads_ == 0) {
    throw std::runtime_error("Cannot use a buffered producer with no threads");
  }
  if (maxQueueSize == 0) {
    throw std::runtime_error(
        "Cannot consturct a BufferedProducer with 0 queue size");
  }
  for (auto i = nThreads; i > 0; i--) {
    threads_.emplace_back(&BufferedProducer::run, this, fn);
  }
  running_ = nThreads_;
}

/// Stops the producers, discarding any items in the queue
template <typename T>
BufferedProducer<T>::~BufferedProducer() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
    queueCV_.notify_all();
  }
  for (auto& th : threads_) {
    th.join();
  }
}

template <typename T>
std::optional<T> BufferedProducer<T>::get() {
  std::unique_lock<std::mutex> lock(mutex_);
  queueCV_.wait(
      lock, [&] { return stop_ || !queue_.empty() || running_ == 0; });
  if (stop_) {
    throw std::runtime_error("BufferedProducer not active");
  }
  if (running_ == 0 && queue_.empty()) {
    return std::optional<T>();
  }
  auto ret = queue_.front().get();
  queue_.pop();
  queueCV_.notify_all();
  return ret;
}

template <typename T>
void BufferedProducer<T>::run(Function fn) {
  while (true) {
    std::unique_lock<std::mutex> lock(mutex_);
    queueCV_.wait(lock, [&] {
      return stop_ || queue_.size() + working_ < maxQueueSize_;
    });
    if (stop_) {
      break;
    }

    std::promise<std::optional<T>> dataPromise;
    working_++;

    lock.unlock();
    auto result = fn();
    bool done = (!result.has_value());
    lock.lock();

    working_--;
    if (done) {
      running_--;
      queueCV_.notify_all();
      break;
    }
    dataPromise.set_value(std::move(result));
    queue_.push(dataPromise.get_future());
    queueCV_.notify_all();
  }
}
} // namespace common
