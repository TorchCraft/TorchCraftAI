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
class TCGame : public BWAPI::Game {
 public:
  TCGame() : BWAPI::Game() {}
  virtual ~TCGame();

  // TODO I'm getting linker errors if I attempt to make a proper constructor.
  void setState(torchcraft::State* s);

  //
  // Implemented methods
  //
  bool isWalkable(int walkX, int walkY) const override;
  int getGroundHeight(int tileX, int tileY) const override;
  bool isBuildable(int tileX, int tileY, bool includeBuildings = false)
      const override;
  BWAPI::Unitset const& getStaticNeutralUnits() const override;
  BWAPI::TilePosition::list const& getStartLocations() const override;
  int mapWidth() const override;
  int mapHeight() const override;
  BWAPI::Unit getUnit(int unitID) const override;

 private:
  void throwNotImplemented() const;

  torchcraft::State* s_ = nullptr;

  BWAPI::Unitset staticNeutralUnits_;
  BWAPI::TilePosition::list startLocations_;

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
  const BWAPI::Forceset& getForces() const override {
    throwNotImplemented();
  }
  const BWAPI::Playerset& getPlayers() const override {
    throwNotImplemented();
  }
  const BWAPI::Unitset& getAllUnits() const override {
    throwNotImplemented();
  }
  const BWAPI::Unitset& getMinerals() const override {
    throwNotImplemented();
  }
  const BWAPI::Unitset& getGeysers() const override {
    throwNotImplemented();
  }
  const BWAPI::Unitset& getNeutralUnits() const override {
    throwNotImplemented();
  }
  const BWAPI::Unitset& getStaticMinerals() const override {
    throwNotImplemented();
  }
  const BWAPI::Unitset& getStaticGeysers() const override {
    throwNotImplemented();
  }
  const BWAPI::Bulletset& getBullets() const override {
    throwNotImplemented();
  }
  const BWAPI::Position::list& getNukeDots() const override {
    throwNotImplemented();
  }
  const std::list<BWAPI::Event>& getEvents() const override {
    throwNotImplemented();
  }
  BWAPI::Force getForce(int forceID) const override {
    throwNotImplemented();
  }
  BWAPI::Player getPlayer(int playerID) const override {
    throwNotImplemented();
  }
  BWAPI::Unit indexToUnit(int unitIndex) const override {
    throwNotImplemented();
  }
  BWAPI::Region getRegion(int regionID) const override {
    throwNotImplemented();
  }
  BWAPI::GameType getGameType() const override {
    throwNotImplemented();
  }
  int getLatency() const override {
    throwNotImplemented();
  }
  int getFrameCount() const override {
    throwNotImplemented();
  }
  int getReplayFrameCount() const override {
    throwNotImplemented();
  }
  ///   static int bestFPS override { throwNotImplemented(); }
  int getFPS() const override {
    throwNotImplemented();
  }
  double getAverageFPS() const override {
    throwNotImplemented();
  }
  BWAPI::Position getMousePosition() const override {
    throwNotImplemented();
  }
  bool getMouseState(BWAPI::MouseButton button) const override {
    throwNotImplemented();
  }
  bool getKeyState(BWAPI::Key key) const override {
    throwNotImplemented();
  }
  BWAPI::Position getScreenPosition() const override {
    throwNotImplemented();
  }
  void setScreenPosition(int x, int y) override {
    throwNotImplemented();
  }
  void pingMinimap(int x, int y) override {
    throwNotImplemented();
  }
  bool isFlagEnabled(int flag) const override {
    throwNotImplemented();
  }
  void enableFlag(int flag) override {
    throwNotImplemented();
  }
  BWAPI::Unitset getUnitsInRectangle(
      int left,
      int top,
      int right,
      int bottom,
      const BWAPI::UnitFilter& pred = nullptr) const override {
    throwNotImplemented();
  }
  BWAPI::Unit getClosestUnitInRectangle(
      BWAPI::Position center,
      const BWAPI::UnitFilter& pred = nullptr,
      int left = 0,
      int top = 0,
      int right = 999999,
      int bottom = 999999) const override {
    throwNotImplemented();
  }
  BWAPI::Unit getBestUnit(
      const BWAPI::BestUnitFilter& best,
      const BWAPI::UnitFilter& pred,
      BWAPI::Position center = BWAPI::Positions::Origin,
      int radius = 999999) const override {
    throwNotImplemented();
  }
  BWAPI::Error getLastError() const override {
    throwNotImplemented();
  }
  bool setLastError(BWAPI::Error e = BWAPI::Errors::None) const override {
    throwNotImplemented();
  }
  std::string mapFileName() const override {
    throwNotImplemented();
  }
  std::string mapPathName() const override {
    throwNotImplemented();
  }
  std::string mapName() const override {
    throwNotImplemented();
  }
  std::string mapHash() const override {
    throwNotImplemented();
  }
  bool isVisible(int tileX, int tileY) const override {
    throwNotImplemented();
  }
  bool isExplored(int tileX, int tileY) const override {
    throwNotImplemented();
  }
  bool hasCreep(int tileX, int tileY) const override {
    throwNotImplemented();
  }
  bool hasPowerPrecise(
      int x,
      int y,
      BWAPI::UnitType unitType = BWAPI::UnitTypes::None) const override {
    throwNotImplemented();
  }
  bool canBuildHere(
      BWAPI::TilePosition position,
      BWAPI::UnitType type,
      BWAPI::Unit builder = nullptr,
      bool checkExplored = false) override {
    throwNotImplemented();
  }
  bool canMake(BWAPI::UnitType type, BWAPI::Unit builder = nullptr)
      const override {
    throwNotImplemented();
  }
  bool canResearch(
      BWAPI::TechType type,
      BWAPI::Unit unit = nullptr,
      bool checkCanIssueCommandType = true) override {
    throwNotImplemented();
  }
  bool canUpgrade(
      BWAPI::UpgradeType type,
      BWAPI::Unit unit = nullptr,
      bool checkCanIssueCommandType = true) override {
    throwNotImplemented();
  }
  void vPrintf(const char* format, va_list args) override {
    throwNotImplemented();
  }
  void vSendTextEx(bool toAllies, const char* format, va_list args) override {
    throwNotImplemented();
  }
  bool isInGame() const override {
    throwNotImplemented();
  }
  bool isMultiplayer() const override {
    throwNotImplemented();
  }
  bool isBattleNet() const override {
    throwNotImplemented();
  }
  bool isPaused() const override {
    throwNotImplemented();
  }
  bool isReplay() const override {
    throwNotImplemented();
  }
  void pauseGame() override {
    throwNotImplemented();
  }
  void resumeGame() override {
    throwNotImplemented();
  }
  void leaveGame() override {
    throwNotImplemented();
  }
  void restartGame() override {
    throwNotImplemented();
  }
  void setLocalSpeed(int speed) override {
    throwNotImplemented();
  }
  bool issueCommand(const BWAPI::Unitset& units, BWAPI::UnitCommand command)
      override {
    throwNotImplemented();
  }
  const BWAPI::Unitset& getSelectedUnits() const override {
    throwNotImplemented();
  }
  BWAPI::Player self() const override {
    throwNotImplemented();
  }
  BWAPI::Player enemy() const override {
    throwNotImplemented();
  }
  BWAPI::Player neutral() const override {
    throwNotImplemented();
  }
  BWAPI::Playerset& allies() override {
    throwNotImplemented();
  }
  BWAPI::Playerset& enemies() override {
    throwNotImplemented();
  }
  BWAPI::Playerset& observers() override {
    throwNotImplemented();
  }
  void setTextSize(
      BWAPI::Text::Size::Enum size = BWAPI::Text::Size::Default) override {
    throwNotImplemented();
  }
  void vDrawText(
      BWAPI::CoordinateType::Enum ctype,
      int x,
      int y,
      const char* format,
      va_list arg) override {
    throwNotImplemented();
  }
  void drawBox(
      BWAPI::CoordinateType::Enum ctype,
      int left,
      int top,
      int right,
      int bottom,
      BWAPI::Color color,
      bool isSolid = false) override {
    throwNotImplemented();
  }
  void drawTriangle(
      BWAPI::CoordinateType::Enum ctype,
      int ax,
      int ay,
      int bx,
      int by,
      int cx,
      int cy,
      BWAPI::Color color,
      bool isSolid = false) override {
    throwNotImplemented();
  }
  void drawCircle(
      BWAPI::CoordinateType::Enum ctype,
      int x,
      int y,
      int radius,
      BWAPI::Color color,
      bool isSolid = false) override {
    throwNotImplemented();
  }
  void drawEllipse(
      BWAPI::CoordinateType::Enum ctype,
      int x,
      int y,
      int xrad,
      int yrad,
      BWAPI::Color color,
      bool isSolid = false) override {
    throwNotImplemented();
  }
  void drawDot(
      BWAPI::CoordinateType::Enum ctype,
      int x,
      int y,
      BWAPI::Color color) override {
    throwNotImplemented();
  }
  void drawLine(
      BWAPI::CoordinateType::Enum ctype,
      int x1,
      int y1,
      int x2,
      int y2,
      BWAPI::Color color) override {
    throwNotImplemented();
  }
  int getLatencyFrames() const override {
    throwNotImplemented();
  }
  int getLatencyTime() const override {
    throwNotImplemented();
  }
  int getRemainingLatencyFrames() const override {
    throwNotImplemented();
  }
  int getRemainingLatencyTime() const override {
    throwNotImplemented();
  }
  int getRevision() const override {
    throwNotImplemented();
  }
  int getClientVersion() const override {
    throwNotImplemented();
  }
  bool isDebug() const override {
    throwNotImplemented();
  }
  bool isLatComEnabled() const override {
    throwNotImplemented();
  }
  void setLatCom(bool isEnabled) override {
    throwNotImplemented();
  }
  bool isGUIEnabled() const override {
    throwNotImplemented();
  }
  void setGUI(bool enabled) override {
    throwNotImplemented();
  }
  int getInstanceNumber() const override {
    throwNotImplemented();
  }
  int getAPM(bool includeSelects = false) const override {
    throwNotImplemented();
  }
  bool setMap(const char* mapFileName) override {
    throwNotImplemented();
  }
  void setFrameSkip(int frameSkip) override {
    throwNotImplemented();
  }
  bool setAlliance(
      BWAPI::Player player,
      bool allied = true,
      bool alliedVictory = true) override {
    throwNotImplemented();
  }
  bool setVision(BWAPI::Player player, bool enabled = true) override {
    throwNotImplemented();
  }
  int elapsedTime() const override {
    throwNotImplemented();
  }
  void setCommandOptimizationLevel(int level) override {
    throwNotImplemented();
  }
  int countdownTimer() const override {
    throwNotImplemented();
  }
  const BWAPI::Regionset& getAllRegions() const override {
    throwNotImplemented();
  }
  BWAPI::Region getRegionAt(int x, int y) const override {
    throwNotImplemented();
  }
  int getLastEventTime() const override {
    throwNotImplemented();
  }
  bool setRevealAll(bool reveal = true) override {
    throwNotImplemented();
  }
  unsigned getRandomSeed() const override {
    throwNotImplemented();
  }
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
};

} // namespace tcbwapi
