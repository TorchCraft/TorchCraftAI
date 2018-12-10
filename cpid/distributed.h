/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <memory>

#include <c10d/ProcessGroup.hpp>
#include <c10d/Store.hpp>
#include <c10d/Types.hpp>

#include <gflags/gflags.h>

#include <autogradpp/autograd.h>

// Distributed logging utilities.
#define VLOG_MASTER(lvl) \
  VLOG_IF(lvl, cpid::distributed::globalContext()->rank == 0)
#define VLOG_ALL(lvl) \
  VLOG(lvl) << "w" << cpid::distributed::globalContext()->rank << ": "

namespace cpid {
namespace distributed {

using namespace ::c10d;
/**
 * This is a wrapper around ProcessGroup::Work.
 * We wait for the work to be finished on destruction, to ensure transfer
 * completes. Additionally, this subclass provides support for waiting on
 * multiple pieces of work to finish, in the case of syncing entire models.
 *
 * The comments for this class are copied from ProcessGroup::Work and may be out
 * of date when PyTorch updates...
 **/
class Context;
class Work {
 public:
  Work(std::function<void()> onFinish);
  Work(Work const&) = delete;
  Work(Work&&);
  ~Work();

  // Checks if request has completed. Non-blocking operation.
  bool isCompleted();

  // Returns if the work completed successfully.
  // If false, the exception function can be called to get details.
  bool isSuccess();

  // Ensures that operations on the output tensors that are invoked
  // after this function returns are correctly sequenced after the
  // asynchronous completion of this work.
  //
  // For CUDA tensors, it inserts stream synchronization such that
  // the streams of the caller wait for completion of the
  // asynchronous operations on the destination tensors.
  //
  // For CPU tensors, it is currently a nop.
  //
  // This function should only be used if the caller polls for
  // completion through the `isCompleted` function, it has returned
  // true, and the `isSuccess` function also has returned true.
  //
  void synchronize();

  // Waits until request completes. Blocking operation.
  // Returns false if the work completed with an exception.
  //
  // Functionally equivalent to:
  //
  //   while (!isCompleted()) { /* nop */ }
  //   auto success = isSuccess();
  //   if (success) { synchronize(); }
  //   return success;
  //
  bool wait();

  // Returns exception if wait() returned false.
  // This will return the first exception encountered.
  const std::exception& exception() const;

 private:
  Work() = default;
  Work(std::vector<std::shared_ptr<ProcessGroup::Work>>&&);
  void add(std::shared_ptr<ProcessGroup::Work>);
  void add(Work&&);

  std::vector<std::shared_ptr<ProcessGroup::Work>> works_;
  std::function<void()> onFinish_ = nullptr;
  friend class Context;
};

// Let's provide some type-safety. We can only send types that has a
// torch::Dtype
template <typename T>
using IsTorchDType =
    typename std::enable_if_t<std::is_same<T, uint8_t>::value || // Byte
                              std::is_same<T, char>::value || // Char
                              std::is_same<T, int8_t>::value || // Char
                              std::is_same<T, int16_t>::value || // Short
                              std::is_same<T, int32_t>::value || // Int
                              std::is_same<T, int64_t>::value || // Long
                              std::is_same<T, float>::value ||
                              std::is_same<T, double>::value>;

// The Context contains 2 instantiations of the C10D
// processgroup, and will automatically reroute tensors to NCCL or Gloo,
// depending on whether we use CPU or GPU
class Context {
 public:
  Context(std::shared_ptr<Store> store, int rank, int size);

  int rank;
  int size;

  template <typename T, IsTorchDType<T>* = nullptr>
  Work allreduce(T* ptr, int64_t s, ReduceOp = ReduceOp::SUM);
  template <typename T, IsTorchDType<T>* = nullptr>
  Work allreduce(std::vector<T>& v, ReduceOp = ReduceOp::SUM);
  Work allreduce(torch::Tensor, ReduceOp = ReduceOp::SUM);
  Work allreduceGradients(ag::Container const&, ReduceOp = ReduceOp::SUM);

  template <typename T, IsTorchDType<T>* = nullptr>
  Work broadcast(T* ptr, int64_t s, int root = 0);
  template <typename T, IsTorchDType<T>* = nullptr>
  Work broadcast(std::vector<T>& v, int root = 0);
  Work broadcast(torch::Tensor, int root = 0);
  Work broadcast(ag::Container const&, int root = 0);

  template <typename T, IsTorchDType<T>* = nullptr>
  Work allgather(T* out, T* in, int64_t s);
  template <typename T, IsTorchDType<T>* = nullptr>
  Work allgather(T* out, torch::Tensor in);
  Work allgather(torch::Tensor, torch::Tensor);

 private:
  std::shared_ptr<ProcessGroup> glooPG_;
  std::shared_ptr<ProcessGroup> ncclPG_;
  std::shared_ptr<ProcessGroup> devicePG(torch::Tensor x);
};

// Here are some functions that will automatically use the global context.
template <typename T, IsTorchDType<T>* = nullptr>
Work allreduce(T* ptr, int64_t s, ReduceOp = ReduceOp::SUM);
template <typename T, IsTorchDType<T>* = nullptr>
Work allreduce(std::vector<T>& v, ReduceOp = ReduceOp::SUM);
Work allreduce(torch::Tensor, ReduceOp = ReduceOp::SUM);
Work allreduceGradients(ag::Container const&, ReduceOp = ReduceOp::SUM);

template <typename T, IsTorchDType<T>* = nullptr>
Work broadcast(T* ptr, int64_t s, int root = 0);
template <typename T, IsTorchDType<T>* = nullptr>
Work broadcast(std::vector<T>& v, int root = 0);
Work broadcast(torch::Tensor, int root = 0);
Work broadcast(ag::Container const&, int root = 0);

template <typename T, IsTorchDType<T>* = nullptr>
Work allgather(T* out, T* in, int64_t s);
template <typename T, IsTorchDType<T>* = nullptr>
Work allgather(T* out, torch::Tensor in);
Work allgather(torch::Tensor, torch::Tensor);

void init();
/// Sets CUDA device to the local (if available) or MPI rank, both modulo the
/// number of available devices. Does nothing when no CUDA is avail.
/// init() calls setGPUToLocalRank() already, but since the result is
/// thread-local it's necessary to call it from any thread that is spawned
void setGPUToLocalRank();
std::shared_ptr<Context> globalContext();
} // namespace distributed
} // namespace cpid
