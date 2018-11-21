/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "module.h"

#include "state.h"
#include "utils.h"

#include <algorithm>
#include <cctype>

namespace cherrypi {

Module::Module() {}

std::shared_ptr<Module> Module::make(std::string const& typeName) {
  auto* record = SubclassRegistry<Module>::record(typeName);
  if (record == nullptr) {
    record = SubclassRegistry<Module>::record(typeName + "Module");
    if (record == nullptr) {
      LOG(WARNING) << "No such module: " << typeName;
      return nullptr;
    }
  }

  auto m = record->ctor();
  if (m->name().empty()) {
    m->setName(makeName(record->type));
  }
  return m;
}

void Module::setName(std::string name) {
  name_ = std::move(name);
}

std::string Module::name() {
  return name_;
}

std::string Module::makeName(std::type_index const& type) {
  auto name = SubclassRegistry<Module>::name(type);
  if (name.empty()) {
    // This will return a mangled name but it's better than an empty string.
    return type.name();
  }
  return name;
}

} // namespace cherrypi
