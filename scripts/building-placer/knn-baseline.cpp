/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * A nearest neighbor baseline for building placement predicition.
 */

#include "common.h"

#include <common/autograd/utils.h>
#include <common/datareader.h>
#include <common/fsutils.h>
#include <common/rand.h>

#include <gflags/gflags.h>
#include <nlohmann/json.hpp>

DEFINE_int32(map, -1, "Restrict to specific map");
DEFINE_int32(num_train_threads, 1, "Parallelize training");
DEFINE_int32(num_valid_threads, 1, "Parallelize validation");
DEFINE_string(sample_path, ".", "Load samples from here");
DEFINE_string(
    map_spec,
    "scripts/building-placer/stardata_rep_map.json.zst",
    "Path to JSON mapping from replay files to map IDs");
DEFINE_int32(num_data_threads, 1, "Number of data loader threads");
DEFINE_int32(
    seed,
    -1,
    "Random seed. Use default seed based on current time if -1");

// Model options for building-placer model based embedding
DEFINE_bool(gpu, common::gpuAvailable(), "Train on GPU");
DEFINE_string(
    distance_feature,
    "buildings",
    "Set the distance metrics used for KNN (buildings|embedding|oracle)");
DEFINE_string(
    similarity_metric,
    "l2",
    "Set the similarity metric used for KNN (l1|l2|cosine), only works in "
    "embedding mode");
DEFINE_bool(
    filter_buildings,
    true,
    "Filter candidates by same buildings in same area");
DEFINE_string(model_path, "", "Path to a model.bin file for embedding");

using namespace cherrypi;
using namespace common;
using nlohmann::json;

namespace {
cpid::MetricsContext gMetrics;

std::unordered_map<std::string, int> readMapIds(std::string const& path) {
  zstd::ifstream ifs(path);
  json doc;
  ifs >> doc;

  std::unordered_map<std::string, int> mapIds;
  for (json::iterator it = doc.begin(); it != doc.end(); ++it) {
    // This mapping does not contain the replay file extension, but
    // BuildingPlacerSample::mapName (which we want this to map against) does.
    mapIds[it.key() + ".rep"] = it.value().get<int>();
  }
  return mapIds;
}

struct Building {
  Position pos;
  UnitType type;
};

struct ReducedSample {
  int areaId = 0;
  std::vector<Building> alliedBuildings;
  std::vector<Building> alliedBuildingsInArea;
  UnitType type = tc::BW::UnitType::Terran_Marine;
  Position action;
  torch::Tensor embedding;

  ReducedSample() {}
  ReducedSample(BuildingPlacerSample const& sample, torch::Tensor embedding)
      : areaId(sample.areaId),
        type(sample.features.type),
        action(sample.action),
        embedding(std::move(embedding)) {
    // Unfortunately we can't get accessors to const Tensor objects; use
    // const_cast as a workaround.
    auto pacc = const_cast<torch::Tensor&>(sample.features.units.positions)
                    .accessor<int, 2>();
    auto dacc = const_cast<torch::Tensor&>(sample.features.units.data)
                    .accessor<float, 2>();
    for (auto i = 0U; i < pacc.size(0); i++) {
      // It's not one of our units if it's not among our cannels; recall that
      // unit types are placed in three exclusive bins: (allied, enemy, neutral)
      if (dacc[i][0] > UnitTypeFeaturizer::kNumUnitTypes / 3) {
        continue;
      }
      if (tc::BW::data::IsBuilding[int(dacc[i][0])]) {
        alliedBuildings.push_back({{pacc[i][1], pacc[i][0]}, int(dacc[i][0])});
      }
    }

    // Keep buildings sorted by type so comparing across samples is easy
    std::sort(
        alliedBuildings.begin(),
        alliedBuildings.end(),
        [](auto const& bldgA, auto const& bldgB) {
          return bldgA.type < bldgB.type;
        });

    // Area is marked in position tensor with non-zero probability
    auto upcP =
        selectFeatures(sample.features.map, {PlainFeatureType::UserFeature1})
            .tensor[0];
    auto acc = upcP.accessor<float, 2>();
    for (auto const& b : alliedBuildings) {
      if (acc[b.pos.y / sample.features.map.scale]
             [b.pos.x / sample.features.map.scale] > 0.0f) {
        alliedBuildingsInArea.push_back(b);
      }
    }
  }
};

struct Samples {
  std::mutex mutex;
  // Indexed by (mapId, areaId)
  std::map<std::pair<int, int>, std::vector<ReducedSample>> d;
  size_t count = 0;
};

std::vector<ReducedSample> reduceBatch(
    std::vector<BuildingPlacerSample> const& samples,
    torch::Tensor const& embeddings) {
  std::vector<ReducedSample> reduced;
  for (size_t i = 0; i < samples.size(); i++) {
    reduced.emplace_back(
        samples[i], embeddings.defined() ? embeddings[i] : torch::Tensor());
  }
  return reduced;
}

void reduceTrainSamples(
    int id,
    Samples* dest,
    std::unordered_map<std::string, int> const& maps,
    std::shared_ptr<BuildingPlacerModel> model) {
  auto dr = common::makeDataReader<BuildingPlacerSample>(
      fsutils::readLinesPartition(
          FLAGS_sample_path + "/train.list", id, FLAGS_num_train_threads),
      FLAGS_num_data_threads, // threads
      128, // batch size
      [&](auto samples) {
        std::vector<int> mapIds;
        for (auto const& sample : samples) {
          auto mapIt = maps.find(sample.mapName);
          if (mapIt == maps.end()) {
            LOG(WARNING) << "No map ID for replay " << sample.mapName;
          }
          mapIds.push_back(mapIt == maps.end() ? -1 : mapIt->second);
        }

        auto batch = model->makeInputBatch(samples);
        VLOG(3) << "Get batch from the model";
        torch::NoGradGuard g_;
        auto embedOutput = FLAGS_distance_feature == "embedding"
            ? model->forward(batch)["output"].to(at::kCPU)
            : torch::Tensor();
        VLOG(3) << "Get embeddings from the batch" << embedOutput.sizes();
        return std::make_pair(mapIds, reduceBatch(samples, embedOutput));
      },
      FLAGS_sample_path);
  auto it = dr.iterator();

  decltype(dest->d) samples;
  size_t nsamples = 0;
  while (it->hasNext()) {
    std::vector<int> mapIds;
    std::vector<ReducedSample> rsamples;
    std::tie(mapIds, rsamples) = it->next();
    for (auto i = 0U; i < mapIds.size(); i++) {
      if (mapIds[i] >= 0) {
        samples[std::make_pair(mapIds[i], rsamples[i].areaId)].push_back(
            std::move(rsamples[i]));
        nsamples++;

        if (VLOG_IS_ON(2)) {
          VLOG_EVERY_N(2, 1000) << google::COUNTER << " samples";
        } else if (VLOG_IS_ON(1)) {
          VLOG_EVERY_N(1, 10000) << google::COUNTER << " samples";
        } else {
          VLOG_EVERY_N(0, 100000) << google::COUNTER << " samples";
        }
      }
    }
  }

  std::lock_guard<std::mutex> lock(dest->mutex);
  dest->count += nsamples;
  for (auto& it : samples) {
    auto& vec = dest->d[it.first];
    vec.insert(vec.end(), it.second.begin(), it.second.end());
  }
}

void scoreValidSamples(
    int id,
    std::map<UnitType, uint32_t>& typeN,
    Samples const& samples,
    std::unordered_map<std::string, int> const& maps,
    std::shared_ptr<BuildingPlacerModel> model) {
  auto dr = common::makeDataReader<BuildingPlacerSample>(
      fsutils::readLinesPartition(
          FLAGS_sample_path + "/valid.list", id, FLAGS_num_valid_threads),
      1, // threads
      1, // batch size
      [&](auto samples) {
        if (samples.empty()) {
          return std::make_tuple(std::string(), -1, -1, ReducedSample());
        }
        // Assume single-element batches
        auto mapIt = maps.find(samples[0].mapName);
        if (mapIt == maps.end()) {
          LOG(WARNING) << "No map ID for replay " << samples[0].mapName;
        }
        auto mapId = (mapIt == maps.end() ? -1 : mapIt->second);
        return std::make_tuple(
            samples[0].mapName,
            mapId,
            samples[0].frame,
            ReducedSample(
                samples[0],
                model->forward(model->makeInputBatch(samples))["output"][0].to(
                    at::kCPU)));
      },
      FLAGS_sample_path);
  auto it = dr.iterator();
  std::unordered_map<std::string, uint32_t> myPerf;
  std::unordered_map<UnitType, uint32_t> myTypeN;
  while (it->hasNext()) {
    std::string mapName;
    int mapId;
    FrameNum frame;
    ReducedSample rsample;
    std::tie(mapName, mapId, frame, rsample) = it->next();

    // Restrict to single map?
    if (FLAGS_map >= 0 && mapId != FLAGS_map) {
      continue;
    }

    myPerf["n"] += 1;
    myTypeN[rsample.type] += 1;

    auto sit = samples.d.find(std::make_pair(mapId, rsample.areaId));
    if (sit == samples.d.end()) {
      // No samples for this map and area unfortunately
      for (size_t i = 1; i < kMetricsList.size(); i++) {
        myPerf[fmt::format("{}_{}", kMetricsList[i], rsample.type)] += 1;
      }
      continue;
    }

    // Initial candidates: on same map and area
    std::vector<ReducedSample> candidates = sit->second;
    VLOG(3) << candidates.size() << " candidates on map " << mapId
            << " and area " << rsample.areaId;

    // Filter candidates: same requested type
    candidates.erase(
        std::remove_if(
            candidates.begin(),
            candidates.end(),
            [&](ReducedSample const& rs) { return rs.type != rsample.type; }),
        candidates.end());

    // Filter candidates: same buildings (in area)
    if (FLAGS_filter_buildings) {
      candidates.erase(
          std::remove_if(
              candidates.begin(),
              candidates.end(),
              [&](ReducedSample const& rs) {
                return !std::equal(
                    rs.alliedBuildingsInArea.begin(),
                    rs.alliedBuildingsInArea.end(),
                    rsample.alliedBuildingsInArea.begin(),
                    rsample.alliedBuildingsInArea.end(),
                    [](auto const& bldgA, auto const& bldgB) {
                      return bldgA.type == bldgB.type;
                    });
              }),
          candidates.end());
      VLOG(3) << candidates.size() << " candidates with equal buildings";
    }

    if (candidates.empty()) {
      // Won't be able to make it for this one -- no candidates
      for (size_t i = 1; i < kMetricsList.size(); i++) {
        myPerf[fmt::format("{}_{}", kMetricsList[i], rsample.type)] += 1;
      }
      continue;
    }

    auto cosineDist = [](torch::Tensor const& a, torch::Tensor const& b) {
      return (a.dot(b) / (a.norm(2) * b.norm(2))).item<float>();
    };

    // Rank candidates by cumulative euclidian distance of within-area buildings
    std::vector<std::pair<std::reference_wrapper<ReducedSample const>, float>>
        candidateDist;
    if (FLAGS_distance_feature == "buildings") {
      for (auto const& c : candidates) {
        auto cumDist = 0.0f;
        auto cbuildings = c.alliedBuildingsInArea;

        for (auto const& bldg : rsample.alliedBuildingsInArea) {
          // Greedy matching: use closest building with given type
          auto minit = cbuildings.end();
          auto mind = kfMax;
          for (auto it = cbuildings.begin(); it != cbuildings.end(); ++it) {
            if (it->type != bldg.type) {
              continue;
            }
            auto d = utils::distance(bldg.pos, it->pos);
            if (d < mind) {
              mind = d;
              minit = it;
            }
          }

          if (minit != cbuildings.end()) {
            cumDist += mind;
            cbuildings.erase(minit);
          } else {
            throw std::runtime_error("Could not find matching building??");
          }
        }
        candidateDist.push_back(std::make_pair(std::cref(c), cumDist));
      }
    } else if (FLAGS_distance_feature == "embedding") {
      for (auto const& c : candidates) {
        if (FLAGS_similarity_metric == "l1") {
          candidateDist.push_back(
              std::make_pair(
                  std::cref(c),
                  at::dist(rsample.embedding, c.embedding, 1).item<float>()));
        } else if (FLAGS_similarity_metric == "l2") {
          candidateDist.push_back(
              std::make_pair(
                  std::cref(c),
                  at::dist(rsample.embedding, c.embedding, 2).item<float>()));
        } else if (FLAGS_similarity_metric == "cosine") {
          candidateDist.push_back(
              std::make_pair(
                  std::cref(c), cosineDist(rsample.embedding, c.embedding)));
        } else {
          throw std::runtime_error("similarity metric not defined!");
        }
      }
    } else if (FLAGS_distance_feature == "oracle") {
      for (auto const& c : candidates) {
        candidateDist.push_back(
            std::make_pair(
                std::cref(c), utils::distance(rsample.action, c.action)));
      }
    } else {
      throw std::runtime_error(
          "Unknown distance feature: " + FLAGS_distance_feature);
    }

    std::sort(
        candidateDist.begin(),
        candidateDist.end(),
        [](auto const& cd1, auto const& cd2) {
          return cd1.second < cd2.second;
        });

    auto topN = [](auto candidates, Position target, size_t n, int scale) {
      for (auto i = 0U; i < std::min(candidates.size(), n); i++) {
        if ((candidates[i].first.get().action / scale) == (target / scale)) {
          return 0; // hit: no error
        }
      }
      return 1;
    };
    auto dN = [](auto candidates, Position target, size_t n, int scale) {
      auto candidate = candidates[0].first.get().action / scale;
      auto targetScaled = target / scale;
      return (size_t)std::abs(candidate.x - targetScaled.x) <= n &&
              (size_t)std::abs(candidate.y - targetScaled.y) <= n
          ? 0
          : 1;
    };

    // Score
    auto buildTileScale = tc::BW::XYWalktilesPerBuildtile;
    myPerf[fmt::format("top1_{}", rsample.type)] +=
        topN(candidateDist, rsample.action, 1, buildTileScale);
    myPerf[fmt::format("top5_{}", rsample.type)] +=
        topN(candidateDist, rsample.action, 5, buildTileScale);
    myPerf[fmt::format("d1_{}", rsample.type)] +=
        dN(candidateDist, rsample.action, 1, buildTileScale);
    myPerf[fmt::format("d3_{}", rsample.type)] +=
        dN(candidateDist, rsample.action, 3, buildTileScale);

    if (VLOG_IS_ON(2)) {
      VLOG_EVERY_N(2, 100) << google::COUNTER << " samples";
    } else if (VLOG_IS_ON(1)) {
      VLOG_EVERY_N(1, 1000) << google::COUNTER << " samples";
    } else {
      VLOG_EVERY_N(0, 10000) << google::COUNTER << " samples";
    }
  }

  // Sum performance
  gMetrics.incCounter("n", myPerf["n"]);

  for (auto& it : myTypeN) {
    auto type = it.first;
    for (auto const& metric : kMetricsList) {
      auto index = fmt::format("{}_{}", metric, type);
      gMetrics.incCounter(index, myPerf[index]);
    }
  }

  static std::mutex mutex; // Protecting typeN
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& it : myTypeN) {
    auto type = it.first;
    typeN[type] += it.second;
  }
}
} // namespace

int main(int argc, char** argv) {
  cherrypi::init();
  std::map<UnitType, uint32_t> typeN;
  FLAGS_logtostderr = true;
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (FLAGS_seed < 0) {
    FLAGS_seed = common::Rand::defaultRandomSeed();
  }
  common::Rand::setSeed(FLAGS_seed);

  VLOG(0) << std::string(42, '=');
  for (auto const& it : utils::gflagsValues(__FILE__)) {
    VLOG(0) << it.first << ": " << it.second;
  }
  VLOG(0) << std::string(42, '=');

  auto model = BuildingPlacerModel().masked(false).logprobs(false).make();
  if (!FLAGS_model_path.empty()) {
    ag::load(FLAGS_model_path, model);
  }
  if (FLAGS_gpu) {
    model->to(torch::kCUDA);
  }

  VLOG(0) << "Building database of reduced training set samples";
  auto maps = readMapIds(FLAGS_map_spec);
  Samples samples;
  {
    std::vector<std::thread> threads;
    for (auto i = 0; i < FLAGS_num_train_threads; i++) {
      threads.emplace_back(
          &reduceTrainSamples, i, &samples, std::cref(maps), model);
    }
    for (auto& thread : threads) {
      thread.join();
    }
  }
  VLOG(0) << "Collected data from " << samples.count << " samples";

  // Query validation set examples
  VLOG(0) << "Scoring validation set";
  {
    std::vector<std::thread> threads;
    for (auto i = 0; i < FLAGS_num_valid_threads; i++) {
      threads.emplace_back(
          &scoreValidSamples,
          i,
          std::ref(typeN),
          std::cref(samples),
          std::cref(maps),
          model);
    }
    for (auto& thread : threads) {
      thread.join();
    }
  }
  pushNormalizedMetrics(&gMetrics, typeN);
  logPerf(&gMetrics, typeN, 1, 1);
  gMetrics.dumpJson("metrics.json");
  return EXIT_SUCCESS;
}
