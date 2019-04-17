/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <set>
#include <vector>

#include "module.h"
#include "unitsinfo.h"

namespace cherrypi {

/**
 * The last module run in each frame.
 * Consumes all remaining unambiguous (sharp) UPCs and issues BWAPI commands
 * via TorchCraft.
 */
class UPCToCommandModule : public Module {
 public:
  UPCToCommandModule() {}
  virtual ~UPCToCommandModule() = default;

  virtual void step(State* s);

 private:
  struct UPCToCommandState {
    std::set<const Unit*> commandToUnit;
    std::vector<tc::Client::Command> commands;
    std::vector<int> upcIds;
  };

  void checkDuplicateCommand(
      State* state,
      const Unit*,
      UpcId newUpcId,
      UPCToCommandState&);
  void registerCommand(
      State*,
      const Unit*,
      UpcId,
      tc::Client::Command,
      UPCToCommandState&);
  void stepUPC(State*, UPCToCommandState&, UpcId, UPCTuple* const);
  void postGameCommand(State*, UPCToCommandState&);
  void temporaryDebugDrawing(State*, UPCToCommandState&);
};

} // namespace cherrypi
