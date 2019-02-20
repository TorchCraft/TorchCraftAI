/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "distributed.h"

#include "common/utils.h"
#include "netutils.h"

#include <common/autograd/utils.h>

#include <fmt/format.h>
#include <glog/logging.h>

#include <c10d/FileStore.hpp>
#include <c10d/ProcessGroupGloo.hpp>
#include <c10d/ProcessGroupNCCL.hpp>
#include <c10d/TCPStore.hpp>
#include <gloo/transport/tcp/device.h>

DEFINE_int64(
    c10d_rank,
    -1,
    "Specify the c10d rank, -1 will autodetect based on SLURM environment "
    "variables");
DEFINE_int64(
    c10d_size,
    -1,
    "Specify the c10d world size, -1 will autodetect based on SLURM "
    "environment variables");
DEFINE_string(
    c10d_rdvu,
    "file",
    "file:[location]. "
    "Using file without a location causes it to try to autosetup based "
    "on slurm arguments");

namespace {
std::shared_ptr<cpid::distributed::Context> globalContext_;
std::once_flag contextInitialized_;
int cudaDeviceNumber = 0;
} // namespace

namespace cpid {
namespace distributed {

Work::Work(std::function<void()> onFinish) : onFinish_(onFinish){};

Work::~Work() noexcept(false) {
  try {
    wait();
    if (onFinish_) {
      onFinish_();
    }
  } catch (std::exception const& ex) {
    if (std::uncaught_exceptions() == 0) {
      throw;
    }
    LOG(WARNING) << "Detected exception during stack unwinding, ignoring: {}"
                 << ex.what();
  }
}

Work::Work(Work&& other) {
  std::swap(works_, other.works_);
  std::swap(onFinish_, other.onFinish_);
}

bool Work::isCompleted() {
  for (auto& work : works_) {
    if (!work->isCompleted()) {
      return false;
    }
  }
  return true;
}

bool Work::isSuccess() {
  for (auto& work : works_) {
    if (!work->isSuccess()) {
      return false;
    }
  }
  return true;
}

void Work::synchronize() {
  for (auto& work : works_) {
    work->synchronize();
  }
}

void Work::wait() {
  for (auto& work : works_) {
    if (!work->isCompleted()) {
      work->wait();
    }
  }
}

const std::exception_ptr Work::exception() const {
  for (auto& work : works_) {
    if (!work->isSuccess()) {
      return work->exception();
    }
  }
  LOG(FATAL)
      << "No exception found, perhaps your distributed operation did not fail?";
}

Work::Work(std::vector<std::shared_ptr<ProcessGroup::Work>>&& works)
    : works_(std::move(works)) {}

void Work::add(std::shared_ptr<ProcessGroup::Work> work) {
  works_.push_back(std::move(work));
}

void Work::add(Work&& other) {
  for (auto& work : other.works_) {
    works_.push_back(std::move(work));
  }
  other.works_.clear();
  auto func = [tof = this->onFinish_, oof = other.onFinish_]() {
    if (tof) {
      tof();
    }
    if (oof) {
      oof();
    }
  };
  onFinish_ = func;
}

void init() {
  auto initializer = [&]() {
    if (globalContext_ != nullptr) {
      return;
    }
    std::shared_ptr<Store> store;
    auto jobid = getenv("SLURM_JOB_ID");
    auto stepid = getenv("SLURM_STEPID");
    auto worldSize = getenv("SLURM_STEP_NUM_TASKS");
    if (jobid == nullptr || std::stoi(worldSize) == 1) {
      // If we're not on slurm, or if we only launch one task, we can just
      // use /tmp instead
      if (FLAGS_c10d_rank < 0) {
        FLAGS_c10d_rank = 0;
      }
      if (FLAGS_c10d_size < 0) {
        FLAGS_c10d_size = 1;
      }

      std::string rdvu;
      if (FLAGS_c10d_rdvu == "file") {
        if (FLAGS_c10d_size > 1) {
          throw std::runtime_error(
              "Cannot automatically determine rdvu without slurm");
        } else {
          // We don't depend on fsutils, so we copy over the implementation of
          // mktemp here for a random file to "do rdvu" on our single node
          char tmplt[] = "/tmp/c10d.rdvu.XXXXXX";
          auto res = ::mkstemp(tmplt);
          if (res == -1) {
            throw std::system_error(errno, std::system_category());
          } else {
            if (close(res) == -1) {
              throw std::system_error(errno, std::system_category());
            }
          }
          rdvu = tmplt;
        }
      } else {
        auto split = common::stringSplit(FLAGS_c10d_rdvu, ':', 1);
        if (split[0] != "file") {
          throw std::runtime_error("Unknown rendezvous method " + split[0]);
        }
        rdvu = std::move(split[1]);
      }
      cudaDeviceNumber = FLAGS_c10d_rank;
      VLOG(2) << "Using filestore at " << rdvu;
      store = std::make_shared<FileStore>(rdvu, FLAGS_c10d_size);
    } else {
      // If we're on slurm, automatically set rank and size based on slurm
      // variables
      if (FLAGS_c10d_rank < 0) {
        FLAGS_c10d_rank = std::stoi(getenv("SLURM_PROCID"));
      }
      if (FLAGS_c10d_size < 0) {
        FLAGS_c10d_size = std::stoi(worldSize);
      }

      // Setup the rendezvous.
      std::string rdvu;
      if (FLAGS_c10d_rdvu == "file") {
        rdvu = fmt::format("./c10d.{}.{}.sock", jobid, stepid);
      } else { // if it looks like file:/path/to/rdvu
        auto split = common::stringSplit(FLAGS_c10d_rdvu, ':', 1);
        if (split[0] != "file") {
          throw std::runtime_error("Unknown rendezvous method" + split[0]);
        }
        rdvu = split[1];
      }

      if (char const* localRank = ::getenv("SLURM_LOCALID")) {
        cudaDeviceNumber = std::stoi(localRank);
      }
      VLOG(2) << "Using filestore at " << rdvu;
      store = std::make_shared<FileStore>(rdvu, FLAGS_c10d_size);
    }
    store->setTimeout(std::chrono::seconds::zero());

    // Initialize the Process Groups
    // The destructor of the global context can conflict with the
    // de-initialization of the CUDA shared libraries, which can lead to
    // segmentation fault on exit. This code doesn't really leak any OS
    // resources like file descriptors, so we depend on the OS to clean up once
    // the process exits.
    globalContext_ = std::shared_ptr<Context>(
        new Context(store, FLAGS_c10d_rank, FLAGS_c10d_size), [](Context*) {});

    char hostname[256];
    gethostname(hostname, 255);
    VLOG(0) << "c10d rank: " << globalContext_->rank << " running on host "
            << hostname << " and size " << globalContext_->size;

    if (common::gpuAvailable() && torch::cuda::device_count() > 0) {
      cudaDeviceNumber = cudaDeviceNumber % torch::cuda::device_count();
    }
    setGPUToLocalRank();
  };

  std::call_once(contextInitialized_, initializer);
}

void setGPUToLocalRank() {
  if (common::gpuAvailable()) {
    cudaSetDevice(cudaDeviceNumber);
  }
}

std::shared_ptr<Context> globalContext() {
  init();
  return globalContext_;
}

#define FOR_ALL_TYPES(FUNC)     \
  FUNC(uint8_t, torch::kByte);  \
  FUNC(char, torch::kChar);     \
  FUNC(int8_t, torch::kChar);   \
  FUNC(int16_t, torch::kShort); \
  FUNC(int32_t, torch::kInt);   \
  FUNC(int64_t, torch::kLong);  \
  FUNC(float, torch::kFloat);   \
  FUNC(double, torch::kDouble);

std::shared_ptr<ProcessGroup> Context::devicePG(torch::Tensor x) {
  if (x.is_cuda())
    return ncclPG_;
  else
    return glooPG_;
}

#define ALLREDUCE(FType, DType)                                         \
  template <>                                                           \
  Work Context::allreduce<FType>(FType * ptr, int64_t s, ReduceOp op) { \
    auto tensor = torch::from_blob(ptr, {s}, DType);                    \
    return this->allreduce(tensor, op);                                 \
  }
FOR_ALL_TYPES(ALLREDUCE);
#undef ALLREDUCE

template <typename T, IsTorchDType<T>*>
Work Context::allreduce(std::vector<T>& v, ReduceOp op) {
  return this->allreduce(v.data(), v.size(), op);
}

Work Context::allreduce(torch::Tensor x, ReduceOp op) {
  if (size == 1) {
    return Work();
  }
  std::vector<torch::Tensor> tensors({x.detach()});
  return Work({devicePG(x)->allreduce(tensors, {op})});
}

Work Context::allreduceGradients(ag::Container const& model, ReduceOp op) {
  Work work;
  for (auto& p : model->parameters()) {
    if (p.grad().defined()) {
      work.add(this->allreduce(p.grad(), op));
    }
  }
  return work;
}

#define BROADCAST(FType, DType)                                      \
  template <>                                                        \
  Work Context::broadcast<FType>(FType * ptr, int64_t s, int root) { \
    auto tensor = torch::from_blob(ptr, {s}, DType);                 \
    return this->broadcast(tensor, root);                            \
  }
FOR_ALL_TYPES(BROADCAST)
#undef BROADCAST

template <typename T, IsTorchDType<T>*>
Work Context::broadcast(std::vector<T>& v, int root) {
  return this->broadcast(v.data(), v.size(), root);
}

Work Context::broadcast(torch::Tensor x, int root) {
  if (size == 1) {
    return Work();
  }
  std::vector<torch::Tensor> tensors({x.detach()});
  return Work({devicePG(x)->broadcast(tensors, {root, 0})});
}
Work Context::broadcast(ag::Container const& model, int root) {
  Work work;
  for (auto& p : model->parameters()) {
    work.add(this->broadcast(p, root));
  }
  return work;
}

#define ALLGATHER(FType, DType)                                        \
  template <>                                                          \
  Work Context::allgather<FType>(FType * out, FType * in, int64_t s) { \
    auto inTensor = torch::from_blob(in, {s}, DType);                  \
    auto outTensor = torch::from_blob(out, {this->size, s}, DType);    \
    return this->allgather(outTensor, inTensor);                       \
  }                                                                    \
  template <>                                                          \
  Work Context::allgather<FType>(FType * out, torch::Tensor in) {      \
    auto outTensor =                                                   \
        torch::from_blob(out, {this->size, in.numel()}, in.options()); \
    return this->allgather(outTensor, in);                             \
  }
FOR_ALL_TYPES(ALLGATHER)
#undef ALLGATHER

Work Context::allgather(torch::Tensor out, torch::Tensor in) {
  out = out.detach();
  if (globalContext()->size == 1) {
    out.copy_(in);
    return Work();
  }
  std::vector<torch::Tensor> tin({in.detach()});
  std::vector<std::vector<torch::Tensor>> tout;
  tout.emplace_back();
  for (auto i = 0; i < out.size(0); i++) {
    tout.back().emplace_back(out[i]);
  }
  return Work({devicePG(in)->allgather(tout, tin)});
}

Work Context::barrier() {
  Work work;
  work.add(glooPG_->barrier());
  return work;
}

Context::Context(
    std::shared_ptr<Store> store,
    int rank,
    int size,
    std::chrono::milliseconds timeout)
    : rank(rank),
      size(size),
      ncclPG_(std::make_shared<ProcessGroupNCCL>(store, rank, size)) {
  ProcessGroupGloo::Options opts;
  opts.timeout = timeout;
  auto addr = netutils::getInterfaceAddresses();
  opts.devices.emplace_back(
      gloo::transport::tcp::CreateDevice(addr.front().c_str()));
  glooPG_ = std::make_shared<ProcessGroupGloo>(store, rank, size, opts);
}

template <typename T, IsTorchDType<T>*>
Work allreduce(T* ptr, int64_t s, ReduceOp op) {
  return globalContext()->allreduce(ptr, s, op);
}
template <typename T, IsTorchDType<T>*>
Work allreduce(std::vector<T>& v, ReduceOp op) {
  return globalContext()->allreduce(v, op);
}

Work allreduce(torch::Tensor x, ReduceOp op) {
  return globalContext()->allreduce(x, op);
}
Work allreduceGradients(ag::Container const& x, ReduceOp op) {
  return globalContext()->allreduceGradients(x, op);
}

template <typename T, IsTorchDType<T>*>
Work broadcast(T* ptr, int64_t s, int root) {
  return globalContext()->broadcast(ptr, s, root);
}

template <typename T, IsTorchDType<T>*>
Work broadcast(std::vector<T>& v, int root) {
  return globalContext()->broadcast(v, root);
}

Work broadcast(torch::Tensor x, int root) {
  return globalContext()->broadcast(x, root);
}

Work broadcast(ag::Container const& x, int root) {
  return globalContext()->broadcast(x, root);
}

template <typename T, IsTorchDType<T>*>
Work allgather(T* out, T* in, int64_t s) {
  return globalContext()->allgather(out, in, s);
}
template <typename T, IsTorchDType<T>*>
Work allgather(T* out, torch::Tensor in) {
  return globalContext()->allgather(out, in);
}
Work allgather(torch::Tensor out, torch::Tensor in) {
  return globalContext()->allgather(out, in);
}

Work barrier() {
  return globalContext()->barrier();
}

#define FORCE_INSTANTIATION(T, TORCH_TYPE)                 \
  template Work allreduce(T* ptr, int64_t s, ReduceOp op); \
  template Work allreduce(std::vector<T>& v, ReduceOp op); \
  template Work broadcast(T* ptr, int64_t s, int root);    \
  template Work broadcast(std::vector<T>& v, int root);    \
  template Work allgather(T* out, T* in, int64_t s);       \
  template Work allgather(T* out, torch::Tensor in);

FOR_ALL_TYPES(FORCE_INSTANTIATION);

} // namespace distributed
} // namespace cpid
