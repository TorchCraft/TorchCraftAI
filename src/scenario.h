/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "cherrypi.h"
#include "openbwprocess.h"

#include <torchcraft/client.h>

#include <memory>
#include <string>

namespace cherrypi {

class OpenBwProcess;

/**
 * A constructed gameplay scenario for training/testing purposes
 *
 * A scenario is defined by the commands that should be executed when the game
 * starts.
 * For example, it can spawn units, ask them to move at a certain place,...'
 */
class Scenario {
 public:
  Scenario(std::string map, std::string race, bool forceGui = false);
  Scenario(Scenario&&) = default;
  virtual ~Scenario() = default;

  std::shared_ptr<torchcraft::Client> makeClient(
      torchcraft::Client::Options opts = torchcraft::Client::Options()) const;

 protected:
  Scenario() = default;

  virtual std::unique_ptr<OpenBwProcess> startProcess() const;

  std::unique_ptr<OpenBwProcess> proc_;

 private:
  bool forceGui_;
  std::string map_;
  std::string race_;
};

class MeleeScenario : public Scenario {
 public:
  MeleeScenario(
      std::string map,
      std::string myRace,
      std::string enemyRace = std::string(),
      bool forceGui = false);

 protected:
  virtual std::unique_ptr<OpenBwProcess> startProcess() const override;

 private:
  bool forceGui_;
  std::string map_;
  std::string myRace_;
  std::string enemyRace_;
};

} // namespace cherrypi
