/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * Print game information for a corpus in line-delimited JOSN format
 */

#include "fsutils.h"
#include "models/bos/sample.h"
#include "utils/parallel.h"
#include "zstdstream.h"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

DEFINE_string(sample_list, "train.list", "Path to samples.list file");

using namespace cherrypi;
using namespace cpid;

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  cherrypi::initLogging(argv[0], "", true);
  cherrypi::init();

  auto dir = fsutils::dirname(FLAGS_sample_list);
  auto files = fsutils::readLines(FLAGS_sample_list);

  BufferedConsumer<std::pair<std::string, BosEpisodeData>, 1> print(
      10, [&](std::pair<std::string, BosEpisodeData> data) {
        std::string file = data.first;
        BosEpisodeData epd = data.second;
        std::shared_ptr<BosStaticData> sdata;
        std::vector<std::pair<int, std::string>> builds;
        for (auto i = 0U; i < epd.frames.size() - 1; i++) {
          auto frame =
              std::static_pointer_cast<BosReplayBufferFrame>(epd.frames[i]);
          auto& sample = frame->sample;
          sdata = sample.staticData;
          auto& resources = sample.resources;
          if (resources.ore < 0 || resources.gas < 0 ||
              resources.used_psi < 0 || resources.total_psi < 0) {
            VLOG(2) << "Something is wrong: ore " << resources.ore << " gas "
                    << resources.gas << " used_psi " << resources.used_psi
                    << " total_psi " << resources.total_psi
                    << " the file name is " << file;
          }
          if (sample.switched || i == 0 ||
              sample.buildOrder != sample.nextBuildOrder) {
            builds.push_back(
                std::make_pair(sample.frame, sample.nextBuildOrder));
          }
        }

        auto jd = nlohmann::json{
            {"file", file},
            {"builds", builds},
            {"opponent", sdata->opponentName},
            {"n", epd.frames.size() - 1},
            {"won", sdata->won},
        };
        std::cout << jd << std::endl;
      });
  BufferedConsumer<std::string, 32> deser(128, [&](std::string f) {
    BosEpisodeData epd;
    try {
      zstd::ifstream is(fmt::format("{}/{}", dir, f));
      cereal::BinaryInputArchive ar(is);
      ar(epd);
    } catch (std::exception const& e) {
      VLOG(0) << "Cannot read " << f << ": " << e.what();
      return;
    }
    if (epd.frames.size() >= 2) {
      print.enqueue(std::make_pair(f, std::move(epd)));
    }
  });

  for (auto const& f : files) {
    deser.enqueue(f);
  }

  deser.wait();
  print.wait();
  return EXIT_SUCCESS;
}
