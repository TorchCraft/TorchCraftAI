/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fmt/format.h>
#include <sstream>
#include <torchcraft/client.h>

#include "cherrypi.h"
#include "state.h"

namespace cherrypi {
namespace utils {

void spawnUnit(State*, PlayerId, BuildType const*, Position);
void spawnUnit(State*, PlayerId, int, Position);
void killUnit(State*, Unit*);

std::string curTimeString(char const* format = "%Y-%d-%m %H:%M:%S");
std::string buildTypeString(BuildType const* buildType);
std::string commandString(State* s, tc::Client::Command const& command);
inline std::string commandString(Command c) {
  switch (c) {
    case Command::None:
      return std::string("None");
    case Command::Create:
      return std::string("Create");
    case Command::Move:
      return std::string("Move");
    case Command::Delete:
      return std::string("Delete");
    case Command::Gather:
      return std::string("Gather");
    case Command::Scout:
      return std::string("Scout");
    case Command::Cancel:
      return std::string("Cancel");
    case Command::Harass:
      return std::string("Harass");
    case Command::Flee:
      return std::string("Flee");
    case Command::SetCreatePriority:
      return std::string("SetCreatePriority");
    case Command::ReturnCargo:
      return std::string("ReturnCargo");
    case Command::MAX:
      return std::string("MAX");
    default:
      return std::string("?");
  }
}

template <typename T>
std::string positionString(Vec2T<T> position) {
  std::ostringstream oss;
  oss << position;
  return oss.str();
}

inline std::string unitString(Unit const* unit) {
  if (!unit)
    return "nullptr";
  std::ostringstream oss;
  // Log units with 'i' prefix so that we'll be able to use 'u' for UPC tuples
  oss << (unit->isMine ? "Friendly" : unit->isEnemy ? "Enemy" : "Neutral")
      << " i" << unit->id << " " << unit->type->name << " " << Position(unit);
  return oss.str();
}

template <typename Units>
inline std::string unitsString(Units&& units) {
  std::ostringstream oss;
  oss << "[";
  for (auto unit : units) {
    oss << unitString(unit) << ",";
  }
  oss << "]";
  return oss.str();
}

template <typename T>
inline std::string resourcesString(T&& resources) {
  std::ostringstream oss;
  oss << resources.ore << " ore, " << resources.gas << " gas, "
      << resources.used_psi / 2 << "/" << resources.total_psi / 2 << " psi/2";
  return oss.str();
}

inline std::string upcString(UpcId id) {
  std::ostringstream oss;
  oss << "u" << id;
  return oss.str();
}

inline std::string upcString(std::shared_ptr<UPCTuple> const& upc, UpcId id) {
  if (!upc) {
    return upcString(id);
  }
  std::ostringstream oss;
  oss << "u" << id << " (";
  bool first = true;
  for (const auto& c : upc->command) {
    if (!first) {
      oss << ",";
    }
    oss << commandString(c.first) << "=" << c.second;
    first = false;
  }
  if (upc->command.empty()) {
    oss << "no commands";
  }

  // TODO Add more relevant information for specific UPcs?

  oss << ")";
  return oss.str();
}

inline std::string upcTaskString(State* state, UpcId upcId) {
  const auto& task = state->board()->taskForId(upcId);
  return {task ? task->getName() : "No task"};
}

// commandType should be a tc::BW::UnitCommandType,
// but the conversion is private
inline std::string commandBwString(int commandType) {
  auto uct = tc::BW::UnitCommandType::_from_integral_nothrow(commandType);
  return uct ? uct->_to_string() : "???";
}

inline void
drawLine(State* state, Position const& a, Position const& b, int color = 255) {
  state->board()->postCommand(
      {tc::BW::Command::DrawLine,
       a.x * tc::BW::XYPixelsPerWalktile,
       a.y * tc::BW::XYPixelsPerWalktile,
       b.x * tc::BW::XYPixelsPerWalktile,
       b.y * tc::BW::XYPixelsPerWalktile,
       color},
      kRootUpcId);
}

inline void
drawLine(State* state, Unit const* a, Unit const* b, int color = 255) {
  state->board()->postCommand(
      {tc::BW::Command::DrawUnitLine, a->id, b->id, color}, kRootUpcId);
}

inline void
drawLine(State* state, Unit const* a, Position const& b, int color = 255) {
  state->board()->postCommand(
      {tc::BW::Command::DrawUnitPosLine,
       a->id,
       b.x * tc::BW::XYPixelsPerWalktile,
       b.y * tc::BW::XYPixelsPerWalktile,
       color},
      kRootUpcId);
}

inline void
drawBox(State* state, Position const& a, Position const& b, int color = 255) {
  drawLine(state, {a.x, a.y}, {a.x, b.y}, color);
  drawLine(state, {a.x, a.y}, {b.x, a.y}, color);
  drawLine(state, {a.x, b.y}, {b.x, b.y}, color);
  drawLine(state, {b.x, a.y}, {b.x, b.y}, color);
}

inline void
drawCircle(State* state, Position const& a, int radius, int color = 255) {
  state->board()->postCommand(
      {tc::BW::Command::DrawCircle,
       a.x * tc::BW::XYPixelsPerWalktile,
       a.y * tc::BW::XYPixelsPerWalktile,
       radius, // in pixels
       color},
      kRootUpcId);
}

inline void
drawCircle(State* state, Unit const* u, int radius, int color = 255) {
  state->board()->postCommand(
      {tc::BW::Command::DrawUnitCircle,
       u->id,
       radius, // in pixels
       color},
      kRootUpcId);
}

constexpr int kLineHeight = 11;
constexpr int kCharacterWidthMax = 5;
// Approximation: 4.5px per character; most are 5 and some like 'I' are ~2
constexpr double kCharacterWidthAvg = 4.5;

inline void
drawTextPx(State* state, Position const& a, const std::string& text) {
  state->board()->postCommand(
      {tc::BW::Command::DrawText, text, a.x, a.y}, kRootUpcId);
}

inline void drawText(State* state, Position const& a, const std::string& text) {
  drawTextPx(
      state,
      {a.x * tc::BW::XYPixelsPerWalktile, a.y * tc::BW::XYPixelsPerWalktile},
      text);
}

inline void drawTextCenteredLinesPx(
    State* state,
    Position const& a,
    const std::vector<std::string>& lines) {
  int pixelWidth = 0;
  int pixelHeight = 0;
  for (auto line : lines) {
    int lineWidth = static_cast<int>(kCharacterWidthAvg * line.length());
    pixelWidth = std::max(pixelWidth, lineWidth);
    pixelHeight += kLineHeight;
  }
  const int x = a.x - pixelWidth / 2;
  int y = a.y - pixelWidth / 2;

  for (auto line : lines) {
    drawTextPx(state, {x, y}, line);
    y += kLineHeight;
  }
}

inline void
drawTextCenteredPx(State* state, Position const& a, const std::string& text) {
  drawTextCenteredLinesPx(state, a, {text});
}

inline void drawTextScreen(
    State* state,
    int xCharacter,
    int yLine,
    const std::string& text) {
  state->board()->postCommand(
      {tc::BW::Command::DrawTextScreen,
       text,
       kCharacterWidthMax * (xCharacter + 1),
       kCharacterWidthMax + yLine * kLineHeight},
      kRootUpcId);
}

inline void drawUnitCommand(
    State* state,
    Unit* unit,
    Command commandCpi,
    int commandBw, // tc::BW::UnitCommandType
    UpcId upcId) {
  std::string taskName = upcTaskString(state, upcId);
  std::string commandCpiName = commandString(commandCpi);
  std::string commandBwName = commandBwString(commandBw);
  drawTextCenteredLinesPx(
      state,
      {unit->x * tc::BW::XYPixelsPerWalktile,
       unit->y * tc::BW::XYPixelsPerWalktile},
      {commandCpiName, commandBwName, taskName});
}

/// Retrieves current gflags values, optionally restricted to a source file
std::map<std::string, std::string> gflagsValues(
    std::string const& sourcePath = std::string());

#ifdef HAVE_TORCH
std::string visualizeHeatmap(torch::Tensor);
#endif // HAVE_TORCH

} // namespace utils
} // namespace cherrypi
