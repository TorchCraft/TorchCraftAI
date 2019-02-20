/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <unordered_map>

#include "flags.h"
#include "module.h"
#include "state.h"
#include "unitsinfo.h"
#include "utils.h"

class RuleModule : public cherrypi::Module {
 public:
  long currentFrame_ = 0;
  std::unordered_map<cherrypi::Unit*, cherrypi::Unit*> attacks_;

  RuleModule() : Module() {
    setName("Rule");
  }

  void step(cherrypi::State* state) override {
    auto& allies = state->unitsInfo().myUnits();
    auto& enemies = state->unitsInfo().enemyUnits();

    if (currentFrame_ % 100 == 0) {
      attacks_.clear();
    }

    if (currentFrame_ % FLAGS_frame_skip == 0) { // Frame skip
      for (auto& ally : allies) {
        cherrypi::Unit* target = nullptr;
        if (FLAGS_opponent == "attack_move") {
          // To make episodes end faster and so our models don't just learn to
          // run, we attack move idle units towards our enemy
          if (ally->idle() && enemies.size() > 0) {
            auto upc = cherrypi::utils::makeSharpUPC(
                ally,
                cherrypi::Position{enemies[0]},
                cherrypi::Command::Delete);
            state->board()->postUPC(std::move(upc), cherrypi::kRootUpcId, this);
          }
        } else if (FLAGS_opponent == "closest") {
          int x = ally->unit.x;
          int y = ally->unit.y;
          target = enemies[0];
          float nearestDist = 1e9;
          for (auto& enemy : enemies) {
            int eX = enemy->unit.x;
            int eY = enemy->unit.y;
            float dist =
                std::pow(float(x - eX), 2.) + std::pow(float(y - eY), 2.);
            if (dist < nearestDist) {
              nearestDist = dist;
              target = enemy;
            }
          }
        } else if (FLAGS_opponent == "weakest") {
          int x = ally->unit.x;
          int y = ally->unit.y;
          target = enemies[0];
          float weakest = 1e9;
          for (auto& enemy : enemies) {
            int eX = enemy->unit.x;
            int eY = enemy->unit.y;
            float dist =
                std::pow(float(x - eX), 2.) + std::pow(float(y - eY), 2.);
            float score = enemy->unit.health + enemy->unit.shield + dist / 1024;
            if (score < weakest) {
              weakest = score;
              target = enemy;
            }
          }
        } else {
          throw std::runtime_error("No such opponent: " + FLAGS_opponent);
        }
        // Sending same attack command can "cancel" attacks, so check first
        if (target && attacks_[ally] != target) {
          attacks_[ally] = target;
          auto upc = cherrypi::utils::makeSharpUPC(
              ally, target, cherrypi::Command::Delete);
          state->board()->postUPC(std::move(upc), cherrypi::kRootUpcId, this);
        }
      }
    }
    currentFrame_++;
  }
};
