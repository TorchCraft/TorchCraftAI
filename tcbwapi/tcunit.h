/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <BWAPI.h>

#include <torchcraft/state.h>

namespace tcbwapi {

// XXX I'm not saying this is clean but it does the job for BWEM
class TCUnit : public BWAPI::UnitInterface {
 public:
  TCUnit(torchcraft::replayer::Unit u) : u_(std::move(u)) {}
  virtual ~TCUnit() {}

  //
  // Implemented methods
  //
  int getID() const override {
    return u_.id;
  }
  int getResources() const override {
    return u_.resources;
  }
  BWAPI::UnitType getType() const override {
    return BWAPI::UnitType(u_.type);
  }
  BWAPI::Position getPosition() const override {
    return BWAPI::Position(
        u_.x * torchcraft::BW::XYPixelsPerWalktile,
        u_.y * torchcraft::BW::XYPixelsPerWalktile);
  }
  bool isLifted() const override {
    return (u_.flags & torchcraft::Unit::Flags::Lifted) != 0;
  }

  // Assume we get the unit at the beginning of the game
  int getInitialResources() const override {
    return getResources();
  }
  BWAPI::TilePosition getInitialTilePosition() const override {
    return getTilePosition();
  }
  BWAPI::Position getInitialPosition() const override {
    return getPosition();
  }
  BWAPI::UnitType getInitialType() const override {
    return getType();
  }

 private:
  void throwNotImplemented() const {
    throw std::runtime_error("tcbwapi::TCUnit: Method not implemented");
  }

  torchcraft::replayer::Unit u_;

//
// Stubs
//
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#endif
 public:
  bool exists() const override {
    throwNotImplemented();
  }
  int getReplayID() const override {
    throwNotImplemented();
  }
  BWAPI::Player getPlayer() const override {
    throwNotImplemented();
  }
  double getAngle() const override {
    throwNotImplemented();
  }
  double getVelocityX() const override {
    throwNotImplemented();
  }
  double getVelocityY() const override {
    throwNotImplemented();
  }
  int getHitPoints() const override {
    throwNotImplemented();
  }
  int getShields() const override {
    throwNotImplemented();
  }
  int getEnergy() const override {
    throwNotImplemented();
  }
  int getResourceGroup() const override {
    throwNotImplemented();
  }
  int getLastCommandFrame() const override {
    throwNotImplemented();
  }
  BWAPI::UnitCommand getLastCommand() const override {
    throwNotImplemented();
  }
  BWAPI::Player getLastAttackingPlayer() const override {
    throwNotImplemented();
  }
  int getInitialHitPoints() const override {
    throwNotImplemented();
  }
  int getKillCount() const override {
    throwNotImplemented();
  }
  int getAcidSporeCount() const override {
    throwNotImplemented();
  }
  int getInterceptorCount() const override {
    throwNotImplemented();
  }
  int getScarabCount() const override {
    throwNotImplemented();
  }
  int getSpiderMineCount() const override {
    throwNotImplemented();
  }
  int getGroundWeaponCooldown() const override {
    throwNotImplemented();
  }
  int getAirWeaponCooldown() const override {
    throwNotImplemented();
  }
  int getSpellCooldown() const override {
    throwNotImplemented();
  }
  int getDefenseMatrixPoints() const override {
    throwNotImplemented();
  }
  int getDefenseMatrixTimer() const override {
    throwNotImplemented();
  }
  int getEnsnareTimer() const override {
    throwNotImplemented();
  }
  int getIrradiateTimer() const override {
    throwNotImplemented();
  }
  int getLockdownTimer() const override {
    throwNotImplemented();
  }
  int getMaelstromTimer() const override {
    throwNotImplemented();
  }
  int getOrderTimer() const override {
    throwNotImplemented();
  }
  int getPlagueTimer() const override {
    throwNotImplemented();
  }
  int getRemoveTimer() const override {
    throwNotImplemented();
  }
  int getStasisTimer() const override {
    throwNotImplemented();
  }
  int getStimTimer() const override {
    throwNotImplemented();
  }
  BWAPI::UnitType getBuildType() const override {
    throwNotImplemented();
  }
  BWAPI::UnitType::list getTrainingQueue() const override {
    throwNotImplemented();
  }
  BWAPI::TechType getTech() const override {
    throwNotImplemented();
  }
  BWAPI::UpgradeType getUpgrade() const override {
    throwNotImplemented();
  }
  int getRemainingBuildTime() const override {
    throwNotImplemented();
  }
  int getRemainingTrainTime() const override {
    throwNotImplemented();
  }
  int getRemainingResearchTime() const override {
    throwNotImplemented();
  }
  int getRemainingUpgradeTime() const override {
    throwNotImplemented();
  }
  BWAPI::Unit getBuildUnit() const override {
    throwNotImplemented();
  }
  BWAPI::Unit getTarget() const override {
    throwNotImplemented();
  }
  BWAPI::Position getTargetPosition() const override {
    throwNotImplemented();
  }
  BWAPI::Order getOrder() const override {
    throwNotImplemented();
  }
  BWAPI::Order getSecondaryOrder() const override {
    throwNotImplemented();
  }
  BWAPI::Unit getOrderTarget() const override {
    throwNotImplemented();
  }
  BWAPI::Position getOrderTargetPosition() const override {
    throwNotImplemented();
  }
  BWAPI::Position getRallyPosition() const override {
    throwNotImplemented();
  }
  BWAPI::Unit getRallyUnit() const override {
    throwNotImplemented();
  }
  BWAPI::Unit getAddon() const override {
    throwNotImplemented();
  }
  BWAPI::Unit getNydusExit() const override {
    throwNotImplemented();
  }
  BWAPI::Unit getPowerUp() const override {
    throwNotImplemented();
  }
  BWAPI::Unit getTransport() const override {
    throwNotImplemented();
  }
  BWAPI::Unitset getLoadedUnits() const override {
    throwNotImplemented();
  }
  BWAPI::Unit getCarrier() const override {
    throwNotImplemented();
  }
  BWAPI::Unitset getInterceptors() const override {
    throwNotImplemented();
  }
  BWAPI::Unit getHatchery() const override {
    throwNotImplemented();
  }
  BWAPI::Unitset getLarva() const override {
    throwNotImplemented();
  }
  bool hasNuke() const override {
    throwNotImplemented();
  }
  bool isAccelerating() const override {
    throwNotImplemented();
  }
  bool isAttacking() const override {
    throwNotImplemented();
  }
  bool isAttackFrame() const override {
    throwNotImplemented();
  }
  bool isBeingGathered() const override {
    throwNotImplemented();
  }
  bool isBeingHealed() const override {
    throwNotImplemented();
  }
  bool isBlind() const override {
    throwNotImplemented();
  }
  bool isBraking() const override {
    throwNotImplemented();
  }
  bool isBurrowed() const override {
    throwNotImplemented();
  }
  bool isCarryingGas() const override {
    throwNotImplemented();
  }
  bool isCarryingMinerals() const override {
    throwNotImplemented();
  }
  bool isCloaked() const override {
    throwNotImplemented();
  }
  bool isCompleted() const override {
    throwNotImplemented();
  }
  bool isConstructing() const override {
    throwNotImplemented();
  }
  bool isDetected() const override {
    throwNotImplemented();
  }
  bool isGatheringGas() const override {
    throwNotImplemented();
  }
  bool isGatheringMinerals() const override {
    throwNotImplemented();
  }
  bool isHallucination() const override {
    throwNotImplemented();
  }
  bool isIdle() const override {
    throwNotImplemented();
  }
  bool isInterruptible() const override {
    throwNotImplemented();
  }
  bool isInvincible() const override {
    throwNotImplemented();
  }
  bool isMorphing() const override {
    throwNotImplemented();
  }
  bool isMoving() const override {
    throwNotImplemented();
  }
  bool isParasited() const override {
    throwNotImplemented();
  }
  bool isSelected() const override {
    throwNotImplemented();
  }
  bool isStartingAttack() const override {
    throwNotImplemented();
  }
  bool isStuck() const override {
    throwNotImplemented();
  }
  bool isTraining() const override {
    throwNotImplemented();
  }
  bool isUnderAttack() const override {
    throwNotImplemented();
  }
  bool isUnderDarkSwarm() const override {
    throwNotImplemented();
  }
  bool isUnderDisruptionWeb() const override {
    throwNotImplemented();
  }
  bool isUnderStorm() const override {
    throwNotImplemented();
  }
  bool isPowered() const override {
    throwNotImplemented();
  }
  bool isVisible(BWAPI::Player player = nullptr) const override {
    throwNotImplemented();
  }
  bool isTargetable() const override {
    throwNotImplemented();
  }
  bool issueCommand(BWAPI::UnitCommand command) override {
    throwNotImplemented();
  }
  bool canIssueCommand(
      BWAPI::UnitCommand command,
      bool checkCanUseTechPositionOnPositions = true,
      bool checkCanUseTechUnitOnUnits = true,
      bool checkCanBuildUnitType = true,
      bool checkCanTargetUnit = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canIssueCommandGrouped(
      BWAPI::UnitCommand command,
      bool checkCanUseTechPositionOnPositions = true,
      bool checkCanUseTechUnitOnUnits = true,
      bool checkCanTargetUnit = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibilityGrouped = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canCommand() const override {
    throwNotImplemented();
  }
  bool canCommandGrouped(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canIssueCommandType(
      BWAPI::UnitCommandType ct,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canIssueCommandTypeGrouped(
      BWAPI::UnitCommandType ct,
      bool checkCommandibilityGrouped = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canTargetUnit(BWAPI::Unit targetUnit, bool checkCommandibility = true)
      const override {
    throwNotImplemented();
  }
  bool canAttack(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canAttack(
      BWAPI::Position target,
      bool checkCanTargetUnit = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canAttack(
      BWAPI::Unit target,
      bool checkCanTargetUnit = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canAttackGrouped(
      bool checkCommandibilityGrouped = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canAttackGrouped(
      BWAPI::Position target,
      bool checkCanTargetUnit = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibilityGrouped = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canAttackGrouped(
      BWAPI::Unit target,
      bool checkCanTargetUnit = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibilityGrouped = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canAttackMove(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canAttackMoveGrouped(
      bool checkCommandibilityGrouped = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canAttackUnit(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canAttackUnit(
      BWAPI::Unit targetUnit,
      bool checkCanTargetUnit = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canAttackUnitGrouped(
      bool checkCommandibilityGrouped = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canAttackUnitGrouped(
      BWAPI::Unit targetUnit,
      bool checkCanTargetUnit = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibilityGrouped = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canBuild(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canBuild(
      BWAPI::UnitType uType,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canBuild(
      BWAPI::UnitType uType,
      BWAPI::TilePosition tilePos,
      bool checkTargetUnitType = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canBuildAddon(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canBuildAddon(
      BWAPI::UnitType uType,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canTrain(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canTrain(
      BWAPI::UnitType uType,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canMorph(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canMorph(
      BWAPI::UnitType uType,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canResearch(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canResearch(BWAPI::TechType type, bool checkCanIssueCommandType = true)
      const override {
    throwNotImplemented();
  }
  bool canUpgrade(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canUpgrade(BWAPI::UpgradeType type, bool checkCanIssueCommandType = true)
      const override {
    throwNotImplemented();
  }
  bool canSetRallyPoint(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canSetRallyPoint(
      BWAPI::Position target,
      bool checkCanTargetUnit = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canSetRallyPoint(
      BWAPI::Unit target,
      bool checkCanTargetUnit = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canSetRallyPosition(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canSetRallyUnit(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canSetRallyUnit(
      BWAPI::Unit targetUnit,
      bool checkCanTargetUnit = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canMove(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canMoveGrouped(
      bool checkCommandibilityGrouped = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canPatrol(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canPatrolGrouped(
      bool checkCommandibilityGrouped = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canFollow(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canFollow(
      BWAPI::Unit targetUnit,
      bool checkCanTargetUnit = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canGather(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canGather(
      BWAPI::Unit targetUnit,
      bool checkCanTargetUnit = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canReturnCargo(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canHoldPosition(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canStop(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canRepair(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canRepair(
      BWAPI::Unit targetUnit,
      bool checkCanTargetUnit = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canBurrow(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canUnburrow(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canCloak(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canDecloak(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canSiege(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canUnsiege(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canLift(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canLand(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canLand(
      BWAPI::TilePosition target,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canLoad(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canLoad(
      BWAPI::Unit targetUnit,
      bool checkCanTargetUnit = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canUnloadWithOrWithoutTarget(
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canUnloadAtPosition(
      BWAPI::Position targDropPos,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canUnload(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canUnload(
      BWAPI::Unit targetUnit,
      bool checkCanTargetUnit = true,
      bool checkPosition = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canUnloadAll(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canUnloadAllPosition(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canUnloadAllPosition(
      BWAPI::Position targDropPos,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canRightClick(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canRightClick(
      BWAPI::Position target,
      bool checkCanTargetUnit = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canRightClick(
      BWAPI::Unit target,
      bool checkCanTargetUnit = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canRightClickGrouped(
      bool checkCommandibilityGrouped = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canRightClickGrouped(
      BWAPI::Position target,
      bool checkCanTargetUnit = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibilityGrouped = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canRightClickGrouped(
      BWAPI::Unit target,
      bool checkCanTargetUnit = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibilityGrouped = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canRightClickPosition(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canRightClickPositionGrouped(
      bool checkCommandibilityGrouped = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canRightClickUnit(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canRightClickUnit(
      BWAPI::Unit targetUnit,
      bool checkCanTargetUnit = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canRightClickUnitGrouped(
      bool checkCommandibilityGrouped = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canRightClickUnitGrouped(
      BWAPI::Unit targetUnit,
      bool checkCanTargetUnit = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibilityGrouped = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canHaltConstruction(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canCancelConstruction(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canCancelAddon(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canCancelTrain(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canCancelTrainSlot(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canCancelTrainSlot(
      int slot,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canCancelMorph(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canCancelResearch(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canCancelUpgrade(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canUseTechWithOrWithoutTarget(
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canUseTechWithOrWithoutTarget(
      BWAPI::TechType tech,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canUseTech(
      BWAPI::TechType tech,
      BWAPI::Position target,
      bool checkCanTargetUnit = true,
      bool checkTargetsType = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canUseTech(
      BWAPI::TechType tech,
      BWAPI::Unit target = nullptr,
      bool checkCanTargetUnit = true,
      bool checkTargetsType = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canUseTechWithoutTarget(
      BWAPI::TechType tech,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canUseTechUnit(
      BWAPI::TechType tech,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canUseTechUnit(
      BWAPI::TechType tech,
      BWAPI::Unit targetUnit,
      bool checkCanTargetUnit = true,
      bool checkTargetsUnits = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canUseTechPosition(
      BWAPI::TechType tech,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canUseTechPosition(
      BWAPI::TechType tech,
      BWAPI::Position target,
      bool checkTargetsPositions = true,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canPlaceCOP(bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
  bool canPlaceCOP(
      BWAPI::TilePosition target,
      bool checkCanIssueCommandType = true,
      bool checkCommandibility = true) const override {
    throwNotImplemented();
  }
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
};

} // namespace tcbwapi
