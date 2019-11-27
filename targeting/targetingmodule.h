/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 */

#pragma once

#include "common.h"
#include "cpid/trainer.h"
#include "module.h"
#include "solver.h"
#include "unitsinfo.h"
#include <autogradpp/autograd.h>

namespace cherrypi {

class TargetingModule : public Module {
 public:
  TargetingModule(
      Targeting b,
      std::shared_ptr<cpid::Trainer> trainer,
      cpid::EpisodeHandle myHandle,
      ModelType model_type)
      : Module(),
        baseline_(b),
        trainer_(trainer),
        myHandle_(std::move(myHandle)),
        model_type_(model_type) {}

  virtual ~TargetingModule() = default;
  void onGameStart(State* state) override;
  void step(State* state) override;
  void onGameEnd(State* state) override;

  void reset();

  void sendLastFrame(State* state);

 protected:
  // compute the squad reward
  float computeReward(
      const std::unordered_map<int, Unit*>& allies,
      const std::unordered_map<int, Unit*>& enemies);

  // Implementation of weakest closest heuristic
  void wc_heuristic(
      const std::unordered_map<int, Unit*>& allies,
      const std::unordered_map<int, Unit*>& enemies);

  // Implementation of even split
  void evenSplit_heuristic(
      const std::unordered_map<int, Unit*>& allies,
      const std::unordered_map<int, Unit*>& enemies);

  // Implementation of weakest closest NOK heuristic
  void wcnok_heuristic(
      const std::unordered_map<int, Unit*>& allies,
      const std::unordered_map<int, Unit*>& enemies,
      bool nochange,
      bool smart);

  // Implementation of closest heuristic
  void closest_heuristic(
      const std::unordered_map<int, Unit*>& allies,
      const std::unordered_map<int, Unit*>& enemies);

  // Implementation of random heuristic
  void random_heuristic(
      const std::unordered_map<int, Unit*>& allies,
      const std::unordered_map<int, Unit*>& enemies);

  // Implementation of random no change heuristic
  void random_nochange_heuristic(
      const std::unordered_map<int, Unit*>& allies,
      const std::unordered_map<int, Unit*>& enemies);

  // Actually plays with the model
  void play_with_model(
      State* state,
      const std::unordered_map<int, Unit*>& allies,
      const std::unordered_map<int, Unit*>& enemies);

  void play_argmax(
      State* state,
      const UnitsInfo::Units& allies,
      const UnitsInfo::Units& enemies,
      torch::Tensor out);
  void play_lp(
      State* state,
      const UnitsInfo::Units& allies,
      const UnitsInfo::Units& enemies,
      torch::Tensor out);
  void play_discrete(
      State* state,
      const UnitsInfo::Units& allies,
      const UnitsInfo::Units& enemies,
      torch::Tensor out);
  void play_quad(
      State* state,
      const UnitsInfo::Units& allies,
      const UnitsInfo::Units& enemies,
      torch::Tensor actions_lp,
      torch::Tensor actions_quad);

  // Helper function to post a sharp delete command with given source and target
  void postUpc(State* state, int srcUpcId, Unit* source, Unit* target);

  // Helper function to post a sharp delete command with given source and
  // position
  void postUpc(State* state, int srcUpcId, Unit* source, int x, int y);

  // returns a matrix contrib[i][j] = damage dealt by i to j and capa[j] = max
  // damage to affect to j
  std::pair<std::vector<std::vector<double>>, std::vector<double>>
  computeContribAndCapa(
      State* state,
      const UnitsInfo::Units& allies,
      const UnitsInfo::Units& enemies);

  // Retrieve and post-process an assignment, and store it in assignment_
  void applyAssignment(
      State* state,
      const UnitsInfo::Units& allies,
      const UnitsInfo::Units& enemies,
      const std::vector<std::vector<double>>& contribMatrix,
      std::vector<double> remaining_capa,
      const Assign& assign);

 public:
  Targeting baseline_;

  static const int kNumPairFeatures;

 private:
  std::shared_ptr<cpid::Trainer> trainer_;

  std::unordered_map<UnitId, UnitId> assignment_,
      oldAssignment_; // keys are ally units ids and values are enemy units
                      // ids.

  int lastFrame{-1000}; // number of the last frame we sent a builtinAI order

  int lastFramePlayed_{-1000}; // number of the last frame we evaluated actions

  cpid::EpisodeHandle myHandle_;
  ModelType model_type_;

  bool minSpread_;
  bool nochange_;
  bool NOK_;
  bool slack_;
  bool weakest_;

  bool started_ = false;

  int debugCount_ = 0;

  ag::Variant last_state_, last_model_out_;
  double aggregatedReward_ = 0;
  bool first_state_sent_ = false;

  // we need to remember the weights given to the linear part and the quadratic
  // part for each pair
  std::unordered_map<int, std::unordered_map<int, std::deque<float>>>
      sampling_hist_linear_, sampling_hist_quad_;

  // We remember the last enemy unitSTring + hp
  std::unordered_map<int, std::pair<std::string, int>> prevEnemyHp_;
  std::unordered_map<int, std::pair<std::string, int>> prevAllyHp_;

  int total_HP_begining_ = -1;
};

} // namespace cherrypi
