/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "glop/linear_solver.h"
#include "glop/linear_solver.pb.h"
#include <vector>

// An assignment is given as a vector: for each agent, we store the id of the
// task, and the score of the pairing, in [0,1]
using Assign = std::vector<std::pair<int, double>>;

/** This function finds an assignment \beta that maximizes the linear objective
   \sum_{i,j}\beta_{i,j} a_{i,j}, where a_{i,j} is the affinity between i and j.
    This maximization is done under the following constraints:
       - each agent is given at most one task
       - the total sum of the contributions of the agents assigned to a task is
   bounded by the capacity of that task. Namely: for any task j, \sum_{i}
   \beta_{i,j} contrib_{i,j} <= capacity_{j}

   Solving is done by first resolving the linear relaxation of the LP using an
   exact solver, than greedily creating a integral solution.

   If normalize is true, then we apply a normalization to the affinity matrix

   The function returns a flatten version of the relaxed assignment solution,
   and the assignment itself.
 */
std::pair<std::vector<double>, Assign> solveLinearWithLP(
    const std::vector<std::vector<double>>& affinityMatrix,
    const std::vector<std::vector<double>>& contribMatrix,
    const std::vector<double>& capacities,
    bool normalize = false);

Assign solveQuad(
    const std::vector<std::vector<double>>& affinityMatrix,
    const std::vector<std::vector<double>>& crossCost,
    const std::vector<std::vector<double>>& contribMatrix,
    const std::vector<double>& capacities,
    bool normalize = false);

namespace solver_internal {
Assign retrieveAssignment(
    const std::vector<std::vector<double>>& matrix,
    const std::vector<std::vector<double>>& contribMatrix,
    std::vector<double> remaining_capa);

std::pair<
    std::vector<operations_research::MPVariable*>,
    std::vector<operations_research::MPConstraint*>>
setupLP(
    operations_research::MPSolver& solver,
    const std::vector<std::vector<double>>& contribMatrix,
    const std::vector<double>& capacities);

} // namespace solver_internal
