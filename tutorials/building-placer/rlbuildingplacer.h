/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "models/buildingplacer.h"
#include "module.h"
#include "upcstorage.h"

#include <cpid/trainer.h>

namespace cherrypi {

/**
 * UPC post data attached to posts from RLBuildingPlacerModule.
 *
 * This is used to record decisions taken by RLBuildingPlacerModule and includes
 * a sample of the relevant state as well as tracking information for easy
 * shaping.
 *
 * Instances posted to the blackboard will be modified as the game progresses,
 * so it's recommended to only collect them and the end of the game.
 */
struct RLBPUpcData : public UpcPostData {
  BuildType const* type;
  BuildingPlacerSample sample; /// (featurized) state and action
  ag::Variant output; /// trainer output (action, probability distribution, ...)
  /// true if not cancelled and actually picked up by builder, or if location
  /// was invalid from the start. This indicates that the sample contains
  /// useful signal for training.
  bool valid = false;
  bool started = false;
  bool finished = false;

  RLBPUpcData(
      BuildType const* type,
      BuildingPlacerSample sample,
      ag::Variant output)
      : type(type), sample(std::move(sample)), output(output) {}

  virtual ~RLBPUpcData() = default;
};

/**
 * A building placement module with reinforcement learning support.
 *
 * The module can be run with either a trainer instance (`setTrainer()`) or just
 * a model (`setModel()`). With a trainer, action selection is done by the
 * trainer (e.g., argmax for evaluation mode); without a trainer, the module
 * will use sample an action from the model output if the model is in training
 * mode, or select the action with maximum probability in evaluation mode.
 *
 * The built-in placement rules are used to pre-select the desired area for
 * placement, which is then supplied to the featurizer (`BuildingPlacerSample`).
 * UPCs from this module will be posted with `RLBPUpcData` instances which
 * contain information regarding the input data, validity and outcome of each
 * action. However, note that if `UpcStorage` is used in non-persistent mode,
 * the post data will not be saved.
 *
 * By default, the module will attempt to load a building placer model from the
 * location specified by the `--rlbp_model` command-line flag. If it cannot find
 * a model, operation will fall back to use the built-in rules from the
 * builderhelpers namespace.
 */
class RLBuildingPlacerModule : public Module {
 public:
  void setTrainer(std::shared_ptr<cpid::Trainer> trainer);
  void setModel(std::shared_ptr<BuildingPlacerModel> model);
  std::shared_ptr<BuildingPlacerModel> model() const;

  virtual void step(State* state) override;
  virtual void onGameStart(State* state) override;
  virtual void onGameEnd(State* state) override;

 private:
  std::tuple<std::shared_ptr<UPCTuple>, std::shared_ptr<UpcPostData>>
  upcWithPositionForBuilding(
      State* state,
      UPCTuple const& sourceUpc,
      BuildType const* type);

  std::shared_ptr<BuildingPlacerModel> model_;
  std::shared_ptr<BuildingPlacerSample::StaticData> staticData_;
  std::shared_ptr<cpid::Trainer> trainer_ = nullptr;
  bool firstStep_ = true;
  std::unordered_set<Position> baseLocations_;
  cpid::EpisodeHandle handle_;
};

} // namespace cherrypi
