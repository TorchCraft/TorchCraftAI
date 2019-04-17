/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "buildtype.h"
#include "features/defoggerfeatures.h"
#include "features/features.h"
#include "gameutils/gamevsbot.h"
#include "models/defogger.h"
#include "upcstorage.h"

#include <glog/logging.h>

#include "botcli-inl.h"
#include "gameutils/openbwprocess.h"

#include <regex>

using namespace cherrypi;
using namespace defogger;

DEFINE_string(race, "Zerg", "Play as this race");
DEFINE_string(opponent, "", "Play against this opponent");
DEFINE_string(map, "", "Play on this map");
DEFINE_string(
    replay_path,
    "bwapi-data/replays/%BOTNAME%_%BOTRACE%.rep",
    "Where to save resulting replays");
DEFINE_string(model_path, "", "npz file to load a model from");
DEFINE_bool(gui, false, "Enable OpenBW GUI");

// This model matches --run 007_ml_090801010002000000020000 in the
// starcraft_defogger. You'll want to change it if you change the model you
// train from Python
//
// Once you train a model in python, simply run
//    python sweep.py --run $ID --dump_npz
// and give the npz path here. Additionally, you'll have to change the
// variables below to match the model you trained.
constexpr auto defoggerFrameSkip = 40;
constexpr double divideBy = 10;
constexpr auto predictDelta = true;
DefoggerFeaturizer featurizer(32, 32, 32, 32);
std::shared_ptr<DefoggerModel> makeModel() {
  auto model = DefoggerModel(conv2dBuilder, at::relu, 32, 118, 32)
                   .n_lvls(2)
                   .midconv_depth(2)
                   .predict_delta(true)
                   .bypass_encoder(false)
                   .map_embsize(8)
                   .hid_dim(256)
                   .inp_embsize(256)
                   .enc_embsize(256)
                   .dec_embsize(128)
                   .midconv_kw(3)
                   .midconv_stride(2)
                   .upsample(common::UpsampleMode::Bilinear)
                   .make();

  if (torch::cuda::is_available()) {
    model->to(torch::kCUDA);
  }

  model->load_parameters(FLAGS_model_path);

  return std::dynamic_pointer_cast<DefoggerModel>(model);
}

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  ForkServer::startForkServer();
  cherrypi::init();

  if (FLAGS_seed >= 0) {
    common::Rand::setSeed(FLAGS_seed);
  }

  // We need to init the logging after we have parsed the command
  // line flags since it depends on flags set by it
  cherrypi::initLogging(argv[0], FLAGS_logsinkdir, FLAGS_logsinktostderr);

  FLAGS_replay_path = std::regex_replace(
      FLAGS_replay_path, std::regex("\\$PID"), std::to_string(getpid()));
  auto opponent = std::make_unique<GameVsBotInOpenBW>(
      FLAGS_map,
      tc::BW::Race::_from_string(FLAGS_race.c_str()),
      FLAGS_opponent,
      GameType::Melee,
      FLAGS_replay_path,
      FLAGS_gui);
  Player bot(opponent->makeClient());
  if (!FLAGS_replay_path.empty() && FLAGS_trace_along_replay_file.empty()) {
    FLAGS_trace_along_replay_file = FLAGS_replay_path;
  }
  setupPlayerFromCli(&bot);

  // In normal playing mode we don't need to save UPC-related data longer than
  // necessary
  bot.state()->board()->upcStorage()->setPersistent(false);
  bot.init();
  auto state = bot.state();

  // Let's setup the defogger
  auto model = makeModel();
  model->zero_hidden();
  std::deque<tc::Frame> lastFrames;

  auto mapFeatures = featurizePlain(
                         state,
                         {PlainFeatureType::Walkability,
                          PlainFeatureType::Buildability,
                          PlainFeatureType::GroundHeight,
                          PlainFeatureType::StartLocations},
                         Rect({0, 0}, {state->mapHeight(), state->mapWidth()}))
                         .tensor.unsqueeze(0);

  do {
    lastFrames.emplace_back(state->tcstate()->frame);
    if (lastFrames.size() == defoggerFrameSkip) {
      auto combined =
          DefoggerFeaturizer::combine(lastFrames, state->playerId());

      auto raceFeatures =
          torch::tensor(
              {(int64_t)state->myRace(),
               (int64_t)state->raceFromClient(state->firstOpponent())},
              torch::kI64)
              .unsqueeze(0);
      auto inputFeatures =
          featurizer
              .featurize(
                  state->tcstate()->frame,
                  state->mapWidth(),
                  state->mapHeight(),
                  state->playerId(),
                  torch::cuda::is_available() ? torch::kCUDA : torch::kCPU)
              .permute({2, 0, 1})
              .unsqueeze(0) /
          divideBy;
      if (torch::cuda::is_available()) {
        mapFeatures = mapFeatures.to(torch::kCUDA);
        raceFeatures = raceFeatures.to(torch::kCUDA);
        inputFeatures = inputFeatures.to(torch::kCUDA);
      }
      // Mapfeatures are 1 x C x H x W
      // RaceFeatures are 1 x 2
      // Input features are 1 x F x H x W
      auto out = model->forward(
          ag::Variant({mapFeatures, raceFeatures, inputFeatures}));
      // 0 is the regression head
      // 1 is the unit classification head
      // 2 is the building classification head
      // 3 is the g_op_bt head
      LOG(INFO) << out[0].sizes() << out[1].sizes() << out[2].sizes()
                << out[3].sizes();

      std::stringstream ss;
      auto regTotal = out[0];
      if (predictDelta) {
        regTotal = inputFeatures + regTotal;
      }
      regTotal = regTotal.mul(divideBy);
      regTotal = (regTotal * (regTotal > 0.1).to(torch::kFloat))
                     .sum(3)
                     .sum(2)
                     .squeeze()
                     .to(torch::kCPU);
      for (auto i = 118; i < 234; i++) {
        auto ind = i - 118;
        auto unit =
            tc::BW::UnitType::_from_integral(featurizer.itypemapper[ind]);
        if (getUnitBuildType(featurizer.itypemapper[ind])->race !=
            state->raceFromClient(state->firstOpponent())) {
          continue;
        }
        if (regTotal[i].item<float>() > 0.5) {
          ss << fmt::format(
              "({} = {}),", unit._to_string(), regTotal[i].item<float>());
        }
      }

      LOG(INFO) << "Defogger estimated unit counts: " << ss.str();

      lastFrames.clear();
    }
    bot.step();
  } while (!state->gameEnded());

  if (state->won()) {
    LOG(WARNING) << "Victory!!";
  } else {
    LOG(WARNING) << "Oh noes we lost :( -- with "
                 << state->unitsInfo().myBuildings().size()
                 << " buildings left";
  }

  cherrypi::shutdown(FLAGS_logsinktostderr);
  return EXIT_SUCCESS;
}
