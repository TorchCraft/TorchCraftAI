#pragma once

#include <fstream>

#include <torch/torch.h>

#include <cereal/access.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/polymorphic.hpp>

#include "cereal/archives/binary.hpp"

#include "cereal/types/string.hpp"
#include "cereal/types/unordered_map.hpp"
#include "cereal/types/vector.hpp"

/// This file is stripped from torch/csrc/api/include/serialization
/// We do not want to move to the PyTorch serialization format since we want
/// to continue loading old models. This header is duck tape until we do want
/// to move to their serialization format

namespace ag {
// Some convenience functions for saving and loading
template <typename T>
void save(std::ostream& stream, T const& obj) {
  cereal::BinaryOutputArchive archive(stream);
  archive(*obj);
}

template <typename T>
void load(std::istream& stream, T& obj) {
  cereal::BinaryInputArchive archive(stream);
  archive(*obj);
}

template <typename T>
void save(std::ostream& stream, T const* obj) {
  cereal::BinaryOutputArchive archive(stream);
  archive(*obj);
}

template <typename T>
void load(std::istream& stream, T* obj) {
  cereal::BinaryInputArchive archive(stream);
  archive(*obj);
}

template <typename T>
void save(std::string const& path, T const& obj) {
  std::ofstream os(path, std::ios::binary);
  ag::save(os, obj);
}

template <typename T>
void load(std::string const& path, T& obj) {
  std::ifstream is(path, std::ios::binary);
  ag::load(is, obj);
}

namespace detail {

// We use our own hard-coded type<->id mapping so that serialization is robust
// wrt changes in ATen; see e.g. https://git.io/vxd6R
// The mapping is consistent with the ScalarType enum as of pytorch version
// v0.1.11-7675-ge94c67e.
inline int32_t scalarTypeId(torch::Dtype type) {
  switch (type) {
    case torch::Dtype::Byte:
      return 0;
    case torch::Dtype::Char:
      return 1;
    case torch::Dtype::Short:
      return 2;
    case torch::Dtype::Int:
      return 3;
    case torch::Dtype::Long:
      return 4;
    case torch::Dtype::Half:
      return 5;
    case torch::Dtype::Float:
      return 6;
    case torch::Dtype::Double:
      return 7;
    case torch::Dtype::Undefined:
      return 8;
    default:
      AT_ERROR("Unknown scalar type: ", static_cast<int>(type));
  }
}

inline torch::Dtype scalarTypeFromId(int32_t id) {
  switch (id) {
    case 0:
      return torch::Dtype::Byte;
    case 1:
      return torch::Dtype::Char;
    case 2:
      return torch::Dtype::Short;
    case 3:
      return torch::Dtype::Int;
    case 4:
      return torch::Dtype::Long;
    case 5:
      return torch::Dtype::Half;
    case 6:
      return torch::Dtype::Float;
    case 7:
      return torch::Dtype::Double;
    case 8:
      return torch::Dtype::Undefined;
    default:
      AT_ERROR("Unknown scalar type id: ", id);
  }
}

inline int32_t backendId(at::Backend backend) {
  switch (backend) {
    case at::Backend::CPU:
      return 0;
    case at::Backend::CUDA:
      return 1;
    case at::Backend::SparseCPU:
      return 2;
    case at::Backend::SparseCUDA:
      return 3;
    case at::Backend::Undefined:
      return 4;
    default:
      AT_ERROR("Unknown backend: ", static_cast<int>(backend));
  }
}

inline at::Backend backendFromId(int32_t id) {
  switch (id) {
    case 0:
      return at::Backend::CPU;
    case 1:
      return at::Backend::CUDA;
    case 2:
      return at::Backend::SparseCPU;
    case 3:
      return at::Backend::SparseCUDA;
    case 4:
      return at::Backend::Undefined;
    default:
      AT_ERROR("Unknown backend id: ", id);
  }
}

} // namespace detail
} // namespace ag

// This is super ugly and I don't know how to simplify it
CEREAL_REGISTER_TYPE(torch::optim::SGD);
CEREAL_REGISTER_POLYMORPHIC_RELATION(
    torch::optim::Optimizer,
    torch::optim::SGD);
CEREAL_REGISTER_TYPE(torch::optim::Adagrad);
CEREAL_REGISTER_POLYMORPHIC_RELATION(
    torch::optim::Optimizer,
    torch::optim::Adagrad);
CEREAL_REGISTER_TYPE(torch::optim::RMSprop);
CEREAL_REGISTER_POLYMORPHIC_RELATION(
    torch::optim::Optimizer,
    torch::optim::RMSprop);
CEREAL_REGISTER_TYPE(torch::optim::Adam);
CEREAL_REGISTER_POLYMORPHIC_RELATION(
    torch::optim::Optimizer,
    torch::optim::Adam);

namespace cereal {
namespace agimpl {

template <class Archive>
void saveBinary(Archive& archive, void const* data, size_t size) {
  // In general, there's no direct `saveBinary`-like method on archives
  std::vector<char> v(
      static_cast<char const*>(data), static_cast<char const*>(data) + size);
  archive(v);
}
template <>
inline void
saveBinary(BinaryOutputArchive& archive, void const* data, size_t size) {
  // Writes to output stream without extra copy
  archive.saveBinary(data, size);
}

template <class Archive>
void loadBinary(Archive& archive, void* data, size_t size) {
  // In general, there's no direct `loadBinary`-like method on archives
  std::vector<char> v(size);
  archive(v);
  std::memcpy(data, v.data(), size);
}
template <>
inline void loadBinary(BinaryInputArchive& archive, void* data, size_t size) {
  // Read from input stream without extra copy
  archive.loadBinary(data, size);
}

} // namespace agimpl

// Gradients will not be saved for variables
template <class Archive>
void save(Archive& archive, const torch::Tensor& tensor) {
  if (!tensor.defined()) {
    int32_t typeId = ::ag::detail::scalarTypeId(torch::Dtype::Undefined);
    archive(CEREAL_NVP(typeId));
    return;
  } else {
    int32_t typeId =
        ::ag::detail::scalarTypeId(torch::typeMetaToScalarType(tensor.dtype()));
    archive(CEREAL_NVP(typeId));
  }
  auto sizes = std::vector<int64_t>();
  auto buf = std::vector<uint8_t>();
  for (auto s : tensor.sizes()) {
    sizes.push_back(s);
  }
  auto contig = tensor.cpu().contiguous();
  int32_t backend = ::ag::detail::backendId(tensor.type().backend());

  archive(CEREAL_NVP(backend), CEREAL_NVP(sizes));
  agimpl::saveBinary(
      archive,
      contig.data_ptr(),
      tensor.numel() * tensor.type().elementSizeInBytes());
}

/**
 * We follow these rules for loading:
 * 1. If tensor is defined, and the same ScalarType as the saved tensor,
 *    then we simply copy the data into the tensor, with resizing.
 * 2. Otherwise, overwrite the provided tensor with the right type and backend
 **/
template <class Archive>
void load(Archive& archive, torch::Tensor& tensor) {
  torch::NoGradGuard guard;
  torch::Dtype type;
  int32_t typeId;
  archive(CEREAL_NVP(typeId));
  type = ::ag::detail::scalarTypeFromId(typeId);
  if (type == torch::Dtype::Undefined) {
    tensor = torch::Tensor();
    return;
  }

  int32_t backendId;
  auto sizes = std::vector<int64_t>();
  auto buf = std::vector<uint8_t>();
  archive(CEREAL_NVP(backendId), CEREAL_NVP(sizes));

  at::Backend backend = ::ag::detail::backendFromId(backendId);
  if (!tensor.defined() ||
      torch::typeMetaToScalarType(tensor.dtype()) != type) {
    tensor = torch::empty({}, at::TensorOptions(backend).dtype(type));
  }
  const auto required_grad = tensor.requires_grad();
  tensor.set_requires_grad(false);
  tensor.resize_(sizes);
  tensor.set_requires_grad(required_grad);

  if (tensor.type().is_cuda()) {
    // should actually use cudamemcpy probably
    auto cputensor = torch::empty(sizes, tensor.dtype());
    agimpl::loadBinary(
        archive,
        cputensor.data_ptr(),
        cputensor.numel() * cputensor.type().elementSizeInBytes());
    tensor.copy_(cputensor);
  } else {
    agimpl::loadBinary(
        archive,
        tensor.data_ptr(),
        tensor.numel() * tensor.type().elementSizeInBytes());
  }
  tensor.detach_();
  tensor.set_requires_grad(required_grad);
}

namespace detail {
enum VariantTag : int8_t {
  Tensor = 0,
  TensorVector = 1,
  String = 2,
  Float = 3,
  Double = 4,
  Bool = 5,
  Int32 = 6,
  Int64 = 7,
  VariantVector = 8,
  VariantMap = 9,
};
}

template <class Archive>
void save(Archive& ar, ag::Variant const& var) {
  using namespace detail;
  var.value().match(
      [&](torch::Tensor const& t) {
        ar(static_cast<int8_t>(VariantTag::Tensor), t);
      },
      [&](std::vector<torch::Tensor> const& v) {
        ar(static_cast<int8_t>(VariantTag::TensorVector), v);
      },
      [&](std::string const& s) {
        ar(static_cast<int8_t>(VariantTag::String), s);
      },
      [&](float v) { ar(static_cast<int8_t>(VariantTag::Float), v); },
      [&](double v) { ar(static_cast<int8_t>(VariantTag::Double), v); },
      [&](bool v) { ar(static_cast<int8_t>(VariantTag::Bool), v); },
      [&](int32_t v) { ar(static_cast<int8_t>(VariantTag::Int32), v); },
      [&](int64_t v) { ar(static_cast<int8_t>(VariantTag::Int64), v); },
      [&](std::vector<ag::Variant> const& v) {
        ar(static_cast<int8_t>(VariantTag::VariantVector), v);
      },
      [&](std::unordered_map<std::string, ag::Variant> const& m) {
        ar(static_cast<int8_t>(VariantTag::VariantMap), m);
      });
}

template <class Archive>
void load(Archive& ar, ag::Variant& var) {
  using namespace detail;
  int8_t tag;
  ar(tag);
  var = [&]() -> ag::Variant {
    switch (tag) {
      case VariantTag::Tensor: {
        torch::Tensor t;
        ar(t);
        return t;
      }
      case VariantTag::TensorVector: {
        std::vector<torch::Tensor> v;
        ar(v);
        return v;
      }
      case VariantTag::String: {
        std::string s;
        ar(s);
        return s;
      }
      case VariantTag::Float: {
        float v;
        ar(v);
        return v;
      }
      case VariantTag::Double: {
        double v;
        ar(v);
        return v;
      }
      case VariantTag::Bool: {
        bool v;
        ar(v);
        return v;
      }
      case VariantTag::Int32: {
        int32_t v;
        ar(v);
        return v;
      }
      case VariantTag::Int64: {
        int64_t v;
        ar(v);
        return v;
      }
      case VariantTag::VariantVector: {
        std::vector<ag::Variant> v;
        ar(v);
        return v;
      }
      case VariantTag::VariantMap: {
        std::unordered_map<std::string, ag::Variant> m;
        ar(m);
        return m;
      }
      default:
        throw std::runtime_error(
            "Unsupported variant tag " + std::to_string(int32_t(tag)));
    }
  }();
}

template <typename...>
struct WhichType;

constexpr size_t kTochNNModuleMagic = 0xF00DF00D;
constexpr size_t kSerializationVersion = 1;

template <class Archive>
void save(Archive& ar, torch::nn::Module const& module) {
  auto params = module.named_parameters();
  size_t size = params.size();
  ar(kTochNNModuleMagic);
  ar(kSerializationVersion);
  ar(size);
  for (auto p : params) {
    ar(p.key(), p.value());
  }

  auto buffers = module.named_buffers();
  size = buffers.size();
  ar(size);
  for (auto p : buffers) {
    ar(p.key(), p.value());
  }
}

template <class Archive>
void load(Archive& ar, torch::nn::Module module) {
  size_t magic, version, size;
  ar(magic);
  if (magic == kTochNNModuleMagic) {
    ar(version);
    ar(size);
  }
  else {
    size = magic;
    version = 0;
  }

  auto params = module.named_parameters();
  std::string name;
  for (size_t i = 0; i < size; i++) {
    ar(name);
    ar(params[name]);
  }

  auto buffers = module.named_buffers();
  if (version == 0) {
    LOG_IF(WARNING, buffers.size())
      << "Warning: torch::nn::Module serialization didnt include buffers"
        " - this will likely break BatchNorm, ...";
    return;
  }
  ar(size);
  for (size_t i = 0; i < size; i++) {
    ar(name);
    ar(buffers[name]);
  }
}

template <class Archive>
void save(Archive& archive, torch::optim::Optimizer const& optimizer) {
  std::stringstream stream;
  torch::save(optimizer, stream);
  archive(stream.str());
}

template <class Archive>
void load(Archive& archive, torch::optim::Optimizer& optimizer) {
  torch::NoGradGuard guard;
  std::string data;
  archive(data);
  std::stringstream stream(data);
  torch::load(optimizer, stream);
}

} // namespace cereal
