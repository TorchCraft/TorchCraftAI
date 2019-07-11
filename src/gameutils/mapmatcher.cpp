/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "mapmatcher.h"
#include "common/fsutils.h"

#include <algorithm>
#include <glog/logging.h>
#include <regex>

namespace cherrypi {

namespace {

/*
 * Regex matching common jargon/noise in map names that we want to remove.
 * * .scx/.scm (File extensions)
 * * ICCup (branding)
 * * Neo (updated version of a map)
 * * SE (special edition?)
 * * OBS/OB/OBs (Observer version, ie. with dedicated observation player slots)
 *
 * Version numbers (like 1.0, 1.1, 2.0) are also common.
 *
 * Lastly, roughly attempt to strip any non-alphabetic characters,
 * (ie. version numbers and other symbols) while retaining Korean characters.
 */

void eraseSubStr(std::string& mainStr, const std::string& toErase) {
  // Search for the substring in string
  size_t pos = mainStr.find(toErase);

  if (pos != std::string::npos) {
    // If found then erase it from string
    mainStr.erase(pos, toErase.length());
  }
}

const std::regex kRemovables(
    "\\.scx|\\.scm|iccup|obs|ob|neo|신|pok|\\sse|\\sobs|\\sob|[\\x00-\\x60]|["
    "\\x7b-"
    "\\x7f]");

const std::vector<std::pair<std::regex, std::string>> kReplacements = {
    {std::regex("투혼"), "fightingspirit"},
    {std::regex("태양의제국"), "empireofthesun"},
    {std::regex("단장의능선"), "heartbreakridge"},
    {std::regex("저격능선"), "sniperridge"},
    {std::regex("colosseumii"), "colosseum"},
    {std::regex("circuitbreakers"), "circuitbreaker"},
    {std::regex("피의능선"), "bloodyridge"}};

std::string fuzz(std::string mapName) {
  std::transform(mapName.begin(), mapName.end(), mapName.begin(), ::tolower);
  mapName = std::regex_replace(mapName, kRemovables, "");
  for (const auto& replacement : kReplacements) {
    mapName =
        std::regex_replace(mapName, replacement.first, replacement.second);
  }
  return mapName;
}

} // namespace

void MapMatcher::load_() {
  auto mapPaths = common::fsutils::findr("maps/fuzzymatch", "*.sc*");
  if (mapPaths.empty()) {
    mapPaths = common::fsutils::findr(prefix_ + "maps/fuzzymatch", "*.sc*");
    for (auto& mapPath : mapPaths) {
      eraseSubStr(mapPath, prefix_);
    }
  }
  for (const auto& mapPath : mapPaths) {
    auto mapFile = common::fsutils::basename(mapPath);
    auto mapFuzz = fuzz(mapFile);
    mapByFuzzyName_[mapFuzz] = mapPath;
    VLOG(1) << mapFuzz << " <- " << mapFile << " <- " << mapPath;
  }
}

std::string MapMatcher::tryMatch(const std::string& mapName) {
  if (mapByFuzzyName_.empty()) {
    load_();
  }
  auto fuzzyName = fuzz(mapName);
  VLOG(2) << "Fuzzed " << mapName << " -> " << fuzzyName;
  auto iFuzz = mapByFuzzyName_.find(fuzzyName);
  if (iFuzz != mapByFuzzyName_.end()) {
    return iFuzz->second;
  }
  VLOG(1) << "Failed to match " << mapName;
  VLOG(1) << "Fuzzed: " << fuzzyName;
  return {};
}

} // namespace cherrypi
