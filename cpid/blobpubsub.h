/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <common/flags.h>

#include <zmq.hpp>

#include <future>
#include <mutex>
#include <string>
#include <vector>

namespace cpid {

/**
 * Publisher for ZeroMQ PUB-SUB pattern.
 *
 * This server will publish binary blobs at `endpoint()`. The last published
 * blob will be cached and re-published if new subscribers are joining.
 *
 * Published data consists of both a tag and binary data. The tag can be used to
 * disambiguiate blobs on the subscriber side but does not affect transport.
 */
class BlobPublisher final {
 public:
  enum DataFlags {
    None = 0,
    HasData = 1 << 0,
    NewData = 1 << 1,
  };

  BlobPublisher(
      std::string endpoint = std::string(),
      std::shared_ptr<zmq::context_t> context = nullptr);
  ~BlobPublisher();

  std::string endpoint() const;

  void publish(void const* data, size_t len, int64_t tag);
  void publish(std::vector<char>&& data, int64_t tag);

 private:
  void run(std::string endpoint, std::promise<std::string>&& endpointP);

  std::shared_ptr<zmq::context_t> context_;
  mutable std::string endpoint_;
  mutable std::future<std::string> endpointF_;
  mutable std::mutex endpointM_;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  int64_t tag_;
  std::vector<char> data_;
  DataFlags dflags_ = DataFlags::None;
  std::mutex dataM_;
  std::condition_variable dataCV_;
};

DEFINE_FLAG_OPERATORS(BlobPublisher::DataFlags);

/**
 * Subscriber for ZeroMQ PUB-SUB pattern.
 *
 * This client will subscribe to *one* of the BlobPublisher endpoints specified
 * and listen for incoming mesages. For each received blob, a user-defined
 * callback will be called (in the context of the dedicated listening thread).
 *
 * Note that due to last-value-caching, the callback might be called multiple
 * times for the same data and tag as they might be broadcasted multiple times.
 *
 * Changing the endpoints via `updateEndpoints()` will trigger endpoint
 * re-selection, which in turn might trigger re-subscription to a new publisher
 * endpoint and which in turn will trigger re-broadcasts.
 */
class BlobSubscriber final {
 public:
  using CallbackFn =
      std::function<void(void const* data, size_t len, int64_t tag)>;

 public:
  BlobSubscriber(
      CallbackFn callback,
      std::vector<std::string> endpoints,
      std::shared_ptr<zmq::context_t> context = nullptr);
  ~BlobSubscriber();

  void updateEndpoints(std::vector<std::string> endpoints);

 private:
  void listen();

  CallbackFn callback_;
  std::shared_ptr<zmq::context_t> context_;
  std::vector<std::string> endpoints_;
  std::mutex endpointsM_;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> endpointsChanged_{false};
};

} // namespace cpid
