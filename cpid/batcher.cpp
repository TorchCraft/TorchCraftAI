/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "batcher.h"
#include "common/autograd.h"
#include <fmt/format.h>
#include <shared_mutex>

namespace cpid {

AsyncBatcher::AsyncBatcher(
    ag::Container model,
    int batchSize,
    int padValue,
    bool stripOutput,
    double stripValue)
    : model_(model),
      consumeThreadStarted_(false),
      padValue_(padValue),
      stripOutput_(stripOutput),
      stripValue_(stripValue),
      accessMutex_(1) {
  // If batch_size < 0, we don't start batching now
  // This prevents race conditions for inherited classes,
  // because we want the object to be fully constructed before batching anything
  if (batchSize > 0) {
    startBatching(batchSize);
  }
}

AsyncBatcher::~AsyncBatcher() {
  stopBatching();
}

void AsyncBatcher::startBatching(int batchSize) {
  if (consumeThreadStarted_) {
    throw std::runtime_error("Consumer thread already started");
  }
  if (batchSize < 1) {
    throw std::runtime_error("Batchsize must be at least 1");
  }
  batchSize_ = batchSize;
  consumeThreadStarted_ = true;
  shouldStop_.store(false);
  consumeThread_ = std::thread(&AsyncBatcher::consumeThread, this);
}

void AsyncBatcher::stopBatching() {
  if (consumeThread_.joinable()) {
    shouldStop_.store(true);
    batchReadyCV_.notify_all();
    consumeThread_.join();
    consumeThreadStarted_ = false;
  }
}

ag::Variant AsyncBatcher::batchedForward(ag::Variant state) {
  if (!consumeThreadStarted_) {
    throw std::runtime_error(
        "Can't batch forwards if the consume thread is not started. Call "
        "startBatching() first");
  }

  auto myPromise = std::make_shared<std::promise<ag::Variant>>();
  {
    // will lock with lowest priority
    std::unique_lock<priority_mutex> accessLock(accessMutex_);
    while (true) {
      if (!shouldConsume()) // if the batch is not full, we can proceed with
        // insertion
        break;
      // if the batch is full, we shouldn't queue more items
      // give the consumer thread a chance to get the lock
      accessLock.unlock();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      accessLock.lock();
    }

    replies_.push_back(myPromise);
    queries_.emplace_back(std::move(state));

    if (replies_.size() != queries_.size()) {
      LOG(FATAL) << "Size mismatch between replies (" << replies_.size()
                 << ") and queries(" << queries_.size() << ")";
    }

    if (shouldConsume()) {
      // when we hit the last element of the batch, we can send a notification
      batchReadyCV_.notify_all();
    }
  }

  ag::Variant reply = myPromise->get_future().get();
  return reply;
}

bool AsyncBatcher::shouldConsume() {
  return int(queries_.size()) >= batchSize_;
}

void AsyncBatcher::consumeThread() {
  while (true) {
    // create the lock, but doesn't actually lock
    priority_lock accessLock(accessMutex_, 1);
    accessLock.lock();

    // wait for the batch to be ready
    if (!batchReadyCV_.wait_for(
            accessLock, std::chrono::milliseconds(200), [this]() {
              return shouldStop_.load() || shouldConsume();
            })) {
      if (queries_.size() > 0) {
        // Carry on with this incomplete forward
        VLOG(3) << "Doing incomplete forward";
      } else {
        // No queries, nothing to do for now
        continue;
      }
    }
    if (shouldStop_.load()) {
      return;
    }

    try {
      ag::Variant input = this->makeBatch(queries_);
      ag::Variant out;
      {
        torch::NoGradGuard g_;
        std::shared_lock modelLock(modelMutex_);
        out = model_->forward(input);
      }

      auto replies_values = this->unBatch(out);

      if (replies_.size() != replies_values.size()) {
        LOG(FATAL) << "The batch size of the reply (" << replies_values.size()
                   << ") doesn't match the expected batch size ("
                   << replies_.size() << ")";
      }

      for (size_t i = 0; i < replies_.size(); ++i) {
        replies_[i]->set_value(std::move(replies_values[i]));
      }
    } catch (...) {
      for (size_t i = 0; i < replies_.size(); ++i) {
        replies_[i]->set_exception(std::current_exception());
      }
    }

    // batch consumed, we can mark this one as done
    replies_.clear();
    queries_.clear();

    accessLock.unlock();
  }
}

std::vector<ag::Variant> AsyncBatcher::unBatch(const ag::Variant& out) {
  return unBatch(out, stripOutput_, stripValue_);
}

std::vector<ag::Variant> AsyncBatcher::unBatch(
    const ag::Variant& o,
    bool stripOutput,
    double stripValue) {
  return common::unBatchVariant(o, 1, stripOutput, stripValue);
}

ag::Variant AsyncBatcher::makeBatch(
    const std::vector<ag::Variant>& queries,
    double padValue) {
  return common::makeBatchVariant(queries, padValue);
}
ag::Variant AsyncBatcher::makeBatch(const std::vector<ag::Variant>& queries) {
  return makeBatch(queries, padValue_);
}

void AsyncBatcher::setModel(ag::Container newModel) {
  std::unique_lock lk(modelMutex_);
  model_ = newModel;
}

std::unique_lock<std::shared_mutex> AsyncBatcher::lockModel() {
  return std::unique_lock(modelMutex_);
}

// ========= SubBatchAsyncBatcher
namespace {
template <typename T>
std::vector<T> tensorToVec(torch::Tensor const& t) {
  std::vector<T> res(t.size(0), 0);
  for (int i = 0; i < t.size(0); ++i) {
    res[i] = t[i].item<T>();
  }
  return res;
}

std::vector<ag::Variant> tensorsToVariantsVec(
    std::vector<torch::Tensor> const& tensors) {
  std::vector<ag::Variant> r;
  for (auto const& t : tensors) {
    r.push_back(t);
  }
  return r;
}
} // namespace

SubBatchAsyncBatcher::SubBatchAsyncBatcher(int batchSize, ag::Container model)
    : AsyncBatcher(model, -1, 0, false) {
  startBatching(batchSize);
}

SubBatchAsyncBatcher::~SubBatchAsyncBatcher() {
  stopBatching();
}

std::vector<torch::Tensor> SubBatchAsyncBatcher::unBatchTensor(
    const torch::Tensor& out,
    std::vector<int64_t> const& batchSizes) {
  if (batchSizes.empty()) {
    if (!out.dim()) {
      throw std::runtime_error("unBatchTensor: can't unbatch a leaf tensor");
    }
    std::vector<torch::Tensor> res = out.split(1);
    for (auto& t : res) {
      if (t.dim() > 1) {
        t.squeeze_(0);
      }
    }
    return res;
  }

  return out.split_with_sizes(batchSizes);
}

torch::Tensor SubBatchAsyncBatcher::makeBatchTensors(
    std::vector<torch::Tensor> const& lst,
    double padValue) {
  if (allowPadding_) {
    std::vector<int64_t> sizes = lst[0].sizes().vec();
    for (auto i = 1U; i < lst.size(); i++) {
      auto elemSize = lst[i].sizes();
      for (auto j = 1U; j < elemSize.size(); j++) {
        sizes[j] = std::max(sizes[j], elemSize[j]);
      }
      sizes[0] += elemSize[0];
    }

    auto batch = torch::empty(sizes, lst[0].options()).fill_(padValue);

    int start = 0;
    for (auto i = 0U; i < lst.size(); i++) {
      auto slice = batch;
      auto elemSize = lst[i].sizes();
      for (auto j = 0U; j < elemSize.size(); j++) {
        slice = slice.narrow(j, j == 0 ? start : 0, elemSize[j]);
      }
      slice.copy_(lst[i]);
      start += elemSize[0];
    }

    return batch;
  }
  std::vector<torch::Tensor> tensorsToCat;
  for (auto const& t : lst) {
    tensorsToCat.push_back(t.dim() > 0 ? t : t.unsqueeze(0));
  }
  return torch::cat(tensorsToCat);
}

std::vector<ag::Variant> SubBatchAsyncBatcher::unBatch(
    const ag::Variant& out,
    bool stripOutput,
    double stripValue) {
  if (!out.isDict()) {
    throw std::runtime_error(
        "unbatch expects an ag::Variant of type map<string, tensor>");
  }

  std::unordered_map<std::string, ag::Variant> const& batched = out.getDict();
  // Find batch info if available
  bool hasBatchInfo = batched.count(kBatchInfoKey) > 0;

  // Unbatch each key
  std::unordered_map<std::string, std::vector<ag::Variant>> unbatchedPerKey;
  size_t batch_size = 0;
  for (auto const& q : batched) {
    if (q.second.isDict()) {
      if (q.first == kBatchInfoKey) {
        continue;
      }
      unbatchedPerKey[q.first] = unBatch(q.second, stripOutput, stripValue);
    } else if (q.second.isTensorList()) {
      unbatchedPerKey[q.first] = tensorsToVariantsVec(q.second.getTensorList());
    } else if (q.second.isList()) {
      unbatchedPerKey[q.first] = std::vector<ag::Variant>(q.second.getList());
    } else if (q.second.isTensor()) {
      std::vector<long> batchInfo;
      if (hasBatchInfo) {
        batchInfo = findBatchInfo(batched.at(kBatchInfoKey), q.first);
      }
      unbatchedPerKey[q.first] =
          tensorsToVariantsVec(unBatchTensor(q.second.get(), batchInfo));
    } else {
      throw std::runtime_error(fmt::format(
          "unBatch: unable to process key \"{}\" of unsupported type. Please "
          "only use Dict, Tensor or TensorList.",
          q.first));
    }
    if (batch_size && unbatchedPerKey[q.first].size() != batch_size) {
      throw std::runtime_error(fmt::format(
          "unBatch error: found batch_size={}, but batch_size={} for key {}",
          batch_size,
          unbatchedPerKey[q.first].size(),
          q.first));
    }
    batch_size = unbatchedPerKey[q.first].size();
  }

  // Unbatch globally
  std::vector<ag::Variant> unbatched;
  for (auto i = 0U; i < batch_size; ++i) {
    std::unordered_map<std::string, ag::Variant> cur;
    for (auto const& unbk : unbatchedPerKey) {
      cur.insert(std::make_pair(unbk.first, unbk.second[i]));
    }
    unbatched.push_back(cur);
  }
  return unbatched;
}

ag::Variant SubBatchAsyncBatcher::makeBatch(
    const std::vector<ag::Variant>& queries,
    double padValue) {
  std::unordered_map<std::string, std::vector<ag::Variant>> batchTensorVec;
  for (auto const& q : queries) {
    if (q.isTensor()) {
      std::vector<torch::Tensor> asTensorsList;
      for (auto const& t : queries) {
        asTensorsList.push_back(t.get());
      }
      return makeBatchTensors(asTensorsList, padValue);
    }
    if (!q.isDict()) {
      throw std::runtime_error(
          "makeBatch inputs have to be Tensors or VariantDict");
    }
    for (auto& p : q.getDict()) {
      if (p.first == kBatchInfoKey) {
        throw std::runtime_error(fmt::format(
            "Can't batch a Dict that contains reserved key \"{}\"",
            kBatchInfoKey));
      }
      if (!p.second.isTensor() && !p.second.isDict()) {
        throw std::runtime_error(fmt::format(
            "can only batch ag::Variant of type map<string, tensor> or "
            "map<string, dict>, but "
            "variant for key \"{}\" is neither a tensor nor a dict",
            p.first));
      }
      batchTensorVec[p.first].push_back(p.second);
    }
  }

  std::unordered_map<std::string, ag::Variant> batchVariant;
  std::unordered_map<std::string, ag::Variant> batchInfo;
  for (auto& q : batchTensorVec) {
    // Some sanity checks
    if (q.second.size() != queries.size()) {
      throw std::runtime_error(fmt::format(
          "makeBatch: only {} items for key {}, but batch size is {}",
          q.second.size(),
          q.first,
          queries.size()));
    }
    for (auto const& val : q.second) {
      if (val.value().which() != q.second[0].value().which()) {
        throw std::runtime_error(fmt::format(
            "makeBatch: Value for key \"{}\" has multiple types", q.first));
      }
      if (val.isTensor() && val.get().dim() != q.second[0].get().dim()) {
        throw std::runtime_error(fmt::format(
            "makeBatch: At key \"{}\", found tensors with different "
            "dimensions: {} and {}",
            q.first,
            val.get().toString(),
            q.second[0].get().toString()));
      }
    }
    if (q.second[0].isDict()) {
      batchVariant[q.first] = makeBatch(q.second, padValue);
      continue;
    }
    torch::Tensor batchSizes =
        torch::zeros({int64_t(q.second.size())}, torch::kLong);
    auto batchSizesA = batchSizes.accessor<int64_t, 1>();
    std::vector<torch::Tensor> toTensors;
    for (auto i = 0U; i < q.second.size(); ++i) {
      torch::Tensor t = q.second[i].get();
      batchSizesA[i] = t.dim() > 0 ? t.size(0) : 1;
      toTensors.push_back(t);
    }
    batchVariant[q.first] = makeBatchTensors(toTensors, padValue);
    batchInfo[q.first] = batchSizes.to(q.second[0].get().options().device());
  }
  batchVariant[kBatchInfoKey] = batchInfo;
  return batchVariant;
}

std::vector<long> SubBatchAsyncBatcher::findBatchInfo(
    ag::Variant const& batchInfoVar,
    std::string const& variableName) {
  if (!batchInfoVar.isDict()) {
    throw std::runtime_error(fmt::format(
        "Wrong format for batch info variable (key \"{}\")", kBatchInfoKey));
  }
  if (batchInfoVar.getDict().count(variableName) > 0) {
    return tensorToVec<long>(batchInfoVar.getDict().at(variableName).get());
  }
  return std::vector<long>();
}

std::vector<torch::Tensor> SubBatchAsyncBatcher::forEachSubbatch(
    ag::Variant const& input,
    std::string const& inputName,
    torch::Tensor batchedInput,
    std::function<torch::Tensor(torch::Tensor)> do_fn) {
  std::vector<long> batchInfo;
  if (input.getDict().count(kBatchInfoKey) > 0) {
    batchInfo = findBatchInfo(input.getDict().at(kBatchInfoKey), inputName);
  }

  auto unbatched = unBatchTensor(batchedInput, batchInfo);
  for (auto i = 0U; i < unbatched.size(); ++i) {
    unbatched[i] = do_fn(unbatched[i]);
  }
  return unbatched;
}
} // namespace cpid
