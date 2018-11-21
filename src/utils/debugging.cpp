/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "utils.h"

#include "fsutils.h"

#ifdef HAVE_TORCH
#include "autogradpp/autograd.h"
#endif // HAVE_TORCH

#include "fmt/format.h"
#include <ctime>
#include <iomanip>

namespace cherrypi {
namespace utils {

std::string commandString(State* state, tc::Client::Command const& cmd) {
  std::ostringstream oss;
  auto cc = tc::BW::Command::_from_integral_nothrow(cmd.code);
  oss << "{code=" << (cc ? cc->_to_string() : "???");
  if (!cmd.str.empty()) {
    oss << ", str='" << cmd.str << "'";
  }
  if (!cmd.args.empty()) {
    oss << ", args=[";
    for (size_t i = 0; i < cmd.args.size(); i++) {
      if (i > 0) {
        oss << ", ";
      }
      if (i == 0 && cmd.code == tc::BW::Command::CommandUnit) {
        // Unit type name
        oss << unitString(state->unitsInfo().getUnit(cmd.args[i]));
      } else if (i == 1 && cmd.code == tc::BW::Command::CommandUnit) {
        // Command name
        oss << "'" << commandBwString(cmd.args[i]) << "'";
      } else if (
          i == 2 && cmd.code == tc::BW::Command::CommandUnit &&
          (cmd.args[1] == tc::BW::UnitCommandType::Right_Click_Unit ||
           cmd.args[1] == tc::BW::UnitCommandType::Attack_Unit)) {
        // Target unit of command
        oss << unitString(state->unitsInfo().getUnit(cmd.args[i]));
      } else if (
          i == 5 && cmd.code == tc::BW::Command::CommandUnit &&
          (cmd.args[1] == tc::BW::UnitCommandType::Build ||
           cmd.args[1] == tc::BW::UnitCommandType::Train)) {
        // Target unit type
        auto bt = tc::BW::UnitType::_from_integral_nothrow(cmd.args[i]);
        oss << "'" << (bt ? bt->_to_string() : "???") << "'";
      } else {
        oss << cmd.args[i];
      }
    }
    oss << "]";
  }
  oss << "}";

  return oss.str();
}

void spawnUnit(
    State* state,
    PlayerId team,
    BuildType const* typ,
    Position loc) {
  spawnUnit(state, team, typ->unit, loc);
}

void spawnUnit(State* state, PlayerId team, int typ, Position loc) {
  state->board()->postCommand(
      tc::Client::Command(
          tc::BW::Command::CommandOpenbw,
          tc::BW::OpenBWCommandType::SpawnUnit,
          team,
          typ,
          loc.x * tc::BW::XYPixelsPerWalktile,
          loc.y * tc::BW::XYPixelsPerWalktile),
      kRootUpcId);
}

void killUnit(State* state, Unit* u) {
  state->board()->postCommand(
      tc::Client::Command(
          tc::BW::Command::CommandOpenbw,
          tc::BW::OpenBWCommandType::KillUnit,
          u->id),
      kRootUpcId);
}

std::map<std::string, std::string> gflagsValues(std::string const& sourcePath) {
  std::map<std::string, std::string> flags;
  std::vector<gflags::CommandLineFlagInfo> config;
  gflags::GetAllFlags(&config);
  for (auto const& c : config) {
    if (sourcePath.empty() || c.filename == sourcePath) {
      flags[c.name] = c.current_value;
    }
  }
  return flags;
}

std::string curTimeString(char const* format) {
  auto t = std::time(nullptr);
  auto tm = *std::localtime(&t);
  std::ostringstream oss;
  oss << std::put_time(&tm, format);
  return oss.str();
}

#ifdef HAVE_TORCH
std::string visualizeHeatmap(torch::Tensor inp) {
  if (inp.ndimension() != 2) {
    return "Can only visualize a 2 dim tensor as a heatmap";
  }
  torch::NoGradGuard guard;
  inp = inp.to(at::kCPU, at::kFloat);
  auto min = inp.min();
  auto max = inp.max();
  inp = (inp - min) / (max - min + 1e-3);
  auto acc = inp.accessor<float, 2>();
  std::ostringstream ss;
  for (auto y = 0; y < acc.size(0); y++) {
    for (auto x = 0; x < acc.size(1); x++) {
      ss << fmt::format("\x1b[48;2;{0};{0};{0}m ", int(acc[y][x] * 256));
    }
    ss << fmt::format("\033[0m\n");
  }
  ss << "\033[0m";
  return ss.str();
}
#endif // HAVE_TORCH

} // namespace utils
} // namespace cherrypi
