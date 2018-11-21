/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "../modules/autobuild.h"

#include "../modules/builderhelper.h"
#include "registry.h"
#include "utils.h"

namespace cherrypi {

namespace buildorders {
std::shared_ptr<AutoBuildTask>
createTask(UpcId srcUpcId, std::string name, State* state, Module* module);
} // namespace buildorders

class ABBOBase : public AutoBuildTask {
 public:
  ABBOBase(UpcId upcId, State* state, Module* module)
      : AutoBuildTask(upcId, state, module) {}

  Position nextBase;
  bool canExpand = false;
  bool shouldExpand = false;
  bool forceExpand = false;
  bool preferSafeExpansions = true;

  int enemyWorkerCount = 0;
  int enemyGasCount = 0;
  int enemyZealotCount = 0;
  int enemyDragoonCount = 0;
  int enemyDarkTemplarCount = 0;
  int enemyHighTemplarCount = 0;
  int enemyArchonCount = 0;
  int enemyReaverCount = 0;
  int enemyVultureCount = 0;
  int enemyGoliathCount = 0;
  int enemyTankCount = 0;
  int enemyMissileTurretCount = 0;
  int enemyCorsairCount = 0;
  int enemyScoutCount = 0;
  int enemyObserverCount = 0;
  int enemyWraithCount = 0;
  int enemyValkyrieCount = 0;
  int enemyBattlecruiserCount = 0;
  int enemyStaticDefenceCount = 0;
  int enemyBarracksCount = 0;
  int enemyRefineryCount = 0;
  int enemyAcademyCount = 0;
  int enemyGatewayCount = 0;
  int enemyCyberneticsCoreCount = 0;
  int enemyStargateCount = 0;
  int enemyForgeCount = 0;
  int enemyZerglingCount = 0;
  int enemyHydraliskCount = 0;
  int enemyMutaliskCount = 0;
  int enemyScourgeCount = 0;
  int enemySunkenCount = 0;
  int enemySporeCount = 0;
  int enemyMarineCount = 0;
  int enemyMedicCount = 0;
  int enemyFirebatCount = 0;
  int enemyFactoryCount = 0;
  int enemyLairCount = 0;
  int enemySpireCount = 0;
  int enemyCloakedUnitCount = 0;
  bool enemyHasExpanded = false;
  bool enemyIsRushing = false; // TODO detect for protoss / zerg
  int enemyBuildingCount = 0;
  int enemyScienceVesselCount = 0;
  int enemyArbiterCount = 0;
  int enemyShuttleCount = 0;
  int enemyResourceDepots = 0;
  int enemyGasUnits = 0;
  int enemyTemplarArchivesCount = 0;

  int myCompletedHatchCount = 0;
  int myLarvaCount = 0;
  int mySunkenCount = 0;
  int mySporeCount = 0;
  int myDroneCount = 0;
  int myZerglingCount = 0;
  int myHydraliskCount = 0;
  int myMutaliskCount = 0;
  int myScourgeCount = 0;
  int myLurkerCount = 0;
  int myGuardianCount = 0;
  int myUltraliskCount = 0;

  int mineralFields = 0;
  int geysers = 0;
  Position homePosition;
  Position naturalPos;
  Position naturalDefencePos;
  Position mainNaturalChoke;
  Position enemyBasePos;
  bool hasFoundEnemyBase = false;
  Position nextStaticDefencePos;
  bool weArePlanningExpansion = false;
  int currentFrame = 0;

  int bases = 0;

  bool isLosingAnOverlord = false;

  Position lastFindNaturalDefencePosEnemyPos{-1, -1};

  std::vector<uint8_t> inBaseArea =
      std::vector<uint8_t>(TilesInfo::tilesWidth * TilesInfo::tilesHeight);
  FrameNum lastUpdateInBaseArea = 0;

  double armySupply = 0.0;
  double groundArmySupply = 0.0;
  double airArmySupply = 0.0;

  double enemySupplyInOurBase = 0.0;
  double enemyArmySupplyInOurBase = 0.0;
  double enemyArmySupply = 0.0;
  double enemyGroundArmySupply = 0.0;
  double enemyAirArmySupply = 0.0;
  double enemyAntiAirArmySupply = 0.0;
  double enemyAttackingArmySupply = 0.0;
  double enemyAttackingGroundArmySupply = 0.0;
  double enemyAttackingAirArmySupply = 0.0;
  int enemyAttackingWorkerCount = 0;
  double enemyLargeArmySupply = 0.0;
  double enemySmallArmySupply = 0.0;
  double enemyBiologicalArmySupply = 0.0;
  double enemyProximity = 0.0;

  int enemyProxyBuildingCount = 0;
  int enemyProxyGatewayCount = 0;
  int enemyProxyBarracksCount = 0;
  int enemyProxyForgeCount = 0;
  int enemyProxyCannonCount = 0;

  bool enemyForgeIsSpinning = false;

  tc::BW::Race enemyRace = tc::BW::Race::Unknown;

  bool autoExpand = true;
  bool autoUpgrade = true;
  bool expandNearest = false;
  bool buildExtraOverlordsIfLosingThem = true;

  void findNaturalDefencePos(State*);

  Position getStaticDefencePos(State*, const BuildType* type);

 private:
  void buildZergStaticDefense(
      autobuild::BuildState& st,
      int numberDesired,
      const BuildType* morphedType,
      Position position,
      bool morphFirst);

 protected:
  virtual void draw(State* state) override;

 public:
  void buildSunkens(
      autobuild::BuildState& st,
      int n,
      Position = {},
      bool morphFirst = false);
  void buildSpores(
      autobuild::BuildState& st,
      int n,
      Position = {},
      bool morphFirst = false);
  void morphSunkens(autobuild::BuildState&, int n = 1000);
  void morphSpores(autobuild::BuildState&, int n = 1000);
  void takeNBases(autobuild::BuildState&, int n);
  void expand(autobuild::BuildState& st);

  void calculateArmySupply(const autobuild::BuildState& st);

  Position findHatcheryPosNear(Position seedPos);

  Position findSunkenPosNear(
      const BuildType* type,
      Position seedPos,
      bool coverMineralsOnly = false);

  Position findSunkenPos(
      const BuildType* type,
      bool mainBaseOnly = false,
      bool coverMineralsOnly = false);

  virtual void preBuild2(autobuild::BuildState& st) {}
  virtual void preBuild(autobuild::BuildState& st) override final;
  virtual void postBuild2(autobuild::BuildState& st) {}
  virtual void postBuild(autobuild::BuildState& st) override final;
  virtual void buildStep2(autobuild::BuildState&) {}
  virtual void buildStep(autobuild::BuildState&) override final;
};
}
