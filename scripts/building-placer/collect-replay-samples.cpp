/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * This script collects building placement actions from replays. Samples are
 * taken from the state every time a player takes an action but are only written
 * to disk once it has been verified that the actual
 * construction/morphing/warping has started (i.e. the latest action
 * corresponding to the final position will be written out).
 *
 * Samples are written to disk using Zstd compresssion with the following path
 * scheme: $output_path/$prefix/$replay_$player/$number.bin . The `-keep-dirs`
 * option controls the number of path components that are retained from the
 * input file.  Samples will be collected for every player in the replay that
 * matches the requested race.
 *
 * For debugging, it may be helpful to visualize the collected samples in
 * Visdom; simply specify the desired environment via --visdom_env.
 */

#include "common.h"

#include "cherrypi.h"
#include "replayer.h"
#include "state.h"
#include "utils.h"
#include "zstdstream.h"

#include <common/fsutils.h>

#include <bwreplib/bwrepapi.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <visdom/visdom.h>

using namespace cherrypi;
auto const vopts = &visdom::makeOpts;
namespace fsutils = common::fsutils;

// CLI options
DEFINE_string(output_path, ".", "Save samples here");
DEFINE_int32(keep_dirs, 1, "Keep this many directories of each sample");
DEFINE_string(race, "Zerg", "Extract samples for this race");
DEFINE_bool(overwrite, false, "Overwrite existing samples");
DEFINE_string(visdom_server, "localhost", "Visdom server address");
DEFINE_int32(visdom_port, 8097, "Visdom server port");
DEFINE_string(
    visdom_env,
    "",
    "Visdom environment (empty string disables visualization)");

namespace {

struct BuildAction {
  FrameNum frame;
  BuildType const* buildType;
  Position pos;

  BuildAction(FrameNum frame, BuildType const* buildType, Position const& pos)
      : frame(frame), buildType(buildType), pos(pos) {}
};

struct SampleCandidate {
  FrameNum frame;
  BuildingPlacerSample sample;
  bool verified;

  SampleCandidate(FrameNum frame, BuildingPlacerSample sample, bool verified)
      : frame(frame), sample(std::move(sample)), verified(verified) {}
};

std::vector<BuildAction>
collectActions(BWrepFile& bwrep, PlayerId playerId, tc::BW::Race race) {
  BWrepPlayer player;
  bwrep.m_oHeader.getLogicalPlayers(player, playerId);
  auto playerRace =
      tc::BW::Race::_from_integral(static_cast<int>(player.getRace()));
  if (playerRace != race) {
    return {};
  }

  std::vector<BuildAction> actions;
  auto& actionList = bwrep.m_oActions;
  // Collect all build actions performed during the game
  for (int i = 0; i < actionList.GetActionCount(); i++) {
    auto* action = actionList.GetAction(i);
    if (action->GetPlayerID() != player.getSlot()) {
      continue;
    }
    if (action->GetID() != BWrepGameData::eACTIONNAME::CMD_BUILD) {
      continue;
    }
    auto* params =
        static_cast<BWrepActionBuild::Params const*>(action->GetParamStruct());
    // Sanity-check: is the building ID is actually corresponding to the player
    // race?
    if (tc::BW::data::GetRace[params->m_buildingid] != race._to_string()) {
      continue;
    }
    actions.emplace_back(
        action->GetTime(),
        getUnitBuildType(params->m_buildingid),
        Position(
            params->m_pos1 * tc::BW::XYWalktilesPerBuildtile,
            params->m_pos2 * tc::BW::XYWalktilesPerBuildtile));
  }

  return actions;
}

std::vector<BuildingPlacerSample> collectSamples(
    std::string const& replayFile,
    PlayerId playerId,
    std::vector<BuildAction> const& actions) {
  Replayer replay(replayFile);
  auto* state = replay.state();
  auto& uinfo = state->unitsInfo();

  state->setPerspective(playerId);
  replay.init();

  auto actionIt = actions.begin();
  // This will contain samples for all build actions performed during the game.
  // Not all of them will succeed, so we'll check for buildings to appear and
  // will verify the respective candidate
  std::vector<SampleCandidate> candidates;

  // When Protoss or Terran refiniers are built, the geyser unit will be re-used
  // for them. We don't get this from getNewUnits() so we keep track of
  // refineries ourselves.
  std::unordered_set<Unit*> refineries;
  BuildType const* refineryType = nullptr;
  if (state->myRace() == +tc::BW::Race::Terran) {
    refineryType = buildtypes::Terran_Refinery;
  } else if (state->myRace() == +tc::BW::Race::Protoss) {
    refineryType = buildtypes::Protoss_Assimilator;
  }

  std::shared_ptr<visdom::Visdom> vs = nullptr;
  if (!FLAGS_visdom_env.empty()) {
    visdom::ConnectionParams vparams;
    vparams.server = FLAGS_visdom_server;
    vparams.port = FLAGS_visdom_port;
    vs = std::make_shared<visdom::Visdom>(vparams, FLAGS_visdom_env);
  }

  while (actionIt != actions.end() && !state->gameEnded()) {
    replay.step();

    // Collect samples for any actions taken during step
    while (actionIt->frame <= state->currentFrame()) {
      // We assume we know the target area
      auto upc = std::make_shared<UPCTuple>();
      upc->position = state->areaInfo().tryGetArea(actionIt->pos);
      upc->state = UPCTuple::BuildTypeMap{{actionIt->buildType, 1}};

      BuildingPlacerSample sample(state, actionIt->pos, upc);
      candidates.emplace_back(state->currentFrame(), std::move(sample), false);
      VLOG(1) << "New sample at frame " << state->currentFrame() << ": build "
              << actionIt->buildType->name << " at " << actionIt->pos;

      if (++actionIt == actions.end()) {
        break;
      }
    }

    // Verify previous samples if new buildings appear
    auto verify = [&](Unit* unit) {
      Position buildPos(unit->buildX, unit->buildY);
      for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
        if (!it->verified && it->sample.action == buildPos) {
          it->verified = true;
          VLOG(1) << "Verified building " << utils::unitString(unit) << " at "
                  << Position(unit) << " placed at " << it->frame;

          // Visualize features if requested
          if (vs != nullptr) {
            auto prefix = fsutils::basename(replayFile) + " @" +
                std::to_string(it->sample.frame) + "<br>" + unit->type->name +
                ": ";
            auto& f = it->sample.features.map;
            for (auto const& desc : f.desc) {
              vs->heatmap(
                  selectFeatures(f, {desc.type}).tensor.sum(0),
                  vopts({{"title", prefix + desc.name}}));
            }
            auto uf = it->sample.unitFeaturizer.toSpatialFeature(
                it->sample.features.units);
            for (auto const& desc : uf.desc) {
              vs->heatmap(
                  subsampleFeature(
                      selectFeatures(uf, {desc.type}), SubsampleMethod::Sum, 4)
                      .tensor.sum(0)
                      .gt(0)
                      .toType(at::kFloat),
                  vopts({{"title", prefix + desc.name}}));
            }
          }
          break;
        }
      }
    };

    for (auto* unit : uinfo.getNewUnits()) {
      if (!unit->isMine || !unit->type->isBuilding) {
        continue;
      }
      verify(unit);
    }
    for (auto* unit : uinfo.getStartedMorphingUnits()) {
      if (!unit->isMine || !unit->type->isBuilding ||
          !unit->type->builder->isWorker) {
        continue;
      }
      verify(unit);
    }
    if (refineryType) {
      for (auto* unit : uinfo.myUnitsOfType(refineryType)) {
        if (refineries.find(unit) == refineries.end()) {
          verify(unit);
          refineries.insert(unit);
        }
      }
    }
  }

  std::vector<BuildingPlacerSample> samples;
  for (auto& cand : candidates) {
    if (cand.verified) {
      samples.emplace_back(std::move(cand.sample));
    }
  }

  return samples;
}

std::string outputDirectory(
    std::string const& replayFile,
    std::string const& outputPath,
    PlayerId player) {
  auto base = fsutils::basename(replayFile, ".rep");
  auto prefix = std::string();
  auto dir = fsutils::dirname(replayFile);
  for (auto i = 0; i < FLAGS_keep_dirs; i++) {
    prefix = fsutils::basename(dir) + "/" + prefix;
    dir = fsutils::dirname(dir);
  }
  return fmt::format("{}/{}{}_{}", outputPath, prefix, base, player);
}

void processReplay(
    std::string const& replayFile,
    tc::BW::Race race,
    std::string const& outputPath,
    bool overwrite) {
  // First, do a static analysis of the replay data to determine player actions
  // (we cannot get them through BWAPI).
  BWrepFile bwrep;
  if (!bwrep.Load(replayFile.c_str(), BWrepFile::LOADACTIONS)) {
    throw std::runtime_error("Cannot load replay: " + replayFile);
  }

  // We don't want to featrurize overly large maps
  if (bwrep.m_oHeader.getMapWidth() > BuildingPlacerSample::kMapSize ||
      bwrep.m_oHeader.getMapHeight() > BuildingPlacerSample::kMapSize) {
    VLOG(0) << "Skipping large map in " << replayFile << " ("
            << bwrep.m_oHeader.getMapWidth() << "x"
            << bwrep.m_oHeader.getMapHeight() << ")";
    return;
  }

  for (PlayerId playerId = 0;
       playerId < bwrep.m_oHeader.getLogicalPlayerCount();
       playerId++) {
    auto actions = collectActions(bwrep, playerId, race);
    if (actions.empty()) {
      continue;
    }

    auto outDir = outputDirectory(replayFile, outputPath, playerId);
    auto donePath = outDir + "/done";
    if (!overwrite && fsutils::exists(donePath)) {
      VLOG(0) << donePath << " exists, skipping";
      continue;
    }

    VLOG(0) << "Found " << actions.size() << " build actions in " << replayFile
            << " for player " << playerId;
    fsutils::mkdir(outDir);

    // Collect and dump samples to a files
    auto samples = collectSamples(replayFile, playerId, actions);
    for (auto i = 0U; i < samples.size(); i++) {
      auto samplePath = fmt::format("{}/{:05d}.bin", outDir, i);
      zstd::ofstream ofs(samplePath);
      cereal::BinaryOutputArchive archive(ofs);
      archive(samples[i]);
    }
    VLOG(0) << "Wrote " << samples.size() << " samples to " << outDir << "/";

    // Mark replay/player as done
    fsutils::touch(donePath);
  }
}

} // namespace

int main(int argc, char** argv) {
  cherrypi::init();
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  cherrypi::initLogging(argv[0], "", true);

  auto race = tc::BW::Race::_from_string(FLAGS_race.c_str());
  fsutils::mkdir(FLAGS_output_path);

  int numFailed = 0;
  for (int i = 1; i < argc; i++) {
// In release mode, just continue in case of errors. In debug mode, crash
// and burn so we can find the error more easily.
#ifdef NDEBUG
    try {
      VLOG(1) << "Processing replay " << argv[i];
      processReplay(argv[i], race, FLAGS_output_path, FLAGS_overwrite);
    } catch (std::exception const& ex) {
      LOG(ERROR) << ex.what();
      ++numFailed;
    }
#else // NDEBUG
    VLOG(1) << "Processing replay " << argv[i];
    processReplay(argv[i], race, FLAGS_output_path, FLAGS_overwrite);
#endif // NDEBUG
  }

  return std::min(numFailed, 255);
}
