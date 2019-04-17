/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "common/str.h"

#include <functional>
#include <list>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace cherrypi {

#define REGISTER_SUBCLASS_0(Base, Derived)          \
  namespace {                                       \
  bool _registered_##Base##_##Derived ATTR_UNUSED = \
      SubclassRegistry<Base>::registerSubclass(     \
          typeid(Derived),                          \
          #Derived,                                 \
          []() -> std::shared_ptr<Base> {           \
            return std::make_shared<Derived>();     \
          });                                       \
  }

#define REGISTER_SUBCLASS_1(Base, Derived, Arg1)      \
  namespace {                                         \
  bool _registered_##Base##_##Derived ATTR_UNUSED =   \
      SubclassRegistry<Base, Arg1>::registerSubclass( \
          typeid(Derived),                            \
          #Derived,                                   \
          [](Arg1 arg1) -> std::shared_ptr<Base> {    \
            return std::make_shared<Derived>(arg1);   \
          });                                         \
  }

#define REGISTER_SUBCLASS_3(Base, Derived, Arg1, Arg2, Arg3)             \
  namespace {                                                            \
  bool _registered_##Base##_##Derived ATTR_UNUSED =                      \
      SubclassRegistry<Base, Arg1, Arg2, Arg3>::registerSubclass(        \
          typeid(Derived),                                               \
          #Derived,                                                      \
          [](Arg1 arg1, Arg2 arg2, Arg3 arg3) -> std::shared_ptr<Base> { \
            return std::make_shared<Derived>(arg1, arg2, arg3);          \
          });                                                            \
  }

template <typename Base, typename... Args>
class SubclassRegistry {
 public:
  using Ctor = std::function<std::shared_ptr<Base>(Args...)>;
  struct SubclassInfo {
    std::type_index type;
    std::string name;
    Ctor ctor;

    SubclassInfo(
        std::type_index const& type,
        std::string const& name,
        Ctor ctor)
        : type(type), name(name), ctor(std::move(ctor)) {}
  };

  static bool registerSubclass(
      std::type_index const& type,
      std::string const& name,
      Ctor ctor) {
    auto& reg = registry();
    if (reg.byType.find(type) == reg.byType.end()) {
      reg.info.emplace_back(type, name, std::move(ctor));
      reg.byType.emplace(type, reg.info.back());
      reg.byName.emplace(common::stringToLower(name), reg.info.back());
    }
    return true;
  }

  static SubclassInfo* record(std::string const& name) {
    auto& reg = registry();
    auto lowerName = common::stringToLower(name);
    auto it = reg.byName.find(lowerName);
    if (it == reg.byName.end()) {
      return nullptr;
    }
    return &it->second;
  }

  static std::vector<SubclassInfo*> subclasses() {
    std::vector<SubclassInfo*> result;
    for (auto& it : registry().info) {
      result.push_back(&it);
    }
    return result;
  }

  template <typename... CArgs>
  static std::shared_ptr<Base> create(
      std::string const& name,
      CArgs&&... args) {
    auto& reg = registry();
    auto lowerName = common::stringToLower(name);
    auto it = reg.byName.find(lowerName);
    if (it == reg.byName.end()) {
      return nullptr;
    }
    return it->second.ctor(std::forward<CArgs>(args)...);
  }

  static std::string name(std::type_index const& type) {
    auto& reg = registry();
    auto it = reg.byType.find(type);
    if (it == reg.byType.end()) {
      return std::string();
    }
    return it->second.name;
  }

  template <typename Derived>
  static std::string name() {
    return name(typeid(Derived));
  }

 private:
  struct Registry {
    std::list<SubclassInfo> info;
    std::unordered_map<std::type_index, SubclassInfo&> byType;
    // Keys are lower-case
    std::unordered_map<std::string, SubclassInfo&> byName;
  };

  static Registry& registry() {
    static Registry reg_;
    return reg_;
  }

  SubclassRegistry() = delete;
};

} // namespace cherrypi
