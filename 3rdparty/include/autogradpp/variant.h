/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <mapbox/variant.hpp>
#include <torch/torch.h>

namespace ag {
class Variant;
using VariantType = mapbox::util::variant<
    torch::Tensor,
    std::vector<torch::Tensor>,
    std::string,
    float,
    double,
    bool,
    int32_t,
    int64_t,
    mapbox::util::recursive_wrapper<std::vector<Variant>>,
    mapbox::util::recursive_wrapper<std::unordered_map<std::string, Variant>>>;

using VariantDict = std::unordered_map<std::string, Variant>;

#define GEN_TYPE(TYP, NAME) \
  Variant(TYP);             \
  bool is##NAME() const;    \
  TYP get##NAME() const;

class Variant {
 public:
  Variant() = default;
  Variant(const Variant&) = default;
  Variant(Variant&&) = default;
  Variant& operator=(Variant&&) = default;
  Variant(torch::Tensor);
  Variant(const std::vector<torch::Tensor>&);
  Variant(const std::string&);
  Variant(std::vector<Variant>&);
  Variant(std::vector<Variant>&&);
  Variant(std::initializer_list<torch::Tensor>);
  Variant(std::unordered_map<std::string, Variant>&);
  Variant(std::unordered_map<std::string, Variant>&&);

  torch::Tensor& get();
  std::vector<torch::Tensor>& getTensorList();
  std::vector<Variant>& getList();
  std::unordered_map<std::string, Variant>& getDict();

  torch::Tensor const& get() const;
  std::vector<torch::Tensor> const& getTensorList() const;
  std::vector<Variant> const& getList() const;
  std::unordered_map<std::string, Variant> const& getDict() const;

  std::string const& getString() const;
  bool isTensor() const;
  bool isTensorList() const;
  bool isString() const;
  bool isList() const;
  bool isDict() const;
  GEN_TYPE(float, Float);
  GEN_TYPE(double, Double);
  GEN_TYPE(bool, Bool);
  GEN_TYPE(int32_t, Int32);
  GEN_TYPE(int64_t, Int64);

  // This is a convenience method, to be used only if the underlying type is
  // tensor_list. It will return the ith item of the tensor_list
  torch::Tensor& operator[](size_t i);
  torch::Tensor const& operator[](size_t i) const;

  // This is a convenience method, to be used only if the underlying type is
  // unordered_map<string, Variant> and the variant can be unambiguously
  // converted to a Tensor
  torch::Tensor& operator[](const std::string& key);
  torch::Tensor const& operator[](const std::string& key) const;

  template <typename T>
  T get() {
    return value_.get<T>();
  }

  template <typename F, typename... Args>
  auto m(F func, Args&&... params) const {
    return func(get(), std::forward<Args>(params)...);
  }

 private:
  VariantType value_;
};
#undef GEN_TYPE

/**************** IMPLEMENTATIONS ********************/

#define GEN_TYPE(TYP, NAME)               \
  inline Variant::Variant(TYP x) {        \
    value_ = x;                           \
  };                                      \
  inline bool Variant::is##NAME() const { \
    return value_.is<TYP>();              \
  };                                      \
  inline TYP Variant::get##NAME() const { \
    return value_.get<TYP>();             \
  };

inline Variant::Variant(torch::Tensor x) {
  value_ = x;
}
inline Variant::Variant(const std::vector<torch::Tensor>& x) {
  value_ = x;
}
inline Variant::Variant(const std::string& x) {
  value_ = x;
}
inline Variant::Variant(std::vector<Variant>& x) {
  value_ = x;
}
inline Variant::Variant(std::vector<Variant>&& x) {
  value_ = std::move(x);
}
inline Variant::Variant(std::initializer_list<torch::Tensor> l) {
  value_ = std::vector<torch::Tensor>(l);
}
inline Variant::Variant(std::unordered_map<std::string, Variant>& x) {
  value_ = x;
};
inline Variant::Variant(std::unordered_map<std::string, Variant>&& x) {
  value_ = std::move(x);
}
inline torch::Tensor& Variant::get() {
  return value_.get<torch::Tensor>();
}
inline std::vector<torch::Tensor>& Variant::getTensorList() {
  return value_.get<std::vector<torch::Tensor>>();
}
inline std::vector<Variant>& Variant::getList() {
  return value_.get<mapbox::util::recursive_wrapper<std::vector<Variant>>>()
      .get();
}
inline std::unordered_map<std::string, Variant>& Variant::getDict() {
  return value_
      .get<mapbox::util::recursive_wrapper<
          std::unordered_map<std::string, Variant>>>()
      .get();
}
inline torch::Tensor const& Variant::get() const {
  return value_.get<torch::Tensor>();
}
inline std::vector<torch::Tensor> const& Variant::getTensorList() const {
  return value_.get<std::vector<torch::Tensor>>();
}
inline std::vector<Variant> const& Variant::getList() const {
  return value_.get<mapbox::util::recursive_wrapper<std::vector<Variant>>>()
      .get();
}
inline std::unordered_map<std::string, Variant> const& Variant::getDict()
    const {
  return value_
      .get<mapbox::util::recursive_wrapper<
          std::unordered_map<std::string, Variant>>>()
      .get();
}
inline std::string const& Variant::getString() const {
  return value_.get<std::string>();
}
inline bool Variant::isTensor() const {
  return value_.is<torch::Tensor>();
}
inline bool Variant::isTensorList() const {
  return value_.is<std::vector<torch::Tensor>>();
}
inline bool Variant::isString() const {
  return value_.is<std::string>();
}
inline bool Variant::isList() const {
  return value_.is<mapbox::util::recursive_wrapper<std::vector<Variant>>>();
}
inline bool Variant::isDict() const {
  return value_.is<mapbox::util::recursive_wrapper<
      std::unordered_map<std::string, Variant>>>();
}
GEN_TYPE(float, Float);
GEN_TYPE(double, Double);
GEN_TYPE(bool, Bool);
GEN_TYPE(int32_t, Int32);
GEN_TYPE(int64_t, Int64);
#undef GEN_TYPE

inline torch::Tensor& Variant::operator[](size_t i) {
  if (i == 0 && isTensor()) {
    return get();
  }
  if (!isTensorList()) {
    throw std::runtime_error("Not a tensor list");
  }
  // Use at() to enable bounds checking -- safety first!
  return getTensorList().at(i);
}

inline torch::Tensor const& Variant::operator[](size_t i) const {
  return (*const_cast<Variant*>(this))[i];
}

inline torch::Tensor& Variant::operator[](const std::string& key) {
  if (!isDict()) {
    throw std::runtime_error("Not a dict");
  }
  auto& d = getDict();
  auto it = d.find(key);
  if (it == d.end()) {
    it = d.insert({key, torch::Tensor()}).first;
  }
  auto& tensorVar = it->second;
  if (tensorVar.isTensor()) {
    return tensorVar.get();
  } else if (
      tensorVar.isTensorList() && tensorVar.getTensorList().size() == 1) {
    return tensorVar[0];
  } else {
    throw std::runtime_error(
        "No canonical way to convert the variant to a tensor");
  }
}

inline torch::Tensor const& Variant::operator[](const std::string& key) const {
  return (*const_cast<Variant*>(this))[key];
}

} // namespace ag
