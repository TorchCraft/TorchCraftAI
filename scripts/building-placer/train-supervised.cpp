/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * Performs offline training for BuildingPlacer. Use `bp-collect-replay-samples`
 * to collect samples from replays and then launch training like this:
 *
 * ./build/tutorials/building-placer/supervised/train-supervised -sample_path \
 *    <sample_path>
 */

#include "common.h"

#include <cpid/distributed.h>
#include <cpid/optimizers.h>

#include <common/autograd/utils.h>
#include <common/fsutils.h>
#include <common/datareader.h>
#include <common/rand.h>

#include <gflags/gflags.h>
#include <visdom/visdom.h>

using namespace cherrypi;
namespace dist = cpid::distributed;
namespace fsutils = common::fsutils;
auto const vopts = &visdom::makeOpts;

// Model options
DEFINE_bool(masked, false, "Mask softmax by valid locations");

// Training  options
DEFINE_int32(
    seed,
    -1,
    "Random seed. Use default seed based on current time if -1");
DEFINE_uint64(batch_size, 64, "Batch this many examples per update");
DEFINE_string(
    sample_path,
    "/tmp/building-placer-samples",
    "Load samples from here");
DEFINE_bool(gpu, common::gpuAvailable(), "Train on GPU");
DEFINE_int32(num_data_threads, 4, "Number of data loader threads");
DEFINE_int32(valid_every, -1, "Validate every n updates (or once per epoch)");
DEFINE_bool(
    validate,
    false,
    "Don't train; just validate model_best.bin on all non-train.list files");

// Visualization and analytics
DEFINE_string(visdom_server, "http://localhost", "Visdom server address");
DEFINE_int32(visdom_port, 8097, "Visdom server port");
DEFINE_string(
    visdom_env,
    "",
    "Visdom environment (empty string disables visualization)");

namespace {

// A big, global struct to keep track of relevant training metrics
struct {
  struct {
    // For visualization: sample, target, output for a random example
    FeatureData input;
    UnitType type;
    torch::Tensor target;
    torch::Tensor output;
  } visuals;
  std::map<UnitType, uint32_t> typeN;
  std::map<UnitType, double> loss;
  std::map<UnitType, double> lastLoss;
  std::map<std::string, uint32_t> counters;
  cpid::MetricsContext ctx;
  std::map<std::string, std::string> visdomWindows;

  double average(std::map<UnitType, double> const& data, UnitType type, int n)
      const {
    double ret = 0;
    uint32_t denom = 1;
    if (type >= 0) {
      auto it = data.find(type);
      auto nit = typeN.find(type);
      if (it != data.end() && nit != typeN.end()) {
        ret = it->second;
        denom = nit->second;
      } else {
        LOG(WARNING) << "No metrics to average (type=" << type << ")";
      }
    } else {
      for (auto const& it : data) {
        ret += it.second;
      }
      denom = n;
    }
    return ret / denom;
  }

  double avgLoss(UnitType type, uint32_t n) const {
    return average(loss, type, n);
  }

  double avgLastLoss(UnitType type, uint32_t n) const {
    return average(lastLoss, type, n);
  }

  void pushMetrics() {
    ctx.setCounter("n", counters["n"]);
    ctx.pushEvent("n", counters["n"]);
    ctx.setCounter("global_n", counters["global_n"]);
    ctx.pushEvent("global_n", counters["global_n"]);
    for (auto const& it : typeN) {
      for (auto const& metric : kMetricsList) {
        auto index = fmt::format("{}_{}", metric, it.first);
        ctx.setCounter(index, counters[index]);
      }
    }
  }

  void clearAllExceptTimeseries() {
    visuals.input = FeatureData();
    visuals.type = UnitType();
    visuals.target = torch::Tensor();
    visuals.output = torch::Tensor();
    typeN.clear();
    loss.clear();
    counters.clear();
  }
} gMetrics;

// Plot metrics and samples via Visdom
void plot(visdom::Visdom& vs, int epoch, int steps) {
  auto updatePlot = [&](
      std::string const& window,
      std::string const& title,
      std::string const& ytitle,
      float value) -> std::string {
    return vs.line(
        torch::tensor(value),
        torch::tensor(float(steps)),
        window,
        vopts({{"title", title}, {"xtitle", "Updates"}, {"ytitle", ytitle}}),
        window.empty() ? visdom::UpdateMethod::None
                       : visdom::UpdateMethod::Append);
  };

  // Plot metrics
  for (auto const& metric : kMetricsList) {
    auto index = fmt::format("{}_normalized", metric);
    gMetrics.visdomWindows[metric] = updatePlot(
        gMetrics.visdomWindows[metric],
        metric + (metric == "loss" ? "" : "-error"),
        metric == "loss" ? "Loss" : "Error",
        gMetrics.ctx.getLastEventValue(index));
  }

  // Visualize network input
  auto prefix = fmt::format(
      "Valid@{}/{} {}<br>",
      epoch,
      steps,
      getUnitBuildType(gMetrics.visuals.type)->name);
  for (auto const& desc : gMetrics.visuals.input.desc) {
    if (desc.name == "UnitType") {
      vs.heatmap(
          selectFeatures(gMetrics.visuals.input, {desc.type})
              .tensor.sum(0)
              .gt(0)
              .toType(at::kFloat),
          vopts({{"title", prefix + desc.name}}));
    } else {
      vs.heatmap(
          selectFeatures(gMetrics.visuals.input, {desc.type}).tensor.sum(0),
          vopts({{"title", prefix + desc.name}}));
    }
  }

  // Visualize target and network output
  vs.heatmap(gMetrics.visuals.target, vopts({{"title", prefix + " target"}}));
  vs.heatmap(gMetrics.visuals.output, vopts({{"title", prefix + " output"}}));
}

// Top-N error, i.e. count for which output, target is not in the top-n
int topN(torch::Tensor output, torch::Tensor target, int n) {
  output = output.view({output.size(0), -1});
  target = target.view({target.size(0), -1});
  auto top = std::get<1>(output.topk(n, 1));
  auto tgt = target.expand({output.size(0), n});
  return output.size(0) - top.eq(tgt).sum().item<int32_t>();
}

// D-N error, i.e. count for which output, target is not in the square of n
int dN(torch::Tensor output, torch::Tensor target, int n) {
  auto planeDim = output.sizes().back();
  output = output.view({output.size(0), -1});
  auto top = std::get<1>(at::max(output, 1));
  auto diff = at::abs(top - target);
  auto diffX = diff % planeDim;
  auto diffY = diff / planeDim;
  return diffX.gt(n).__or__(diffY.gt(n)).sum().item<int32_t>();
}

void validate(
    std::shared_ptr<BuildingPlacerModel> model,
    common::DataReader<BuildingPlacerSample>& validData) {
  auto start = hires_clock::now();

  model->eval();
  gMetrics.clearAllExceptTimeseries();

  validData.shuffle();
  auto trIt = common::makeDataReaderTransform(
      validData.iterator(),
      [&](auto samples) {
        return std::make_pair(samples, model->makeBatch(samples));
      },
      &dist::setGPUToLocalRank);
  while (trIt->hasNext()) {
    auto sbatch = trIt->next();
    auto samples = sbatch.first;
    auto batch = sbatch.second;
    if (batch.first.getDict().empty()) {
      continue;
    }
    auto output = model->forward(batch.first)["output"];
    auto target = batch.second["target"];
    // Compute loss per batch element for fine-grained metrics
    auto losses = at::nll_loss(output, target, {}, Reduction::None);

    // Collect metrics for every sample in the batch
    for (auto j = 0U; j < losses.size(0); j++) {
      auto type = samples[j].features.type;
      gMetrics.loss[type] += losses[j].sum().item<double>();
      gMetrics.counters[fmt::format("loss_{}", type)] +=
          losses[j].sum().item<double>();
      gMetrics.counters[fmt::format("top1_{}", type)] +=
          topN(output.slice(0, j, j + 1), target.slice(0, j, j + 1), 1);
      gMetrics.counters[fmt::format("top5_{}", type)] +=
          topN(output.slice(0, j, j + 1), target.slice(0, j, j + 1), 5);
      gMetrics.counters[fmt::format("d1_{}", type)] +=
          dN(output.slice(0, j, j + 1), target.slice(0, j, j + 1), 1);
      gMetrics.counters[fmt::format("d3_{}", type)] +=
          dN(output.slice(0, j, j + 1), target.slice(0, j, j + 1), 3);
    }
    gMetrics.counters["n"] += samples.size();
    for (auto j = 0U; j < samples.size(); j++) {
      gMetrics.typeN[samples[j].features.type]++;
    }

    if (!gMetrics.visuals.target.defined()) {
      // Save all input/output/targets for subsqeuent visualization
      gMetrics.visuals.input = combineFeatures(
          {samples[0].features.map,
           subsampleFeature(
               samples[0].unitFeaturizer.toSpatialFeature(
                   samples[0].features.units),
               SubsampleMethod::Sum,
               samples[0].features.map.scale)});
      gMetrics.visuals.type = samples[0].features.type;
      torch::Tensor visout = output[0].exp().to(at::kCPU);

      // Reshape output to 2D
      auto size = int64_t(sqrt(visout.size(0)));
      visout = visout.view({size, size});
      gMetrics.visuals.output = visout;

      // One-hot, 2D tensor from ground truth targget position
      auto vistarget = torch::zeros_like(visout);
      vistarget.view({-1})[target[0].item<int32_t>()] = 1.0f;
      gMetrics.visuals.target = vistarget;
    }
  }

  auto duration = hires_clock::now() - start;
  VLOG(2)
      << "Validation done in "
      << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count()
      << "ms";
}

int synchronizeGradients(
    std::shared_ptr<BuildingPlacerModel> model,
    int batchSize) {
  for (auto& var : model->parameters()) {
    if (!var.grad().defined()) {
      continue;
    }
    dist::allreduce(var.grad());
    var.grad().div_(dist::globalContext()->size);
  }

  static int dBatchSize;
  dBatchSize = batchSize;
  dist::allreduce(&dBatchSize, 1);
  return dBatchSize;
}

// Sums up perf data via allreduce
void synchronizePerf() {
  // Take care to never synchronize temporaries. The safest (and ugliest) bet is
  // to use dedicated static variables.
  static int dBuf;
  static float dBufF;

  // Unit type counters, assuming that all workers have data for the same set of
  // unit types.
  for (auto& it : gMetrics.typeN) {
    dBuf = it.second;
    dist::allreduce(&dBuf, 1);
    it.second = dBuf;
  }
  for (auto const& it : gMetrics.typeN) {
    for (auto const& metric : kMetricsList) {
      auto index = fmt::format("{}_{}", metric, it.first);
      dBuf = gMetrics.counters[index];
      dist::allreduce(&dBuf, 1);
      gMetrics.counters[index] = dBuf;
    }
  }

  // Total sample count
  dBuf = gMetrics.counters["n"];
  dist::allreduce(&dBuf, 1);
  gMetrics.counters["global_n"] = dBuf;

  // Per-unit loss
  for (auto& it : gMetrics.loss) {
    dBufF = it.second;
    dist::allreduce(&dBufF, 1);
    it.second = dBufF / dist::globalContext()->size;
  }
}

void trainLoop(
    std::shared_ptr<BuildingPlacerModel> model,
    common::DataReader<BuildingPlacerSample>& trainData,
    common::DataReader<BuildingPlacerSample>& validData,
    std::shared_ptr<visdom::Visdom> vs) {
  if (FLAGS_gpu) {
    model->to(torch::kCUDA);
  }

  auto optim = cpid::selectOptimizer(model);

  int epoch = -1;
  int steps = 0;
  int epochSteps = 0;
  int timesLrReduced = 0;
  ThroughputMeter tpm;

  auto runValidation = [&]() {
    validate(model, validData);
    synchronizePerf();
    gMetrics.pushMetrics();
    if (dist::globalContext()->rank == 0) {
      pushNormalizedMetrics(&gMetrics.ctx, gMetrics.typeN);
      logPerf(&gMetrics.ctx, gMetrics.typeN, epoch, steps);
      if (vs) {
        plot(*vs.get(), epoch, steps);
      }
    }

    // Monitor loss on validation set; if it stagnates, reduce the learning rate
    auto globalN = gMetrics.ctx.getLastEvents("global_n", 2);
    if (!gMetrics.lastLoss.empty() && globalN.size() == 2 &&
        gMetrics.avgLoss(-1, globalN[1].second) >=
            gMetrics.avgLastLoss(-1, globalN[0].second)) {
      if (FLAGS_optim == "sgd") {
        auto op = std::dynamic_pointer_cast<torch::optim::SGD>(optim);
        op->options.learning_rate_ /= 10.0f;
        timesLrReduced++;
        VLOG(0) << "Validation loss stagnating, lowering learning rate to "
                << op->options.learning_rate_;
      } else {
        VLOG(0) << "Validation loss stagnating, aborting";
        timesLrReduced = 100;
      }
    } else {
      // Always keep a copy of the best model we found so far
      ag::save("model_best.bin", model);
    }
    gMetrics.lastLoss = gMetrics.loss;
    model->train();
    tpm.reset();
  };

  auto keepOnTraining = [&]() { return timesLrReduced < 3; };

  model->train();
  while (keepOnTraining()) {
    epoch++;
    epochSteps = 0;
    double cavgLoss = 0;

    trainData.shuffle();
    tpm.reset();

    auto trIt = common::makeDataReaderTransform(
        trainData.iterator(),
        [&](auto samples) { return model->makeBatch(samples); },
        &dist::setGPUToLocalRank);
    while (trIt->hasNext()) {
      auto batch = trIt->next();
      if (batch.first.getDict().empty()) {
        continue;
      }
      // The model returns the output along with the true buildability mask;
      // we're only interested in the output here
      auto output = model->forward(batch.first)["output"];
      auto target = batch.second["target"];
      torch::Tensor loss = at::nll_loss(output, target);

      optim->zero_grad();
      loss.backward();
      auto nsamples = synchronizeGradients(model, target.size(0));
      optim->step();

      steps++;
      epochSteps++;
      tpm.n += nsamples;

      cavgLoss += loss.item<double>();
      if (dist::globalContext()->rank == 0) { // Log on "master" only
        if ((VLOG_IS_ON(1) && steps % 10 == 0) || steps % 100 == 0) {
          VLOG(0) << epoch << "/" << steps
                  << fmt::format(" cum_loss:{:.04f}", cavgLoss / epochSteps)
                  << fmt::format(" cur_loss:{:.04f}", loss.item<double>())
                  << fmt::format(" samples/s:{}", size_t(tpm.throughput()));
          gMetrics.ctx.pushEvent("cum_avg_loss", cavgLoss / epochSteps);
          gMetrics.ctx.pushEvent("cur_avg_loss", loss.item<double>());
          gMetrics.ctx.pushEvent("samples_per_sec", size_t(tpm.throughput()));
          tpm.reset();
        }
      }

      if (FLAGS_valid_every > 0 && (steps % FLAGS_valid_every) == 0) {
        runValidation();
        if (!keepOnTraining()) {
          break;
        }
      }
    }

    if (FLAGS_valid_every <= 0) {
      runValidation();
    }
  }
}

} // namespace

int main(int argc, char** argv) {
  FLAGS_logtostderr = true;
  FLAGS_lr = 0.01;
  FLAGS_optim = "sgd";
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  cherrypi::init();
  dist::init();

  if (FLAGS_seed < 0) {
    FLAGS_seed = common::Rand::defaultRandomSeed();
  }
  common::Rand::setSeed(FLAGS_seed);

  VLOG(1) << "Gloo rank: " << dist::globalContext()->rank << " and size "
          << dist::globalContext()->size;

  std::shared_ptr<visdom::Visdom> vs;
  if (dist::globalContext()->rank == 0) {
    VLOG(0) << "Training run started with " << dist::globalContext()->size
            << " workers";

    if (!FLAGS_visdom_env.empty()) {
      visdom::ConnectionParams vparams;
      vparams.server = FLAGS_visdom_server;
      vparams.port = FLAGS_visdom_port;
      vs = std::make_shared<visdom::Visdom>(vparams, FLAGS_visdom_env);

      std::ostringstream oss;
      oss << "<h4>Supervised building placer training</h4>";
      oss << "<p>Training started " << utils::curTimeString() << "</p>";
      oss << "<hl><p>";
      for (auto const& it : utils::cmerge(
               utils::gflagsValues(__FILE__), cpid::optimizerFlags())) {
        oss << "<b>" << it.first << "</b>: " << it.second << "<br>";
      }
      oss << "</p>";
      vs->text(oss.str());
    }

    VLOG(0) << std::string(42, '=');
    for (auto const& it :
         utils::cmerge(utils::gflagsValues(__FILE__), cpid::optimizerFlags())) {
      VLOG(0) << fmt::format("{}: {}", it.first, it.second);
    }
    VLOG(0) << std::string(42, '=');
  }

  auto model = BuildingPlacerModel()
                   .masked(FLAGS_masked)
                   .flatten(true)
                   .logprobs(true)
                   .make();

  if (FLAGS_validate) {
    if (dist::globalContext()->rank == 0) {
      ag::load("model_best.bin", model);
    }
    dist::broadcast(model);
    if (FLAGS_gpu) {
      model->to(torch::kCUDA);
    }

    auto lists = fsutils::find(FLAGS_sample_path, "*.list");
    lists.erase(
        std::remove_if(
            lists.begin(),
            lists.end(),
            [](std::string const& l) {
              return utils::endsWith(l, "train.list") ||
                  utils::endsWith(l, "all.list");
            }),
        lists.end());
    std::sort(lists.begin(), lists.end());

    for (auto const& list : lists) {
      VLOG_MASTER(0) << "Validating model on " << list;
      auto dr = common::DataReader<BuildingPlacerSample>(
          fsutils::readLinesPartition(
              list, dist::globalContext()->rank, dist::globalContext()->size),
          FLAGS_num_data_threads,
          FLAGS_batch_size,
          FLAGS_sample_path);

      validate(model, dr);
      synchronizePerf();
      gMetrics.pushMetrics();
      if (dist::globalContext()->rank == 0) {
        pushNormalizedMetrics(&gMetrics.ctx, gMetrics.typeN);
        logPerf(&gMetrics.ctx, gMetrics.typeN, 0, 0);
        if (vs) {
          plot(*vs.get(), 0, 0);
        }
      }
    }

  } else { // Training
    // Initial synchronization of model parameters
    dist::broadcast(model);

    // Normal training
    auto trainDr = common::DataReader<BuildingPlacerSample>(
        fsutils::readLinesPartition(
            FLAGS_sample_path + "/train.list",
            dist::globalContext()->rank,
            dist::globalContext()->size),
        FLAGS_num_data_threads,
        FLAGS_batch_size,
        FLAGS_sample_path);
    auto validDr = common::DataReader<BuildingPlacerSample>(
        fsutils::readLinesPartition(
            FLAGS_sample_path + "/valid.list",
            dist::globalContext()->rank,
            dist::globalContext()->size),
        FLAGS_num_data_threads,
        FLAGS_batch_size,
        FLAGS_sample_path);
    trainLoop(model, trainDr, validDr, vs);
  }

  gMetrics.ctx.dumpJson(
      fmt::format("{}-metrics.json", dist::globalContext()->rank));
  return EXIT_SUCCESS;
}
