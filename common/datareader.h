/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "rand.h"
#include "serialization.h"
#include "zstdstream.h"

#include <glog/logging.h>

#include <chrono>
#include <future>
#include <map>
#include <queue>
#include <thread>

namespace common {

using DataReaderThreadInitF = std::function<void()>;
auto const DataReader_NoopF = [] {};

/**
 * A multi-threaded iterator that performs decerealization of objects and
 * returns data in batches.
 *
 * Here, batches means instances of std::vector<T>.
 *
 * The iterator will read data from all files specified by the list of paths
 * passed to the constructor *once*. The order of files is retained and will be
 * reflected in the resulting batches.
 *
 * Call `next()` to retrieve the next batch of data. It will block until enough
 * data is available to form a batch with the requested size. If the iterator
 * cannot advance anymore (`hasNext()` returns false), the call with throw an
 * exception. The last batch returned by this function may have less that
 * `batchSize` elements or be empty, depending on the actual data.
 *
 * For data files that cannot be decerealized (e.g. because the file cannot be
 * accessed, or because decerialization failed), the iterator will print a
 * message via glog but otherwise resume operation as usual.
 *
 * You will probably want to use this via DataReader<T>.
 */
template <typename T>
class DataReaderIterator {
  static size_t constexpr kMaxBatchesInQueue = 4;

 public:
  DataReaderIterator(
      std::vector<std::string> paths,
      size_t numThreads,
      size_t batchSize,
      std::string prefix = std::string(),
      DataReaderThreadInitF init = DataReader_NoopF)
      : paths_(std::move(paths)),
        prefix_(std::move(prefix)),
        batchSize_(batchSize),
        numThreads_(numThreads),
        init_(init) {
    pos_ = 0;
    threadPos_ = 0;
    maxQueueSize_ = kMaxBatchesInQueue * batchSize_;

    // Start reader threads
    for (size_t i = 0; i < numThreads_; i++) {
      threads_.emplace_back(&DataReaderIterator::read, this);
    }
  }

  ~DataReaderIterator();

  bool hasNext();

  std::vector<T> next();

 private:
  /// Read data from files (to be run in a thread)
  void read();

 private:
  std::vector<std::string> paths_;
  std::string prefix_;
  size_t batchSize_;
  size_t numThreads_;
  size_t pos_;
  size_t threadPos_;
  std::map<size_t, std::future<T>> dataQueue_; // key is offset in paths_
  size_t maxQueueSize_;
  std::mutex mutex_;
  std::condition_variable prodCV_;
  std::condition_variable consumerCV_;
  std::vector<std::thread> threads_;
  std::unordered_set<std::thread::id> threadsDone_;
  DataReaderThreadInitF init_;
}; // namespace common

/**
 * Wrapper for DataReaderIterator that applies an additional transform to the
 * resulting batches.
 *
 * The transform function will be run in a dedicated thread.
 */
template <typename T, typename F>
class DataReaderTransform {
 public:
  using Result = typename std::result_of<F(std::vector<T> const&)>::type;
  static size_t constexpr kMaxResultsInQueue = 4;

  DataReaderTransform(
      std::unique_ptr<DataReaderIterator<T>>&& it,
      F function,
      DataReaderThreadInitF init)
      : it_(std::move(it)), fn_(function), init_(init) {
    thread_ = std::thread(&DataReaderTransform::run, this);
  }

  ~DataReaderTransform();

  bool hasNext();

  Result next();

 private:
  void run();

 private:
  std::unique_ptr<DataReaderIterator<T>> it_;
  F fn_;
  DataReaderThreadInitF init_;
  std::queue<Result> queue_;
  std::mutex mutex_;
  std::condition_variable prodCV_;
  std::condition_variable consumerCV_;
  std::thread thread_;
  bool done_ = false;
};

template <typename T, typename F>
std::unique_ptr<DataReaderTransform<T, F>> makeDataReaderTransform(
    std::unique_ptr<DataReaderIterator<T>>&& it,
    F&& function,
    DataReaderThreadInitF init = DataReader_NoopF) {
  return std::make_unique<DataReaderTransform<T, F>>(
      std::move(it), function, init);
}

struct DataReader_NoTransform {};

/**
 * A multi-threaded reader for cerealized data.
 *
 * This class merely holds a list of paths pointing to files that contain
 * cerealized versions of T. zstd decompression will transparently work. The
 * actual multi-threaded data reading will happen in an iterator object that
 * can be obtained by calling `iterator()`.
 *
 * Optionally, a `pathPrefix` can be passed to the constructor which will be
 * prepended to every element in `paths` before accessing the respective file.
 *
 * If a transform function is provided, the iterator will run batches through
 * the function before returning them (in a dedicated thread).
 *
 * Usage example with 4 threads and batch size 32:
 ```
auto reader = makeDataReader<MyDatumType>(fileList, 4, 32);
while (training) {
  reader.shuffle();
  auto it = reader.iterator();
  while (it->hasNext()) {
    auto batch = it->next();
    // Do work
  }
}
 ```
 */
template <typename T, typename F = DataReader_NoTransform>
class DataReader {
 public:
  /// Please use `makeDataReader()` instead
  DataReader(
      std::vector<std::string> paths,
      size_t numThreads,
      size_t batchSize,
      std::string pathPrefix = std::string(),
      DataReaderThreadInitF init = DataReader_NoopF)
      : paths_(std::move(paths)),
        pathPrefix_(pathPrefix),
        batchSize_(batchSize),
        numThreads_(numThreads),
        init_(init) {}

  /// Please use `makeDataReader` instead
  DataReader(
      std::vector<std::string> paths,
      size_t numThreads,
      size_t batchSize,
      F transform,
      std::string pathPrefix = std::string(),
      DataReaderThreadInitF init = DataReader_NoopF)
      : paths_(std::move(paths)),
        pathPrefix_(pathPrefix),
        batchSize_(batchSize),
        numThreads_(numThreads),
        fn_(transform),
        init_(init) {}

  /// Shuffle the list of paths.
  void shuffle() {
    std::shuffle(
        paths_.begin(), paths_.end(), Rand::makeRandEngine<std::mt19937>());
  }

  /// Create an iterator that provides multi-threaded data access.
  /// This function will be available for data readers without transforms.
  template <typename FF = F>
  std::unique_ptr<DataReaderIterator<T>> iterator(
      typename std::enable_if_t<std::is_same<FF, DataReader_NoTransform>::value,
                                bool> = true) {
    return std::make_unique<DataReaderIterator<T>>(
        paths_, numThreads_, batchSize_, pathPrefix_, init_);
  }

  /// Create an iterator that provides multi-threaded data access.
  /// This function will be available for data readers with a transform.
  template <typename FF = F>
  std::unique_ptr<DataReaderTransform<T, F&>> iterator(
      typename std::enable_if_t<
          !std::is_same<FF, DataReader_NoTransform>::value,
          bool> = true) {
    return makeDataReaderTransform(
        std::make_unique<DataReaderIterator<T>>(
            paths_, numThreads_, batchSize_, pathPrefix_, init_),
        fn_,
        init_);
  }

 protected:
  std::vector<std::string> paths_;
  std::string pathPrefix_;
  size_t batchSize_;
  size_t numThreads_;
  F fn_;
  DataReaderThreadInitF init_;
};

template <typename T>
auto makeDataReader(
    std::vector<std::string> paths,
    size_t numThreads,
    size_t batchSize,
    std::string pathPrefix = std::string(),
    DataReaderThreadInitF init = DataReader_NoopF) {
  return DataReader<T>(paths, numThreads, batchSize, pathPrefix, init);
}

// Desperately waiting for C++17 so we get automatic class template deduction
template <typename T, typename F>
auto makeDataReader(
    std::vector<std::string> paths,
    size_t numThreads,
    size_t batchSize,
    F transform,
    std::string pathPrefix = std::string(),
    DataReaderThreadInitF init = DataReader_NoopF) {
  return DataReader<T, F>(
      paths, numThreads, batchSize, transform, pathPrefix, init);
}

/**************** IMPLEMENTATIONS ********************/

template <typename T>
DataReaderIterator<T>::~DataReaderIterator() {
  {
    // Move iterator to end and clear results queue so that threads can
    // finish  their current operation.
    std::lock_guard<std::mutex> lock(mutex_);
    maxQueueSize_ = std::max(maxQueueSize_, paths_.size() - threadPos_);
    dataQueue_.clear();
    threadPos_ = paths_.size();
  }
  prodCV_.notify_all();

  for (auto& thread : threads_) {
    thread.join();
  }
}

template <typename T>
bool DataReaderIterator<T>::hasNext() {
  std::unique_lock<std::mutex> lock(mutex_);
  return (!dataQueue_.empty() || threadPos_ < paths_.size());
}

template <typename T>
std::vector<T> DataReaderIterator<T>::next() {
  std::unique_lock<std::mutex> lock(mutex_);
  if (dataQueue_.empty() && threadPos_ >= paths_.size()) {
    throw std::runtime_error("Data iterator is already at end");
  }

  std::vector<T> batch;
  while (pos_ < paths_.size() && batch.size() < batchSize_) {
    auto curPos = pos_++;

    int numAttempts = 0;
    auto it = dataQueue_.end();

    consumerCV_.wait(lock, [&] {
      it = dataQueue_.find(curPos);
      if (it == dataQueue_.end()) {
        // The requested datum is not in the queue yet -- wait a while.
        // If we waited for a while already, increase queue size and hope
        // that we'll eventually get our requested datum's future.
        if ((++numAttempts % 5) == 0) {
          maxQueueSize_ *= 1.5;
        }
        return false;
      }

      return true;
    });

    try {
      batch.push_back(it->second.get());
    } catch (std::exception const& e) {
      LOG(WARNING) << "Cannot query result for datum " << pos_ << ", skipping ("
                   << e.what() << ")";
    }
    dataQueue_.erase(it);
  }

  // Back to normal
  maxQueueSize_ = kMaxBatchesInQueue * batchSize_;

  prodCV_.notify_all();
  return batch;
}

template <typename T>
void DataReaderIterator<T>::read() {
  init_();

  while (true) {
    size_t curPos;
    std::promise<T> dataPromise;
    bool done = false;

    { // Critical section interacting with producer and consumer queue
      std::unique_lock<std::mutex> lock(mutex_);
      while (true) {
        if (threadPos_ >= paths_.size()) {
          done = true;
          break;
        }
        if (dataQueue_.size() < maxQueueSize_) {
          curPos = threadPos_++;
          dataQueue_[curPos] = dataPromise.get_future();
          break;
        }

        // No space in queue, return to waiting. In case the specific datum in
        // question is blocked here, we notify the consumer (next()) to
        // provide them a chance for increasing the queue size
        consumerCV_.notify_one();

        prodCV_.wait(lock);
      }
    }
    if (done) {
      break;
    }

    std::string filePath;
    auto const& curPath = paths_[curPos];
    if (!prefix_.empty() && !curPath.empty() && curPath[0] != '/') {
      filePath = prefix_ + "/" + curPath;
    } else {
      filePath = curPath;
    }

    // Read data, fulfill promise from above
    try {
      VLOG(4) << "Reading data from " << filePath;
      zstd::ifstream is(filePath);
      cereal::BinaryInputArchive archive(is);
      T d;
      archive(d);
      dataPromise.set_value(std::move(d));
    } catch (std::exception const& e) {
      VLOG(0) << "Invalid data file " << filePath << ", skipping (" << e.what()
              << ")";
      try {
        dataPromise.set_exception(std::current_exception());
      } catch (std::exception const& e2) {
        LOG(ERROR) << "Cannot propagate exception: " << e2.what();
      }
    }

    // Use notify_all() for the (unlikely) case where multiple threads are
    // waiting in next().
    consumerCV_.notify_all();
  }

  std::unique_lock<std::mutex> lock(mutex_);
  threadsDone_.insert(std::this_thread::get_id());
}

template <typename T, typename F>
DataReaderTransform<T, F>::~DataReaderTransform() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    done_ = true;
  }
  prodCV_.notify_all();
  thread_.join();
}

template <typename T, typename F>
bool DataReaderTransform<T, F>::hasNext() {
  std::lock_guard<std::mutex> lock(mutex_);
  return !(queue_.empty() && done_);
}

template <typename T, typename F>
typename DataReaderTransform<T, F>::Result DataReaderTransform<T, F>::next() {
  std::unique_lock<std::mutex> lock(mutex_);
  if (queue_.empty() && done_) {
    throw std::runtime_error("Data iterator is already at end");
  }

  consumerCV_.wait(lock, [&] { return !queue_.empty(); });
  auto result = std::move(queue_.front());
  queue_.pop();
  prodCV_.notify_all();
  return result;
}

template <typename T, typename F>
void DataReaderTransform<T, F>::run() {
  init_();

  std::unique_lock<std::mutex> lock(mutex_);
  // We want this check to be locked so that when we are at the end of the
  // iterator, we do not leave the critical section before setting done_ to
  // true.
  while (it_->hasNext()) {
    lock.unlock();
    auto result = fn_(it_->next());
    lock.lock();

    prodCV_.wait(
        lock, [&] { return done_ || queue_.size() < kMaxResultsInQueue; });
    if (done_) {
      break;
    }

    queue_.push(std::move(result));
    consumerCV_.notify_one();
  }
  done_ = true;
}

} // namespace common
