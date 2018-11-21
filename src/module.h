/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <list>

#include <torchcraft/client.h>

#include "cherrypi.h"

namespace cherrypi {

class BasePlayer;
class State;
// Useful for derived classes
class Task;
struct UPCTuple;
struct UpcPostData;

/**
 * Interface for bot modules.
 *
 * Use Module::make<T>(args) to construct a new module instance of a given type.
 */
class Module {
 public:
  virtual ~Module() = default;

  template <typename T, typename... Args>
  static std::shared_ptr<T> make(Args&&... args) {
    auto m = std::make_shared<T>(std::forward<Args>(args)...);
    if (m->name().empty()) {
      m->setName(makeName(typeid(T)));
    }
    return m;
  }
  static std::shared_ptr<Module> make(std::string const& typeName);

  virtual void setPlayer(BasePlayer* p) {
    player_ = p;
  };

  void setName(std::string name);
  std::string name();
  static std::string makeName(std::type_index const& type);

  virtual void step(State* s) {}
  virtual void onGameStart(State* s) {}
  virtual void onGameEnd(State* s) {}

 protected:
  Module();

  BasePlayer* player_;
  std::string name_;
};

} // namespace cherrypi
