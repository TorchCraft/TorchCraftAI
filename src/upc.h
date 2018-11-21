/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "buildtype.h"
#include "cherrypi.h"

#include <mapbox/variant.hpp>
#ifdef HAVE_TORCH
#include <autogradpp/autograd.h>
#endif // HAVE_TORCH

#include <unordered_map>

namespace cherrypi {

class State;
struct Area;
struct BuildType;
struct Unit;

/**
 * (Unit, Position, Command) tuple.
 * Specifies the (who, where, what) of an action
 *
 * UPCTuples are used for module-to-module communication (via the Blackboard).
 * Posting a UPCTuple to the Blackboard is akin to requesting a specific action
 * from another module, and consuming a UPCTuple from the Blackboard implies
 * that the consumer implements this action. The implementation can also consist
 * of refining the UPCTuple so that a more lower-level module can actually
 * execute it.
 *
 * For example, a build order module might post a UPCTuple to create a certain
 * unit type, and a builder module might wait until their are sufficient
 * resources before consuming it and then select a worker and a location.
 *
 * The UPCToCommandModule translates executable UPCTuples (ones with
 * sharp unit, position and command entries) to actual game commands.
 */
struct UPCTuple {
  /// An empty default item for the variants defined below
  using Empty = char; // Any better options here?
  using UnitMap = std::unordered_map<Unit*, float>;
  using CommandMap = std::unordered_map<Command, float>;
  using BuildTypeMap = std::unordered_map<BuildType const*, float>;

  using SetCreatePriorityState = std::tuple<UpcId, float>;

  // Using variant for some types but not others is not very consistent, but it
  // simplifies code working with UPCTuples.

  /// A typedef for the unit distribution.
  using UnitT = UnitMap;
#ifdef HAVE_TORCH
  /// A typedef for the position distribution.
  /// Possible values:
  /// - Empty: unspecified position, i.e. uniform over the entire map
  /// - Position: A single x/y position with probability of one.
  ///   This is more efficient then having a one-hot position tensor.
  /// - Area*: An area so that every position in this area has probability
  ///   1/(walktiles in area), and every position outside has probability 0.
  /// - UnitMap: A distribution over units which can be used instead of
  ///   position. Sometimes it is more efficient to specify target units
  ///   directly (e.g. for resource mining or for attacks).
  /// - torch::Tensor: A distribution over the whole map. First dimension is Y,
  ///   second is X.
  using PositionT =
      mapbox::util::variant<Empty, Position, Area*, UnitMap, torch::Tensor>;
#else // HAVE_TORCH
  using PositionT = mapbox::util::variant<Empty, Position, Area*, UnitMap>;
#endif // HAVE_TORCH
  /// A typedef for the command distribution
  using CommandT = CommandMap;
#ifdef HAVE_TORCH
  /// A typedef for additional structured information ("state").
  /// Possible values:
  /// - Empty: unspecified state
  /// - BuildTypeMap: A distribution over unit types. This is useful for UPCs
  ///   with the "Create" command.
  /// - std::string: An arbitrary string.
  /// - Position: An arbitrary position.
  /// - torch::Tensor: An arbitrary tensor.
  using StateT = mapbox::util::variant<Empty,
                                       BuildTypeMap,
                                       std::string,
                                       Position,
                                       SetCreatePriorityState,
                                       torch::Tensor>;
#else // HAVE_TORCH
  using StateT = mapbox::util::variant<Empty,
                                       BuildTypeMap,
                                       std::string,
                                       Position,
                                       SetCreatePriorityState>;
#endif // HAVE_TORCH

  /// A distribution over units that we can control.
  UnitT unit;
  /// A distribution over positions.
  PositionT position;
  /// A distribution over abstract game commands.
  CommandT command;
  /// An auxiliary state that can be used to pass additional information.
  /// Interpretation of this is entirely context-dependent.
  StateT state;

  /// Specifies the (inverse) scale of the position tensor or the single sharp
  /// position.
  /// For efficiency reasons, position tensors can be specified at a coarser
  /// resolution.
  int scale = 1;

  /* Methods */

  /// Returns argmax and probability of position distribution
  std::pair<Position, float> positionArgMax() const;

  /// Returns argmax and probability of position distribution over units
  std::pair<Unit*, float> positionUArgMax() const;

  /// Returns the probability of a given position
  float positionProb(int x, int y) const;

  /// Returns the probability of a given command
  float commandProb(Command c) const;

#ifdef HAVE_TORCH
  /// Returns walk tile resolution tensor of position probabilities
  torch::Tensor positionTensor(State* state) const;
#endif // HAVE_TORCH

  /// Returns argmax and probability of the BuildTypeMap distribution in the
  /// UPC's `state` field.
  /// If the UPC specifies a different type of state, this function will return
  /// `(nullptr, 0.0f)`
  std::pair<BuildType const*, float> createTypeArgMax() const;

  /// Creates a uniform distribution over all game commands.
  static CommandT uniformCommand();
};

} // namespace cherrypi
