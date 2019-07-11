/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "variant.h"
#include <torch/torch.h>
#include "serialization.h"

#include <memory>
#include <vector>

#define AUTOGRAD_CONTAINER_CLASS(Type) \
  struct Type : public ag::Container_CRTP<Type>
#define AUTOGRAD_KWARG(CLS, TYP, NAME, DEFAULT, OPTION) \
  TYP NAME##_ = DEFAULT;                                \
  AT_DEPRECATED(CLS& NAME(TYP x = OPTION) {             \
    NAME##_ = x;                                        \
    return *this;                                       \
  })
#define AUTOGRAD_REGISTER(x,expr) x = add(expr, #x)

namespace ag {
class ContainerImpl;
using tensor_list = std::vector<torch::Tensor>;
using Container = std::shared_ptr<ContainerImpl>; // Containers are pointers!!!
using Optimizer = std::shared_ptr<torch::optim::Optimizer>;

// This clone keeps container-ness, instead of falling into torch::Modules
template <typename Ptr>
inline Container clone(Ptr&& model, at::optional<at::Device> const& = c10::nullopt);

class ContainerImpl : public virtual torch::nn::Module {
 public:
  virtual Variant forward(Variant) = 0;

  torch::Tensor&
  add(torch::Tensor, std::string const&, bool requires_grad = true);

  /// Registers a submodule with this `Module`.
  Container add(Container, std::string const&);

  /// These just check the first few parameters for the type, which might
  /// be wrong. Please override with a more correct approximation if necessary
  /// This function returns a type/options that:
  ///  1. has the device of the first parameter it finds
  ///  2. has the type of the first F16, F32 or F64 parameter it finds.
  ///     If none, take the default options if it's a floating point type.
  ///     If not, then return F32
  ///  3. layout is always dense.
  virtual torch::TensorOptions options() const;

  // Let's not support moving, it's annoying with virtual bases, and we don't
  // expect to be moving around models a lot anyway
  ContainerImpl& operator=(const ContainerImpl&) = default;
};

template <typename Derived>
struct Container_CRTP : public torch::nn::Cloneable<Derived>,
                        public virtual ContainerImpl {
  /// Moves the `Module` into a `shared_ptr` and calls `reset()` on it.
  std::shared_ptr<Derived> make();
};

// These mirrors the torch::nn namespace. Please look at the XXXXOptions for
// documentation on what options are implemented. Unlike torch::nn, we keep
// state in the Container and use .make() to make a new Container =
// std::shared_ptr<ContainerImpl>
// Example: model = ag::Con1d(5, 3, 3).stride(2).padding(3).make();
struct Linear;
struct Conv1d;
struct Conv2d;
struct Conv3d;
struct BatchNorm;
struct Dropout;
using Dropout2d = Dropout;
struct Embedding;
struct LSTM;
struct RNN;
struct GRU;

struct Sequential : public Container_CRTP<Sequential> {
  // Lets you use a container like a vector without making a new class,
  // just for simple implementations
  Variant forward(Variant input) override;
  void reset() override {}

  Container& operator[](int index);
  int size();
  auto begin() {
    return list_.begin();
  }
  auto end() {
    return list_.end();
  }

  Container add(Container m, std::string name = "");
  Sequential& append(Container m, std::string name = "");

  /// Special cloning function for `Sequential` because it does not use
  /// `reset()`.
  std::shared_ptr<torch::nn::Module> clone(
      at::optional<at::Device> const& = c10::nullopt) const override;

  std::vector<Container> list_;
  std::vector<std::string> listNames_;
};

// Copied from torch::nn::Functional
struct Functional : public Container_CRTP<Functional> {
 public:
  using Function = std::function<at::Tensor(at::Tensor)>;

  explicit Functional(Function function) : function_(std::move(function)) {}

  template <
      typename SomeFunction,
      typename... Args,
      typename = torch::enable_if_t<(sizeof...(Args) > 0)>>
  explicit Functional(SomeFunction original_function, Args&&... args)
      : function_(std::bind(
            original_function,
            /*input=*/std::placeholders::_1,
            std::forward<Args>(args)...)) {
    // std::bind is normally evil, but (1) gcc is broken w.r.t. handling
    // parameter pack expansion in lambdas and (2) moving parameter packs into
    // a lambda only works with C++14, so std::bind is the more move-aware
    // solution here.
  }

  void reset() override {}
  Variant forward(Variant inp) override {
    return Variant(tensor_list{function_(inp[0])});
  }

 private:
  Function function_;
};

/************** IMPEMENTATIONS *********************/

template <typename Ptr>
inline Container clone(Ptr&& model, at::optional<at::Device> const& device) {
  auto clone = std::dynamic_pointer_cast<ContainerImpl>(model->clone(device));
  return clone;
}

template <typename Derived>
std::shared_ptr<Derived> Container_CRTP<Derived>::make() {
  auto module = std::make_shared<Derived>(static_cast<Derived&&>(*this));
  module->reset();
  return module;
}

inline Variant Sequential::forward(Variant input) {
  for (auto& container : list_) {
    input = container->forward(input);
  }
  return input;
}

inline Container& Sequential::operator[](int index) {
  return list_[index];
}

inline int Sequential::size() {
  return list_.size();
}

inline Container Sequential::add(Container m, std::string name) {
  return append(m, name).list_.back();
}

inline Sequential& Sequential::append(Container m, std::string name) {
  if (name == "") {
    name = std::to_string(size());
  }
  list_.push_back(m);
  listNames_.push_back(name);
  ContainerImpl::add(list_.back(), name);
  return *this;
}

inline std::shared_ptr<torch::nn::Module> Sequential::clone(
    at::optional<at::Device> const& device) const {
  auto clone = Sequential().make();
  for (auto i = 0U; i < list_.size(); ++i) {
    clone->append(::ag::clone(list_[i], device), listNames_[i]);
  }
  return clone;
}

inline torch::Tensor& ContainerImpl::add(
    torch::Tensor tensor,
    std::string const& name,
    bool requires_grad) {
  return register_parameter(name, tensor, requires_grad);
}

inline Container ContainerImpl::add(Container module, std::string const& name) {
  auto m = register_module(
      name, std::dynamic_pointer_cast<torch::nn::Module>(module));
  return std::dynamic_pointer_cast<ContainerImpl>(m);
}

inline torch::TensorOptions ContainerImpl::options() const {
  // Defaults, kStrided vs kSparse are the only options, CPU, and Float32
  auto options = at::TensorOptions()
                     .layout(torch::kStrided)
                     .device(torch::kCPU)
                     .dtype(torch::kF32);

  if (parameters().size() != 0) {
    options = options.device(parameters().begin()->options().device());
  }

  for (auto& p : parameters()) {
    auto typ = p.options().dtype();
    if (torch::isFloatingType(torch::typeMetaToScalarType(typ))) {
      options = options.dtype(typ);
      break;
    }
  }

  return options;
}

/**************** MODULE IMPLEMENTATIONS ********************/

#define TORCH_OPTION(name)                                         \
  auto name(const decltype(name##_) new_##name)->decltype(*this) { \
    this->name##_ = new_##name;                                    \
    return *this;                                                  \
  }

#define GENERATE_MODULE(NAME, OPTS)                                            \
  struct NAME : public Container_CRTP<NAME>, public torch::nn::NAME##Options { \
    using torch::nn::NAME##Options::NAME##Options;                             \
    void reset() override;                                                     \
    Variant forward(Variant) override;                                         \
    torch::nn::NAME impl_{nullptr};                                            \
    OPTS                                                                       \
  };                                                                           \
  inline void NAME::reset() {                                                  \
    auto module = torch::nn::NAME(torch::nn::NAME##Options(*this));            \
    impl_ = register_module("impl", module);                                   \
  }

#define ONE_VAR_FORWARD(NAME)                                              \
  inline Variant NAME::forward(Variant inp) {                              \
    if (inp.isTensorList()) {                                              \
      return Variant(tensor_list{impl_->forward(inp.getTensorList()[0])}); \
    } else if (inp.isTensor()) {                                           \
      return Variant(tensor_list{impl_->forward(inp.get())});              \
    }                                                                      \
    throw std::runtime_error("Forward received unsupported type");         \
    return Variant();                                                      \
  }

#define RNN_FORWARD(NAME)                                           \
  inline Variant NAME::forward(Variant i) {                         \
    tensor_list& inp = i.getTensorList();                           \
    auto output = inp.size() == 1 ? impl_->forward(inp[0])          \
                                  : impl_->forward(inp[0], inp[1]); \
    return Variant(tensor_list{output.output, output.state});       \
  }

// If there are new options, just add it to the list.

GENERATE_MODULE(Linear, TORCH_OPTION(in); TORCH_OPTION(out);
                TORCH_OPTION(with_bias););
ONE_VAR_FORWARD(Linear);

#define CONV_OPTIONS             \
  TORCH_OPTION(input_channels);  \
  TORCH_OPTION(output_channels); \
  TORCH_OPTION(kernel_size);     \
  TORCH_OPTION(stride);          \
  TORCH_OPTION(padding);         \
  TORCH_OPTION(dilation);        \
  TORCH_OPTION(output_padding);  \
  TORCH_OPTION(transposed);      \
  TORCH_OPTION(with_bias);       \
  TORCH_OPTION(groups);

GENERATE_MODULE(Conv1d, CONV_OPTIONS);
ONE_VAR_FORWARD(Conv1d);
GENERATE_MODULE(Conv2d, CONV_OPTIONS);
ONE_VAR_FORWARD(Conv2d);
GENERATE_MODULE(Conv3d, CONV_OPTIONS);
ONE_VAR_FORWARD(Conv3d);
#undef CONV_OPTIONS

GENERATE_MODULE(BatchNorm, TORCH_OPTION(features); TORCH_OPTION(affine);
                TORCH_OPTION(stateful);
                TORCH_OPTION(eps);
                TORCH_OPTION(momentum););
inline Variant BatchNorm::forward(Variant i) {
  if (i.isTensor()) {
    return Variant(tensor_list{impl_->forward(i.get())});
  }
  tensor_list& inp = i.getTensorList();
  if (inp.size() == 3) {
    return Variant(tensor_list{impl_->pure_forward(inp[0], inp[1], inp[2])});
  }
  return Variant(tensor_list{impl_->forward(inp[0])});
}

GENERATE_MODULE(Dropout, TORCH_OPTION(rate));
ONE_VAR_FORWARD(Dropout);
// Dropout2d is the same as Dropout...
GENERATE_MODULE(Embedding, TORCH_OPTION(count); TORCH_OPTION(dimension););
ONE_VAR_FORWARD(Embedding);

#define RNN_OPTIONS            \
  TORCH_OPTION(input_size);    \
  TORCH_OPTION(hidden_size);   \
  TORCH_OPTION(layers);        \
  TORCH_OPTION(with_bias);     \
  TORCH_OPTION(dropout);       \
  TORCH_OPTION(bidirectional); \
  TORCH_OPTION(batch_first);

GENERATE_MODULE(LSTM, RNN_OPTIONS);
RNN_FORWARD(LSTM);
GENERATE_MODULE(RNN, RNN_OPTIONS; TORCH_OPTION(activation));
RNN_FORWARD(RNN);
GENERATE_MODULE(GRU, RNN_OPTIONS);
RNN_FORWARD(GRU);

#undef RNN_OPTIONS
#undef GENERATE_MODULE
#undef ONE_VAR_FORWARD
#undef RNN_FORWARD
#undef TORCH_OPTION

} // namespace ag
