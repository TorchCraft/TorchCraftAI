/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "solver.h"
#include "flags.h"

#include "common/rand.h"

#include "common/autograd.h"
#include "glop/linear_solver.h"
#include "glop/linear_solver.pb.h"
#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <autogradpp/autograd.h>

using Eigen::MatrixXd;
using Eigen::VectorXd;

using namespace operations_research;

namespace solver_internal {

std::normal_distribution<float> noise(0, 0.00001);
// given the solution to the relaxed problem, greedily construct the (discrete)
// assignment
Assign retrieveAssignment(
    const std::vector<std::vector<double>>& matrix,
    const std::vector<std::vector<double>>& contribMatrix,
    std::vector<double> remaining_capa) {
  const size_t nAgents = contribMatrix.size();
  const size_t nTasks = contribMatrix[0].size();
  if (nAgents != matrix.size()) {
    LOG(FATAL) << "Matrix doesn't have expected first dim: " << matrix.size()
               << " instead of " << nAgents;
  }
  if (nTasks != matrix[0].size()) {
    LOG(FATAL) << "Matrix doesn't have expected second dim: "
               << matrix[0].size() << " instead of " << nTasks;
  }
  if (nAgents != contribMatrix.size()) {
    LOG(FATAL) << "ContribMatrix doesn't have expected first dim: "
               << matrix.size() << " instead of " << nAgents;
  }
  if (nTasks != contribMatrix[0].size()) {
    LOG(FATAL) << "contribMatrix doesn't have expected second dim: "
               << matrix[0].size() << " instead of " << nTasks;
  }
  if (nTasks != remaining_capa.size()) {
    LOG(FATAL) << "Must provide capacity of all tasks. Got "
               << remaining_capa.size() << " capacities instead of " << nTasks;
  }

  std::vector<std::pair<std::vector<std::pair<double, int>>, int>> values(
      nAgents,
      std::pair<std::vector<std::pair<double, int>>, int>{
          std::vector<std::pair<double, int>>(nTasks), 0});
  for (size_t i = 0; i < nAgents; ++i) {
    values[i].second = i;
    for (size_t j = 0; j < nTasks; ++j) {
      // we add a tiny noise to break ties randomly
      values[i].first[j].first = matrix[i][j] + common::Rand::sample(noise);
      values[i].first[j].second = j;
    }
    // sort by x
    std::sort(
        values[i].first.begin(),
        values[i].first.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });
  }
  // Now, we want to greedily create the assignment. For this, we sort the
  // agents by the value of their highest assignment

  std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) {
    return a.first[0].first > b.first[0].first;
  });

  /*
  std::vector<int> hpEnemies(nTasks, 0);
  for (size_t i = 0; i < nTasks; ++i) {
    hpEnemies[i] = kSlack + enemies[i]->unit.health + enemies[i]->unit.shield;
  }
  */
  Assign assignment(nAgents, {0, 0.});
  for (const auto& w : values) {
    int cur = 0;
    // in case we don't find a better assignment for this agent, we still store
    // the hightest scoring task (with score 0);
    assignment[w.second].first = w.first[0].second;
    assignment[w.second].second = 0;
    bool found = false;
    while (!found && cur < (int)w.first.size()) {
      int cur_id = w.first[cur].second;
      if (remaining_capa[cur_id] > 0) {
        found = true;
        /*
        // TODO cache this dmg
        int hpDmg, shieldDmg;
        allies[w.second]->computeDamageTo(enemies[cur_id], &hpDmg, &shieldDmg);
        int curHp =
            allies[w.second]->unit.health + allies[w.second]->unit.shield;
        hpEnemies[cur_id] -=
            hpDmg + shieldDmg - kHPSlack * std::max(0., (18. - curHp) / 2.);
        */
        remaining_capa[cur_id] -= contribMatrix[w.second][cur_id];
        assignment[w.second].first = cur_id;
        assignment[w.second].second = w.first[cur].first;
      }
      cur++;
    }
  }
  return assignment;
}

std::pair<std::vector<MPVariable*>, std::vector<MPConstraint*>> setupLP(
    MPSolver& solver,
    const std::vector<std::vector<double>>& contribMatrix,
    const std::vector<double>& capacities) {
  const size_t nAgents = contribMatrix.size();
  const size_t nTasks = contribMatrix[0].size();

  solver.set_time_limit(1600);

  const double infinity = solver.infinity();

  // We now construct a LP. It has one variable for each couple (agent, task)
  // that is 1 if agent is assigned to task and 0 otherwise.
  // We first create a helper lambda that computes the id of the variable
  // corresponding to each couple.

  auto getId = [nb_tasks = nTasks](int agent, int task) {
    return agent * nb_tasks + task;
  };

  std::vector<MPVariable*> allVars;
  for (size_t i = 0; i < nAgents; ++i) {
    for (size_t j = 0; j < nTasks; ++j) {
      allVars.emplace_back(solver.MakeBoolVar(
          "x_" + std::to_string(i) + "_" + std::to_string(j)));
    }
  }

  std::vector<MPConstraint*> allCst;
  // First set of constraints: at most one task per unit
  // In other words, for all agent i, \sum_{task j} x[i,j] <= 1
  // where x[i,j] is 1 if i targets j
  int cstId = 0;
  for (size_t i = 0; i < nAgents; ++i) {
    allCst.emplace_back(solver.MakeRowConstraint(0, 1.0));
    for (size_t j = 0; j < nTasks; ++j) {
      allCst[cstId]->SetCoefficient(allVars[getId(i, j)], 1);
    }
    cstId++;
  }

  // Second set of constraints: the total contributions don't exceed the
  // capacity of the tasks
  for (size_t j = 0; j < nTasks; ++j) {
    // \sum_i contrib[i,j] * x[i,j]  <= capacity[j]
    allCst.emplace_back(solver.MakeRowConstraint(-infinity, capacities[j]));
    for (size_t i = 0; i < nAgents; ++i) {
      allCst[cstId]->SetCoefficient(allVars[getId(i, j)], contribMatrix[i][j]);
    }
    cstId++;
  }
  return {std::move(allVars), std::move(allCst)};
}
} // namespace solver_internal

using namespace solver_internal;

std::pair<std::vector<double>, Assign> solveLinearWithLP(
    const std::vector<std::vector<double>>& affinityMatrix,
    const std::vector<std::vector<double>>& contribMatrix,
    const std::vector<double>& capacities,
    bool normalize) {
  const size_t nAgents = affinityMatrix.size();
  const size_t nTasks = affinityMatrix[0].size();

  if (contribMatrix.size() != nAgents) {
    LOG(FATAL) << "Wrong first dimension of the contribMatrix: expected "
               << nAgents << " but got " << contribMatrix.size();
  }
  if (contribMatrix[0].size() != nTasks) {
    LOG(FATAL) << "Wrong second dimension of the contribMatrix: expected "
               << nTasks << " but got " << contribMatrix[0].size();
  }

  if (capacities.size() != nTasks) {
    LOG(FATAL) << "Must provide capacity of all tasks. Got "
               << capacities.size() << " capacities instead of " << nTasks;
  }

  // first we normalize the affinities, so that they are possible and in a
  // sensible range
  std::vector<std::vector<double>> affinityMatrix_(
      nAgents, std::vector<double>(nTasks, 0));
  double minAffin = 1e100;
  double maxAffin = -1e100;
  if (normalize) {
    for (size_t i = 0; i < nAgents; ++i) {
      for (size_t j = 0; j < nTasks; ++j) {
        minAffin = std::min(minAffin, affinityMatrix[i][j]);
        maxAffin = std::max(maxAffin, affinityMatrix[i][j]);
      }
    }
    minAffin -= std::abs(0.1 * minAffin);
  }
  for (size_t i = 0; i < nAgents; ++i) {
    for (size_t j = 0; j < nTasks; ++j) {
      if (normalize) {
        affinityMatrix_[i][j] =
            100.0 * (affinityMatrix[i][j] - minAffin) / (maxAffin - minAffin);
      } else {
        affinityMatrix_[i][j] = affinityMatrix[i][j];
      }
    }
  }

  MPSolver solver("assignementLP", MPSolver::GLOP_LINEAR_PROGRAMMING);
  std::vector<MPVariable*> allVars;
  std::vector<MPConstraint*> allCst;
  std::tie(allVars, allCst) = setupLP(solver, contribMatrix, capacities);
  auto getId = [nb_tasks = nTasks](int agent, int task) {
    return agent * nb_tasks + task;
  };

  // We finally fill the objective, which is maximize expected utility
  MPObjective* const objective = solver.MutableObjective();
  for (size_t i = 0; i < nAgents; ++i) {
    for (size_t j = 0; j < nTasks; ++j) {
      double utility = affinityMatrix_[i][j];
      objective->SetCoefficient(allVars[getId(i, j)], utility);
    }
  }
  objective->SetMaximization();

  // Solve !
  const MPSolver::ResultStatus result_status = solver.Solve();
  if (result_status != MPSolver::OPTIMAL) {
    std::cout << "FATAL: Something went wrong when solving first LP. "
              << std::endl;
    bool ok = false;
    switch (result_status) {
      case MPSolver::OPTIMAL:
        std::cout << "MPSOLVER_OPTIMAL" << std::endl;
        break;
      case MPSolver::FEASIBLE:
        std::cout << "MPSOLVER_FEASIBLE" << std::endl;
        ok = true;
        break;
      case MPSolver::INFEASIBLE:
        std::cout << "MPSOLVER_INFEASIBLE" << std::endl;
        break;
      case MPSolver::UNBOUNDED:
        std::cout << "MPSOLVER_UNBOUNDED" << std::endl;
        break;
      case MPSolver::ABNORMAL:
        std::cout << "MPSOLVER_ABNORMAL" << std::endl;
        break;
      case MPSolver::MODEL_INVALID:
        std::cout << "MPSOLVER_MODEL_INVALID" << std::endl;
        break;
      case MPSolver::NOT_SOLVED:
        std::cout << "MPSOLVER_NOT_SOLVED" << std::endl;
        break;
    }
    if (!ok)
      return {std::vector<double>(nAgents * nTasks, 0),
              Assign(nAgents, {0, 0.})};
  }

  std::vector<double> res(nTasks * nAgents);
  std::vector<std::vector<double>> matrix(
      nAgents, std::vector<double>(nTasks, 0));
  for (size_t i = 0; i < nAgents; ++i) {
    for (size_t j = 0; j < nTasks; ++j) {
      res[getId(i, j)] = allVars[getId(i, j)]->solution_value();
      matrix[i][j] = allVars[getId(i, j)]->solution_value();
    }
  }
  return {res, retrieveAssignment(matrix, contribMatrix, capacities)};
}

namespace {
Assign solveWithFW4Torch(
    const std::vector<std::vector<double>>& affinityMatrix,
    const std::vector<std::vector<double>>& crossCost,
    const std::vector<std::vector<double>>& contribMatrix,
    const std::vector<double>& capacities,
    bool normalize = false) {
  const size_t nAgents = affinityMatrix.size();
  const size_t nTasks = affinityMatrix[0].size();
  const int nbVars = (nTasks)*nAgents;

  if (contribMatrix.size() != nAgents) {
    LOG(FATAL) << "Wrong first dimension of the contribMatrix: expected "
               << nAgents << " but got " << contribMatrix.size();
  }
  if (contribMatrix[0].size() != nTasks) {
    LOG(FATAL) << "Wrong second dimension of the contribMatrix: expected "
               << nTasks << " but got " << contribMatrix[0].size();
  }

  if (crossCost.size() != nTasks) {
    LOG(FATAL) << "Wrong first dimension of the crossCost: expected " << nTasks
               << " but got " << crossCost.size();
  }
  if (crossCost[0].size() != nTasks) {
    LOG(FATAL) << "Wrong second dimension of the crossCost: expected " << nTasks
               << " but got " << crossCost[0].size();
  }

  if (capacities.size() != nTasks) {
    LOG(FATAL) << "Must provide capacity of all tasks. Got "
               << capacities.size() << " capacities instead of " << nTasks;
  }

  auto getId = [nb_spots = nTasks](int ally, int enemy) {
    return ally * nb_spots + enemy;
  };

  std::vector<std::vector<double>> affinityMatrix_(
      nAgents, std::vector<double>(nTasks, 0));
  double minCoef = 1e100;
  double maxCoef = -1e100;
  if (normalize) {
    for (size_t i = 0; i < nAgents; ++i) {
      for (size_t j = 0; j < nTasks; ++j) {
        minCoef = std::min(minCoef, affinityMatrix[i][j]);
        maxCoef = std::max(maxCoef, affinityMatrix[i][j]);
      }
    }
    for (size_t i = 0; i < nTasks; ++i) {
      for (size_t j = 0; j < nTasks; ++j) {
        minCoef = std::min(minCoef, crossCost[i][j]);
        maxCoef = std::max(maxCoef, crossCost[i][j]);
      }
    }
    minCoef -= std::abs(0.1 * minCoef);
  }
  torch::Tensor affinity = torch::zeros(nbVars, torch::kDouble);
  affinity.set_requires_grad(false);
  torch::Tensor costs = torch::zeros({nbVars, nbVars}, torch::kDouble);
  costs.set_requires_grad(false);

  auto costs_acc = costs.accessor<double, 2>();
  auto affinity_acc = affinity.accessor<double, 1>();

  for (size_t i = 0; i < nAgents; ++i) {
    for (size_t j = 0; j < nTasks; ++j) {
      if (normalize) {
        affinityMatrix_[i][j] =
            100.0 * (affinityMatrix[i][j] - minCoef) / (maxCoef - minCoef);
      } else {
        affinityMatrix_[i][j] = affinityMatrix[i][j];
      }
      affinity_acc[getId(i, j)] = affinityMatrix_[i][j];
    }
  }
  for (size_t i = 0; i < nAgents; ++i) {
    for (size_t j = 0; j < nTasks; ++j) {
      for (size_t k = 0; k < nAgents; ++k) {
        for (size_t l = 0; l < nTasks; ++l) {
          if (normalize) {
            costs_acc[getId(i, j)][getId(k, l)] =
                100.0 * (crossCost[j][l] - minCoef) / (maxCoef - minCoef);
          } else {
            costs_acc[getId(i, j)][getId(k, l)] = crossCost[j][l];
          }
        }
      }
    }
  }

  costs = costs.to(torch::kCUDA);
  affinity = affinity.to(torch::kCUDA);

  MPSolver solver("helperLP", MPSolver::GLOP_LINEAR_PROGRAMMING);
  std::vector<MPVariable*> allVars;
  std::vector<MPConstraint*> allCst;
  std::tie(allVars, allCst) = setupLP(solver, contribMatrix, capacities);

  // we solve the linear part of the LP, to get an initial point
  auto init =
      solveLinearWithLP(affinityMatrix_, contribMatrix, capacities, false)
          .first;
  torch::Tensor currentPt = torch::zeros((int)init.size(), torch::kDouble);
  currentPt.set_requires_grad(false);
  for (size_t i = 0; i < init.size(); ++i) {
    currentPt[i] = init[i];
  }
  currentPt = currentPt.to(torch::kCUDA);

  // this evaluates the objective function of a point
  auto obj = [&affinity, &costs, nbVars](const torch::Tensor& pt) {
    return (pt.dot(affinity) -
            (torch::mm(
                pt.view({1, nbVars}), torch::mm(costs, pt.view({nbVars, 1})))))
        .item<double>();
  };

  auto costs_sym = costs + costs.transpose(0, 1);

  // This is the beginning of the Frank-Wolf algorithm
  MPObjective* const objective = solver.MutableObjective();

  std::vector<std::pair<torch::Tensor, double>> supportSet;
  supportSet.push_back({currentPt.clone(), 1});

  int count = 0;
  for (int step = 0; step < 600; step++) {
    // compute the gradient of the objective in the current point
    torch::Tensor grad =
        (affinity - torch::mm(costs_sym, currentPt.view({nbVars, 1})).view(-1));

    // FW algorithm finds the direction in which to move the
    // point by maximizing the taylor expansion of the function
    // around the current point x. That is, max_y f(x) + \grad
    // f(x) (y - x) with y subject to the constraints of the
    // problem
    // This is equivalent to maximizing \grad f(x) * y, which we
    // do using the lp solver

    auto grad_cpu = grad.to(torch::kCPU);
    auto grad_acc = grad_cpu.accessor<double, 1>();

    objective->Clear();
    for (size_t i = 0; i < allVars.size(); ++i) {
      objective->SetCoefficient(allVars[i], grad_acc[i]);
    }
    objective->SetMaximization();
    const MPSolver::ResultStatus result_status = solver.Solve();
    if (result_status != MPSolver::OPTIMAL) {
      std::cout << "FATAL: Something went wrong when solving FW LP. "
                << std::endl;
      break;
    }

    torch::Tensor s = torch::zeros(currentPt.size(0), torch::kDouble);
    s.set_requires_grad(false);
    for (size_t i = 0; i < allVars.size(); ++i) {
      s[i] = allVars[i]->solution_value();
    }
    s = s.to(torch::kCUDA);

    // the optimization direction is d = s - x
    torch::Tensor FW_direction = s - currentPt;

    double FW_gap = grad.dot(FW_direction).item<double>();

    // std::cout << "FW gap2 " << FW_gap << std::endl;
    if (FW_gap < 1e-5) {
      break;
    }

    // we also compute an away direction
    torch::Tensor bestAway = supportSet.front().first;
    double bestAwayScore = grad.dot(bestAway).item<double>();
    size_t idBest = 0;
    size_t curId = 0;
    for (const auto& elem : supportSet) {
      double curScore = grad.dot(elem.first).item<double>();
      if (curScore < bestAwayScore ||
          (std::abs(curScore - bestAwayScore) < 1e-4 &&
           elem.second > supportSet[idBest].second)) {
        bestAwayScore = curScore;
        bestAway = elem.first;
        idBest = curId;
      }
      curId++;
    }

    // the away direction is d = x - bestAway
    torch::Tensor away_direction = currentPt - bestAway;

    torch::Tensor direction = s - bestAway;
    double max_step_size = supportSet[idBest].second;

    double step_size = max_step_size;

    double coef = (torch::mm(
                       direction.view({1, nbVars}),
                       torch::mm(costs, direction.view({nbVars, 1}))))
                      .item<double>();
    coef *= -2.;

    if (coef > -1e-4) {
      step_size = max_step_size;
    } else {
      double dirCx = (torch::mm(
                          direction.view({1, nbVars}),
                          torch::mm(costs, currentPt.view({nbVars, 1}))))
                         .item<double>();
      double xCdir = (torch::mm(
                          currentPt.view({1, nbVars}),
                          torch::mm(costs, direction.view({nbVars, 1}))))
                         .item<double>();
      double Adir = (direction.dot(affinity)).item<double>();

      step_size = -(Adir - dirCx - xCdir) / coef;
      step_size = std::min(step_size, max_step_size);
    }
    // std::cout << "stepsize found " << step_size << std::endl;
    currentPt += step_size * direction;

    if (step_size < 1e-4)
      break;
    // std::cout << "new value " << obj(currentPt) << std::endl;

    // update the support set
    supportSet[idBest].second -= step_size;
    if (supportSet[idBest].second < 1e-4) {
      supportSet.erase(supportSet.begin() + idBest);
    }
    // search if s is an existing atom
    int id = -1;
    for (size_t i = 0; i < supportSet.size(); ++i) {
      double diff = (supportSet[i].first - s).abs().max().item<double>();
      if (diff < 1e-5) {
        id = (int)i;
        break;
      }
    }
    if (id == -1) {
      supportSet.push_back({s.clone(), step_size});
    } else {
      supportSet[id].second += step_size;
    }

    /*
    torch::Tensor sum = torch::zeros(nbVars, torch::kDouble).to(torch::kCUDA);
    for (const auto& pt : supportSet) {
      sum += pt.second * pt.first;
    }
    std::cout << "reconstruction error _torch"
              << (currentPt - sum).abs().sum().item<double>() << std::endl;
    */

    count++;
  }
  /*std::cout << "fw4Torch: finished in " << count << " steps. Reached "
            << obj(currentPt) << std::endl;
  */

  std::vector<std::vector<double>> matrix(
      nAgents, std::vector<double>(nTasks, 0));

  currentPt = currentPt.to(torch::kCPU);
  auto currentPt_acc = currentPt.accessor<double, 1>();

  // LOG(INFO) << "assignment";
  for (size_t i = 0; i < nAgents; ++i) {
    // std::stringstream ss;
    for (size_t j = 0; j < nTasks; ++j) {
      matrix[i][j] = currentPt_acc[getId(i, j)];
      // ss << matrix[i][j] << " ";
    }
    //    LOG(INFO) << ss.str();
  }

  return retrieveAssignment(matrix, contribMatrix, capacities);
}

Assign solveWithFW4(
    const std::vector<std::vector<double>>& affinityMatrix,
    const std::vector<std::vector<double>>& crossCost,
    const std::vector<std::vector<double>>& contribMatrix,
    const std::vector<double>& capacities,
    bool normalize = false) {
  const size_t nAgents = affinityMatrix.size();
  const size_t nTasks = affinityMatrix[0].size();
  int nbVars = (nTasks)*nAgents;

  if (contribMatrix.size() != nAgents) {
    LOG(FATAL) << "Wrong first dimension of the contribMatrix: expected "
               << nAgents << " but got " << contribMatrix.size();
  }
  if (contribMatrix[0].size() != nTasks) {
    LOG(FATAL) << "Wrong second dimension of the contribMatrix: expected "
               << nTasks << " but got " << contribMatrix[0].size();
  }

  if (crossCost.size() != nTasks) {
    LOG(FATAL) << "Wrong first dimension of the crossCost: expected " << nTasks
               << " but got " << crossCost.size();
  }
  if (crossCost[0].size() != nTasks) {
    LOG(FATAL) << "Wrong second dimension of the crossCost: expected " << nTasks
               << " but got " << crossCost[0].size();
  }

  if (capacities.size() != nTasks) {
    LOG(FATAL) << "Must provide capacity of all tasks. Got "
               << capacities.size() << " capacities instead of " << nTasks;
  }

  auto getId = [nb_spots = nTasks](int ally, int enemy) {
    return ally * nb_spots + enemy;
  };

  std::vector<std::vector<double>> affinityMatrix_(
      nAgents, std::vector<double>(nTasks, 0));
  double minCoef = 1e100;
  double maxCoef = -1e100;
  if (normalize) {
    for (size_t i = 0; i < nAgents; ++i) {
      for (size_t j = 0; j < nTasks; ++j) {
        minCoef = std::min(minCoef, affinityMatrix[i][j]);
        maxCoef = std::max(maxCoef, affinityMatrix[i][j]);
      }
    }
    for (size_t i = 0; i < nTasks; ++i) {
      for (size_t j = 0; j < nTasks; ++j) {
        minCoef = std::min(minCoef, crossCost[i][j]);
        maxCoef = std::max(maxCoef, crossCost[i][j]);
      }
    }
    minCoef -= std::abs(0.1 * minCoef);
  }
  VectorXd affinity(nbVars);
  MatrixXd costs(nbVars, nbVars);
  for (size_t i = 0; i < nAgents; ++i) {
    for (size_t j = 0; j < nTasks; ++j) {
      if (normalize) {
        affinityMatrix_[i][j] =
            100.0 * (affinityMatrix[i][j] - minCoef) / (maxCoef - minCoef);
      } else {
        affinityMatrix_[i][j] = affinityMatrix[i][j];
      }
      affinity(getId(i, j)) = affinityMatrix_[i][j];
    }
  }
  for (size_t i = 0; i < nAgents; ++i) {
    for (size_t j = 0; j < nTasks; ++j) {
      for (size_t k = 0; k < nAgents; ++k) {
        for (size_t l = 0; l < nTasks; ++l) {
          if (normalize) {
            costs(getId(i, j), getId(k, l)) =
                100.0 * (crossCost[j][l] - minCoef) / (maxCoef - minCoef);
          } else {
            costs(getId(i, j), getId(k, l)) = crossCost[j][l];
          }
        }
      }
    }
  }

  MPSolver solver("helperLP", MPSolver::GLOP_LINEAR_PROGRAMMING);
  std::vector<MPVariable*> allVars;
  std::vector<MPConstraint*> allCst;
  std::tie(allVars, allCst) = setupLP(solver, contribMatrix, capacities);

  // we have nAgents constraints for at most 1 task per agent and nTasks
  // constraints to make sure each the capacity is respected
  // The box constraints 0<=x_i<=1 are enforced using clamps
  int nbCst = nAgents + nTasks;
  MatrixXd constraints(nbCst, nbVars);
  constraints = MatrixXd::Zero(nbCst, nbVars);
  VectorXd coeffs(nbCst);

  int offset = 0;
  for (size_t i = 0; i < nAgents; ++i) {
    coeffs(offset + i) = 1;
    for (size_t j = 0; j < nTasks; ++j) {
      constraints(offset + i, getId(i, j)) = 1;
    }
  }

  offset += nAgents;
  for (size_t j = 0; j < nTasks; ++j) {
    coeffs(offset + j) = capacities[j];
    for (size_t i = 0; i < nAgents; ++i) {
      constraints(offset + j, getId(i, j)) = contribMatrix[i][j];
    }
  }
  std::vector<Eigen::Hyperplane<double, Eigen::Dynamic>> planes;

  for (int i = 0; i < nbCst; ++i) {
    double n = constraints.row(i).norm();
    planes.emplace_back(constraints.row(i).normalized(), -coeffs(i) / n);
  }

  // we solve the linear part of the LP, to get an initial point
  auto init =
      solveLinearWithLP(affinityMatrix_, contribMatrix, capacities, false)
          .first;
  VectorXd currentPt((int)init.size());
  for (size_t i = 0; i < init.size(); ++i) {
    currentPt(i) = init[i];
  }

  // This is a projection to the convex search space, defined by the
  // constraints. We iteratively project on all the hyperplanes sequentially
  // over and over until convergence
  auto project = [&planes, &coeffs, &constraints, nbCst](
                     VectorXd currentPt, bool precise = false) {
    double eps = precise ? 1e-6 : 1e-4;
    bool converged = false;
    for (int k = 0; k < 100000 && !converged; k++) {
      converged = true;
      // clamping to enforce [0,1] range of all the variables
      currentPt = currentPt.unaryExpr(
          [](double a) { return std::min(1., std::max(0., a)); });
      for (int i = 0; i < nbCst; ++i) {
        // compute distance to the plane
        double value = constraints.row(i).dot(currentPt);
        if (value > coeffs(i) + eps) {
          // if outside the constraint (with a bit of slack), project back
          currentPt = planes[i].projection(currentPt);
          converged = false;
        }
      }
    }
    return currentPt;
  };

  // this evaluates the objective function of a point
  auto obj = [&affinity, &costs](const VectorXd& pt) {
    return pt.dot(affinity) - (pt.transpose() * costs * pt);
  };
  currentPt = project(currentPt);

  MatrixXd costs_sym = costs + costs.transpose();
  // std::cout << "initial value " << obj(currentPt) << std::endl;

  // This is the beginning of the Frank-Wolf algorithm
  MPObjective* const objective = solver.MutableObjective();

  std::vector<std::pair<VectorXd, double>> supportSet;
  supportSet.push_back({currentPt, 1});

  int count = 0;
  for (int step = 0; step < 600; step++) {
    // compute the gradient of the objective in the current point
    VectorXd grad = (affinity - costs_sym * currentPt);

    // FW algorithm finds the direction in which to move the
    // point by maximizing the taylor expansion of the function
    // around the current point x. That is, max_y f(x) + \grad
    // f(x) (y - x) with y subject to the constraints of the
    // problem
    // This is equivalent to maximizing \grad f(x) * y, which we
    // do using the lp solver

    objective->Clear();
    for (size_t i = 0; i < allVars.size(); ++i) {
      objective->SetCoefficient(allVars[i], grad(i));
    }
    objective->SetMaximization();
    const MPSolver::ResultStatus result_status = solver.Solve();
    if (result_status != MPSolver::OPTIMAL) {
      std::cout << "FATAL: Something went wrong when solving FW LP. "
                << std::endl;
      break;
    }

    VectorXd s(currentPt.size());
    for (size_t i = 0; i < allVars.size(); ++i) {
      s(i) = allVars[i]->solution_value();
    }

    // the optimization direction is d = s - x
    VectorXd FW_direction = s - currentPt;

    double FW_gap = grad.dot(FW_direction);
    // std::cout << "FW gap2 " << FW_gap << std::endl;
    if (FW_gap < 1e-5) {
      break;
    }

    // we also compute an away direction
    VectorXd bestAway = supportSet.front().first;
    double bestAwayScore = grad.dot(bestAway);
    size_t idBest = 0;
    size_t curId = 0;
    for (const auto& elem : supportSet) {
      double curScore = grad.dot(elem.first);
      // if (curScore < bestAwayScore) {
      if (curScore < bestAwayScore ||
          (std::abs(curScore - bestAwayScore) < 1e-5 &&
           elem.second > supportSet[idBest].second)) {
        bestAwayScore = curScore;
        bestAway = elem.first;
        idBest = curId;
      }
      curId++;
    }

    // the away direction is d = x - bestAway
    VectorXd away_direction = currentPt - bestAway;

    VectorXd direction = s - bestAway;
    double max_step_size = supportSet[idBest].second;

    // std::cout << "max_step_size " << max_step_size << std::endl;

    double step_size = max_step_size;

    double coef = (direction.transpose() * costs * direction);
    coef *= -2.;

    if (coef > 0) {
      step_size = max_step_size;
    } else {
      double dirCx = direction.transpose() * costs * currentPt;
      double xCdir = currentPt.transpose() * costs * direction;
      double Adir = direction.dot(affinity);
      step_size = -(Adir - dirCx - xCdir) / coef;
      step_size = std::min(step_size, max_step_size);
    }
    // std::cout << "stepsize found " << step_size << std::endl;
    currentPt.noalias() += step_size * direction;

    if (step_size < 1e-4)
      break;
    // std::cout << "new value " << obj(currentPt) << std::endl;

    // update the support set
    supportSet[idBest].second -= step_size;
    if (supportSet[idBest].second < 1e-4) {
      supportSet.erase(supportSet.begin() + idBest);
    }
    // search if s is an existing atom
    int id = -1;
    for (size_t i = 0; i < supportSet.size(); ++i) {
      double diff = (supportSet[i].first - s).cwiseAbs().maxCoeff();
      if (diff < 1e-5) {
        id = (int)i;
        // std::cout << "FOUND!!!!!" << std::endl;
        break;
      }
    }
    if (id == -1) {
      supportSet.push_back({s, step_size});
    } else {
      supportSet[id].second += step_size;
    }

    /*
    VectorXd sum(currentPt.size());
    sum = sum * 0;
    for (const auto& pt : supportSet) {
      sum += pt.second * pt.first;
    }
    std::cout << "reconstruction error " << (currentPt - sum).cwiseAbs().sum()
              << std::endl;
    */

    count++;
  }
  currentPt = project(currentPt, true);

  std::vector<std::vector<double>> matrix(
      nAgents, std::vector<double>(nTasks, 0));

  // LOG(INFO) << "assignment";
  for (size_t i = 0; i < nAgents; ++i) {
    // std::stringstream ss;
    for (size_t j = 0; j < nTasks; ++j) {
      matrix[i][j] = currentPt(getId(i, j));
      // ss << matrix[i][j] << " ";
    }
    //    LOG(INFO) << ss.str();
  }

  return retrieveAssignment(matrix, contribMatrix, capacities);
}

struct Individual {
  std::vector<double> remaining_capa;
  Assign assign;
  std::vector<std::vector<size_t>>
      back_assign; // ids of the agents affected to each task
  size_t nAgents;
  size_t nTasks;
  double score = -100000;
  std::vector<bool> taboo_insert, taboo_swap;
  void fillAssign() {
    assign.resize(nAgents, {0, 0.});
    std::fill(assign.begin(), assign.end(), std::pair<int, double>({0, 0.}));
    for (size_t i = 0; i < nTasks; ++i) {
      for (size_t a : back_assign[i]) {
        assign[a] = {i, 1.};
      }
    }
  }

  void recomputeCapa(
      const std::vector<std::vector<double>>& contribMatrix,
      const std::vector<double>& capacities) {
    std::copy(capacities.begin(), capacities.end(), remaining_capa.begin());
    for (size_t i = 0; i < assign.size(); ++i) {
      if (assign[i].second < 0.1)
        continue;
      remaining_capa[assign[i].first] -= contribMatrix[i][assign[i].first];
    }
  }

  void fillBackAssign() {
    back_assign.resize(nTasks, std::vector<size_t>());
    std::fill(back_assign.begin(), back_assign.end(), std::vector<size_t>());
    for (size_t i = 0; i < nAgents; ++i) {
      if (assign[i].second > 0.1) {
        back_assign[assign[i].first].push_back(i);
      }
    }
  }

  void reset_taboo() {
    std::fill(taboo_insert.begin(), taboo_insert.end(), false);
    std::fill(taboo_swap.begin(), taboo_swap.end(), false);
  }
};
double scoreAssign(
    const Assign& assign,
    const std::vector<std::vector<double>>& affinityMatrix,
    const std::vector<std::vector<double>>& crossCost,
    const std::vector<std::vector<double>>& contribMatrix,
    std::vector<double> capacities) {
  double tot_score = 0.;

  for (size_t i = 0; i < assign.size(); ++i) {
    if (assign[i].second < 0.1)
      continue;

    capacities[assign[i].first] -= contribMatrix[i][assign[i].first];
    if (capacities[assign[i].first] < 0) {
      std::cout << "Wrong assignment" << std::endl;
      throw 0;
    }
    tot_score += affinityMatrix[i][assign[i].first];
    for (size_t j = 0; j < assign.size(); ++j) {
      if (assign[j].second >= 0.1) {
        tot_score -= crossCost[assign[i].first][assign[j].first];
      }
    }
  }
  return tot_score;
}
Assign solveWithGenetic(
    std::vector<std::vector<double>> affinityMatrix,
    std::vector<std::vector<double>> crossCost,
    const std::vector<std::vector<double>>& contribMatrix,
    const std::vector<double>& capacities,
    bool normalize = false) {
  const size_t nAgents = affinityMatrix.size();
  const size_t nTasks = affinityMatrix[0].size();
  int nbVars = (nTasks)*nAgents;

  if (contribMatrix.size() != nAgents) {
    LOG(FATAL) << "Wrong first dimension of the contribMatrix: expected "
               << nAgents << " but got " << contribMatrix.size();
  }
  if (contribMatrix[0].size() != nTasks) {
    LOG(FATAL) << "Wrong second dimension of the contribMatrix: expected "
               << nTasks << " but got " << contribMatrix[0].size();
  }

  if (crossCost.size() != nTasks) {
    LOG(FATAL) << "Wrong first dimension of the crossCost: expected " << nTasks
               << " but got " << crossCost.size();
  }
  if (crossCost[0].size() != nTasks) {
    LOG(FATAL) << "Wrong second dimension of the crossCost: expected " << nTasks
               << " but got " << crossCost[0].size();
  }

  if (capacities.size() != nTasks) {
    LOG(FATAL) << "Must provide capacity of all tasks. Got "
               << capacities.size() << " capacities instead of " << nTasks;
  }

  double minCoef = 1e100;
  double maxCoef = -1e100;
  if (normalize) {
    for (size_t i = 0; i < nAgents; ++i) {
      for (size_t j = 0; j < nTasks; ++j) {
        minCoef = std::min(minCoef, affinityMatrix[i][j]);
        maxCoef = std::max(maxCoef, affinityMatrix[i][j]);
      }
    }
    for (size_t i = 0; i < nTasks; ++i) {
      for (size_t j = 0; j < nTasks; ++j) {
        minCoef = std::min(minCoef, crossCost[i][j]);
        maxCoef = std::max(maxCoef, crossCost[i][j]);
      }
    }
    minCoef -= std::abs(0.1 * minCoef);
    for (size_t i = 0; i < nAgents; ++i) {
      for (size_t j = 0; j < nTasks; ++j) {
        affinityMatrix[i][j] =
            100.0 * (affinityMatrix[i][j] - minCoef) / (maxCoef - minCoef);
      }
    }
    for (size_t j = 0; j < nTasks; ++j) {
      for (size_t l = 0; l < nTasks; ++l) {
        crossCost[j][l] =
            100.0 * (crossCost[j][l] - minCoef) / (maxCoef - minCoef);
      }
    }
  }

  std::minstd_rand0 gen = common::Rand::makeRandEngine<std::minstd_rand0>();

  const size_t N = 30;
  std::uniform_int_distribution<size_t> agent_dist(0, nAgents - 1);
  std::uniform_int_distribution<size_t> task_dist(0, nTasks - 1);
  std::uniform_int_distribution<size_t> pop_dist(0, N - 1);
  std::uniform_real_distribution<float> float_dist(0., 1.);
  std::bernoulli_distribution choice(0.5);

  Assign best_assign(nAgents, {0, 0.});

  // generate a random solution
  auto generate_rand = [&]() {
    Individual ind;
    ind.nAgents = nAgents;
    ind.nTasks = nTasks;
    ind.remaining_capa.resize(nTasks, 0);
    ind.assign.resize(nAgents, {0, 0.});
    std::copy(capacities.begin(), capacities.end(), ind.remaining_capa.begin());
    for (size_t i = 0; i < nAgents; ++i) {
      int tries = 3;
      while (tries-- > 0) {
        size_t j = task_dist(gen);
        if (ind.remaining_capa[j] >= contribMatrix[i][j]) {
          ind.remaining_capa[j] -= contribMatrix[i][j];
          ind.assign[i] = {j, 1.};
          break;
        } else {
          ind.assign[i] = {0, 0.};
        }
      }
    }
    ind.fillBackAssign();
    ind.score = scoreAssign(
        ind.assign, affinityMatrix, crossCost, contribMatrix, capacities);
    ind.taboo_insert = std::vector<bool>(nAgents, false);
    ind.taboo_swap = std::vector<bool>(nAgents, false);
    return ind;
  };

  std::vector<Individual> pop;
  pop.reserve(N);
  for (size_t i = 0; i < N; ++i) {
    pop.push_back(generate_rand());
  }

  Individual best_ind = pop.front();

  auto apply_transition =
      [&](Individual& ind,
          const std::vector<std::pair<size_t, size_t>>& transition) {
        double delta = 0;
        for (const auto& t : transition) {
          if (ind.assign[t.first].second > 0.1) {
            size_t cur_task = ind.assign[t.first].first;
            delta -= affinityMatrix[t.first][cur_task];
            for (size_t i = 0; i < nAgents; ++i) {
              if (ind.assign[i].second > 0.1) {
                delta += crossCost[cur_task][ind.assign[i].first];
                if (i != t.first) {
                  delta += crossCost[ind.assign[i].first][cur_task];
                }
              }
            }
            ind.remaining_capa[cur_task] += contribMatrix[t.first][cur_task];
            ind.assign[t.first] = {0, 0.};
          }
          if (t.second != -1) {
            size_t cur_task = t.second;
            delta += affinityMatrix[t.first][cur_task];
            delta -= crossCost[cur_task][cur_task];
            for (size_t i = 0; i < nAgents; ++i) {
              if (ind.assign[i].second > 0.1) {
                delta -= crossCost[cur_task][ind.assign[i].first];
                if (i != t.first) {
                  delta -= crossCost[ind.assign[i].first][cur_task];
                }
              }
            }
            ind.remaining_capa[cur_task] -= contribMatrix[t.first][cur_task];
            ind.assign[t.first] = {cur_task, 1.};
          }
        }
        return delta;
      };

  auto try_insert = [&](Individual& ind, bool allow_rand = false) {
    std::vector<std::pair<size_t, size_t>> transition, reverse;

    bool found = false;
    std::vector<size_t> agents(nAgents);
    std::iota(agents.begin(), agents.end(), 0);
    std::shuffle(agents.begin(), agents.end(), gen);
    for (size_t s = 0; s < nAgents; ++s) {
      size_t source = agents[s];
      if (ind.taboo_insert[source]) {
        continue;
      }
      if (ind.assign[source].second > 0.1) {
        // already assigned, going to deassign
        size_t old_task = ind.assign[source].first;
        transition.clear();
        reverse.clear();
        transition.push_back({source, -1});
        reverse.push_back({source, old_task});

        double delta = apply_transition(ind, transition);
        if (delta > 1e-4 || allow_rand) {
          ind.score += delta;
          ind.reset_taboo();
          found = true;
          return true;
        } else {
          apply_transition(ind, reverse);
        }
      } else {
        // not assigned, trying to find a suitable target
        std::vector<size_t> tasks(nTasks);
        std::iota(tasks.begin(), tasks.end(), 0);
        std::shuffle(tasks.begin(), tasks.end(), gen);
        for (size_t kk = 0; kk < nTasks; ++kk) {
          size_t k = tasks[kk];
          if (ind.remaining_capa[k] >= contribMatrix[source][k]) {
            transition.clear();
            reverse.clear();
            transition.push_back({source, k});
            reverse.push_back({source, -1});
            double delta = apply_transition(ind, transition);
            if (delta > 1e-4 || allow_rand) {
              ind.score += delta;
              found = true;
              ind.reset_taboo();
              return true;
              break;
            } else {
              apply_transition(ind, reverse);
            }
          }
        }
      }
      ind.taboo_insert[source] = true;
    }
    return found;
  };

  auto try_swap = [&](Individual& ind, bool allow_rand = false) {
    std::vector<std::pair<size_t, size_t>> transition, reverse;
    bool found = false;

    // try to swap
    std::vector<size_t> agents(nAgents);
    std::iota(agents.begin(), agents.end(), 0);
    std::shuffle(agents.begin(), agents.end(), gen);
    for (size_t s = 0; s < nAgents; ++s) {
      size_t source = agents[s];
      if (ind.assign[source].second < 0.1) {
        continue;
      }
      if (ind.taboo_swap[source]) {
        continue;
      }
      // we find the compatible agents
      for (size_t kk = 0; kk < nAgents; ++kk) {
        size_t k = agents[kk];
        if (ind.assign[source].second < 0.1) {
          break;
        }
        size_t current_task = ind.assign[source].first;

        if (k == source)
          continue;
        double slack_source = ind.remaining_capa[current_task] +
            contribMatrix[source][current_task] -
            contribMatrix[k][current_task];
        if (slack_source < 0)
          continue;
        size_t target_task = ind.assign[k].first;
        if (current_task == target_task)
          continue;
        double slack_target = ind.remaining_capa[target_task] +
            contribMatrix[k][target_task] - contribMatrix[source][target_task];
        if (slack_target >= 0) {
          transition.clear();
          reverse.clear();
          transition.push_back({k, current_task});
          reverse.push_back({source, current_task});
          if (ind.assign[k].second > 0.1) {
            transition.push_back({source, target_task});
            reverse.push_back({k, target_task});
          } else {
            transition.push_back({source, -1});
            reverse.push_back({k, -1});
            // std::cout << "swap out" << std::endl;
          }
          double delta = apply_transition(ind, transition);
          if (delta > 1e-4 || allow_rand) {
            ind.score += delta;
            ind.reset_taboo();
            found = true;
            return true;
          } else {
            apply_transition(ind, reverse);
          }
        }
      }
      ind.taboo_swap[source] = true;
    }
    return found;
  };

  auto mutate = [&](Individual& ind, bool allow_rand = false) {
    bool found = true;
    for (size_t i = 0; i < 10 && found; ++i) {
      found = false;
      if (choice(gen)) {
        if (try_insert(ind, allow_rand)) {
          found = true;
        }
        if (try_swap(ind, allow_rand)) {
          found = true;
        }
      } else {
        if (try_swap(ind, allow_rand)) {
          found = true;
        }
        if (try_insert(ind, allow_rand)) {
          found = true;
        }
      }
    }
    return found;
  };

  auto crossOver_old = [&](Individual& ind, Individual& indB) {
    Individual offSpring = ind;
    for (size_t i = 0; i < nTasks; ++i) {
      if (choice(gen)) {
        offSpring.back_assign[i] = indB.back_assign[i];
      }
    }
    offSpring.fillAssign();
    offSpring.recomputeCapa(contribMatrix, capacities);
    offSpring.score = scoreAssign(
        offSpring.assign, affinityMatrix, crossCost, contribMatrix, capacities);
    return offSpring;
  };
  auto crossOver = [&](Individual& ind, Individual& indB) {
    Individual offSpring = ind;
    std::copy(
        capacities.begin(), capacities.end(), offSpring.remaining_capa.begin());

    std::vector<size_t> agents(nAgents);
    std::iota(agents.begin(), agents.end(), 0);
    std::shuffle(agents.begin(), agents.end(), gen);
    offSpring.reset_taboo();

    for (size_t i = 0; i < nAgents; ++i) {
      size_t agent = agents[i];
      size_t task = ind.assign[i].first;
      if (choice(gen)) {
        task = indB.assign[i].first;
      }
      offSpring.assign[i].first = task;
      offSpring.assign[i].second = 0.;
      if (offSpring.remaining_capa[task] - contribMatrix[agent][task] >= 0) {
        offSpring.assign[i].second = 1.;
        offSpring.remaining_capa[task] -= contribMatrix[agent][task];
      }
    }
    offSpring.score = scoreAssign(
        offSpring.assign, affinityMatrix, crossCost, contribMatrix, capacities);
    return offSpring;
  };

  int tries = 100;
  int stale_count = 0;
  while (tries > 0) {
    bool found = false;
    std::vector<double> weights;
    weights.reserve(N);
    for (size_t i = 0; i < N; ++i) {
      weights.push_back(pop[i].score);
    }
    std::discrete_distribution<size_t> biased_pop_dist(
        weights.begin(), weights.end());

    for (size_t i = 0; i < N; ++i) {
      pop.push_back(
          crossOver(pop[biased_pop_dist(gen)], pop[biased_pop_dist(gen)]));
      /*
      pop.push_back(crossOver(pop[pop_dist(gen)], pop[pop_dist(gen)]));
      Individual new_ind = pop[i];
      mutate(new_ind, true);
      pop.push_back(new_ind);
      */
    }
    for (auto& ind : pop) {
      mutate(ind);
    }
    std::shuffle(pop.begin(), pop.end(), gen);

    // tournament based selection
    for (size_t i = 0; i < N; ++i) {
      if (pop[i].score < pop[i + N].score) {
        std::swap(pop[i], pop[i + N]);
      }
    }
    pop.resize(N);

    std::sort(
        pop.begin(), pop.end(), [&](const Individual& a, const Individual& b) {
          return a.score < b.score;
        });
    pop.erase(
        std::unique(
            pop.begin(),
            pop.end(),
            [&](const Individual& a, const Individual& b) {
              return std::abs(a.score - b.score) < 1e-5;
            }),
        pop.end());

    while (pop.size() < N) {
      pop.push_back(crossOver(
          pop[pop_dist(gen) % pop.size()], pop[pop_dist(gen) % pop.size()]));
    }

    Individual& local_best = pop.front();
    for (auto& ind : pop) {
      if (ind.score > local_best.score) {
        local_best = ind;
      }
    }
    if (local_best.score > best_ind.score) {
      best_ind = local_best;
      stale_count = 0;
    } else {
      stale_count++;
      if (stale_count > 10) {
        break;
      }
    }
    tries--;
  }
  while (mutate(best_ind)) {
  }
  return best_ind.assign;
}

} // namespace

Assign solveQuad(
    const std::vector<std::vector<double>>& affinityMatrix,
    const std::vector<std::vector<double>>& crossCost,
    const std::vector<std::vector<double>>& contribMatrix,
    const std::vector<double>& capacities,
    bool normalize) {
  if (FLAGS_use_ga) {
    return solveWithGenetic(
        affinityMatrix, crossCost, contribMatrix, capacities, normalize);
  }
  return solveWithFW4(
      affinityMatrix, crossCost, contribMatrix, capacities, normalize);
}
