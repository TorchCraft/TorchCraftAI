/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "debug.h"
#ifdef CUDA_FOUND
#include <THC/THCCachingAllocator.h>
#endif // CUDA_FOUND
#include <fmt/ostream.h>
#include <torch/csrc/autograd/function.h>

namespace common {

std::string tensorInfo(torch::Tensor x) {
  std::ostringstream oss;
  oss << x.type().toString();
  if (x.defined()) {
    oss << " " << x.sizes();
  }
  return oss.str();
}

namespace {
std::string variantInfo_impl(ag::Variant x, int depth) {
  std::ostringstream oss;
  std::string sep(depth * 3, ' ');
  if (x.isTensor()) {
    oss << sep << "Tensor: " << tensorInfo(x.get());
  } else if (x.isFloat()) {
    oss << sep << "float: " << x.getFloat();
  } else if (x.isDouble()) {
    oss << sep << "double: " << x.getDouble();
  } else if (x.isBool()) {
    oss << sep << "bool: " << x.getBool();
  } else if (x.isInt32()) {
    oss << sep << "Int32: " << x.getInt32();
  } else if (x.isInt64()) {
    oss << sep << "Int64: " << x.getInt64();
  } else if (x.isString()) {
    oss << sep << "string: " << x.getString();
  } else if (x.isTensorList()) {
    oss << sep << "TensorList: " << std::endl;
    for (size_t i = 0; i < x.getTensorList().size(); ++i) {
      oss << sep << "   "
          << "[" << i << "] = " << tensorInfo(x[i]);
      if (i != x.getTensorList().size() - 1) {
        oss << std::endl;
      }
    }
  } else if (x.isList()) {
    oss << sep << "List: " << std::endl;
    for (size_t i = 0; i < x.getList().size(); ++i) {
      oss << sep << "[" << i << "] = " << std::endl;
      oss << variantInfo_impl(x.getList()[i], depth + 1);
      if (i != x.getList().size() - 1) {
        oss << std::endl;
      }
    }
  } else if (x.isDict()) {
    oss << sep << "Dict: " << std::endl;
    size_t count = 0;
    for (const auto& it : x.getDict()) {
      if (count > 0) {
        oss << std::endl;
      }
      oss << sep << "[" << it.first << "] = " << std::endl;
      oss << variantInfo_impl(it.second, depth + 1);
      count++;
    }
  } else {
    oss << sep << "Unknown variant type";
  }
  return oss.str();
}

struct CustomFunctionPostHook : torch::autograd::FunctionPostHook {
  HookFunction func_;
  CustomFunctionPostHook(HookFunction func) : func_(std::move(func)){};
  virtual VarList operator()(
      const VarList& gradInput,
      const VarList& gradOutput) {
    return func_(gradInput, gradOutput);
  };
};

} // namespace

std::string variantInfo(ag::Variant x) {
  return variantInfo_impl(x, 0);
}

std::string tensorStats(torch::Tensor x) {
  std::ostringstream oss;
  oss << tensorInfo(x);
  if (x.defined()) {
    oss << " min " << x.min().item<float>() << " max " << x.max().item<float>()
        << " mean " << x.sum().item<float>() / x.numel() << " sum "
        << x.sum().item<float>();
  }
  return oss.str();
}

void checkTensor(torch::Tensor x, bool logOnError) {
  if (!std::isfinite(x.sum().item<float>())) {
    if (logOnError) {
      LOG(ERROR) << "Tensor with a NaN or infinity: " << x;
    }

    throw std::runtime_error("checkTensor: tensor has a NaN or infinity!");
  }
}

torch::Tensor const& addHook(torch::Tensor const& tensor, HookFunction&& f) {
  auto& var = torch::autograd::as_variable_ref(tensor);
  if (auto grad_fn = var.grad_fn()) {
    grad_fn->add_post_hook(std::make_unique<CustomFunctionPostHook>(f));
  }
  return tensor;
}

void assertSize(
    const std::string& name,
    const torch::Tensor& tensor,
    at::IntList sizes) {
  auto dimensionsExpected = (int64_t)sizes.size();
  auto dimensionsActual = tensor.dim();
  auto error = fmt::format(
      "Expected tensor {} to have sizes {}, but are actually {}",
      name,
      sizes,
      tensor.sizes());
  if (dimensionsExpected != dimensionsActual) {
    LOG(ERROR) << error;
    throw std::range_error(std::move(error));
  }
  for (size_t i = 0; i < sizes.size(); ++i) {
    auto expected = sizes[i];
    auto actual = tensor.size(i);
    if (expected >= 0 && expected != actual) {
      LOG(ERROR) << error;
      throw std::range_error(error);
    }
  }
  VLOG(3) << fmt::format("{} is ", name) << tensor.sizes();
}

WeightSummary::WeightSummary(torch::nn::Module& module) {
  for (auto& param : module.parameters()) {
    auto weightsTotal = param.numel();
    auto weightTensor1D = param.view({weightsTotal});
    auto weightsBefore = weights;
    weights += weightsTotal;
    zeroes += (weightTensor1D.abs() < 1e-8).sum().item<int32_t>();
    nans += (weightTensor1D != weightTensor1D).sum().item<int32_t>();
    norm1 = (norm1 * weightsBefore + weightTensor1D.norm(1).item<float>()) /
        weights;
    norm2 = sqrt(
                pow(norm2 * weightsBefore, 2) +
                weightTensor1D.pow(2).sum().item<float>()) /
        weights;
  }
}
std::string WeightSummary::toString() const {
  return fmt::format(
      "Weights: {:<11} Zeroes: {:<11} NaNs: {:<11} Norm1: {:.6f} Norm2: "
      "{:.6f}b",
      weights,
      zeroes,
      nans,
      norm1,
      norm2);
}

std::ostream& operator<<(std::ostream& out, const WeightSummary& summary) {
  return out << summary.toString();
}

std::pair<int64_t, int64_t> torchMemoryUsage(int device) {
#ifdef CUDA_FOUND
  cudaDeviceSynchronize();
  return std::pair(
      THCCachingAllocator_currentMemoryAllocated(device),
      THCCachingAllocator_currentMemoryCached(device));
#else // CUDA_FOUND
  throw std::runtime_error("did not compile with CUDA support");
#endif // CUDA_FOUND
}

} // namespace common
