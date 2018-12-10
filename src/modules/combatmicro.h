/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "module.h"

namespace cherrypi {

struct Unit;

/**
 * Unit micro-management for combats
 *
 * Used only for unit tests.
 */
class CombatMicroModule : public Module {
 public:
  virtual void step(State* s) override;

 protected:
  struct HealthInfo {
    int hp;
    int shield;
  };

  void consumeUPC(State* s, int upcId, std::shared_ptr<UPCTuple> upc);
  void updateTasks(State* s);
  void updateTarget(
      std::shared_ptr<Task> task,
      std::unordered_map<Unit*, HealthInfo>* targetHealth);
};

} // namespace cherrypi
