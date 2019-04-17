/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "common/rand.h"

#include "cpid/batcher.h"
#include "cpid/centraltrainer.h"
#include "cpid/distributed.h"
#include "cpid/sampler.h"

#include <autogradpp/autograd.h>

#include <chrono>
#include <future>
#include <thread>

using namespace cpid;
namespace dist = cpid::distributed;

namespace {

char constexpr kMarioString[] = "it's-a-me";

struct MyReplayBufferFrame : ReplayBufferFrame {
  // Note: this struct has to be registered (at global scope) with
  // CEREAL_REGISTER_TYPE(MyReplayBufferFrame)
  std::string s;
  torch::Tensor t;
  uint32_t i;
  std::vector<float> fs;
  bool end;

  MyReplayBufferFrame(bool end = false) : end(end) {
    s = kMarioString;
    t = torch::rand({10, 10, 10});
    i = common::Rand::rand() % std::numeric_limits<uint32_t>::max();
    fs.resize((common::Rand::rand() % 20) + 1);
  }
  virtual ~MyReplayBufferFrame() = default;

  template <class Archive>
  void serialize(Archive& ar) {
    ar(cereal::base_class<ReplayBufferFrame>(this), s, t, i, fs, end);
  }
};

class MyCentralTrainer : public CentralTrainer {
 public:
  using CentralTrainer::CentralTrainer;

  size_t numBatchesReceived = 0;
  size_t numFramesReceived = 0;
  size_t numCorrectFramesReceived = 0;
  size_t numMariosReceived = 0;
  size_t numFinalFramesReceived = 0;
  std::vector<int> batchLengths;

 protected:
  void receivedFrames(GameUID const& gameId, EpisodeKey const& episodeKey)
      override {
    numBatchesReceived++;

    // Verify episode type
    auto& episode = replayer_.get(gameId, episodeKey);
    batchLengths.push_back(episode.size());
    for (auto const& frame : episode) {
      numFramesReceived++;
      auto f = std::dynamic_pointer_cast<MyReplayBufferFrame>(frame);
      if (f != nullptr) {
        numCorrectFramesReceived++;
        if (f->s == kMarioString) {
          numMariosReceived++;
        }
        if (f->end) {
          numFinalFramesReceived++;
        }
      }
    }
  }
};

static int constexpr partialLength = 20;
static int constexpr sendInterval = 10;
class PartialCentralTrainer : public MyCentralTrainer {
 public:
  using MyCentralTrainer::MyCentralTrainer;

 protected:
  uint32_t getMaxBatchLength() const override {
    return partialLength;
  }
  uint32_t getSendInterval() const override {
    return sendInterval;
  }
};

class ContinuousCentralTrainer : public PartialCentralTrainer {
 public:
  using PartialCentralTrainer::PartialCentralTrainer;

 protected:
  bool serveContinuously() const override {
    return true;
  }
};

void worker(
    std::shared_ptr<Trainer> trainer,
    int numEpisodes,
    int minlen,
    int maxlen) {
  std::uniform_int_distribution<int> eplen(minlen, maxlen);
  for (auto i = 0; i < numEpisodes; i++) {
    EpisodeHandle handle;
    while (!(handle = trainer->startEpisode())) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto len = common::Rand::sample(eplen);
    for (auto j = 0; j < len; j++) {
      auto frame = std::make_shared<MyReplayBufferFrame>();
      trainer->step(handle, frame);
    }
    auto frame = std::make_shared<MyReplayBufferFrame>(true);
    trainer->step(handle, frame, true);

    std::this_thread::sleep_for(
        std::chrono::milliseconds(common::Rand::rand() % 50));
  }
}

} // namespace

CEREAL_REGISTER_TYPE(MyReplayBufferFrame)

// Feel free to run this test with `./distrun -n 2 --` or sth for testing.
CASE("centraltrainer/basic[.hide]") {
  dist::init();

  auto metrics = std::make_shared<MetricsContext>();
  auto trainer = std::make_shared<MyCentralTrainer>(
      dist::globalContext()->rank % 4 == 0,
      nullptr,
      nullptr,
      std::make_unique<BaseSampler>());
  trainer->setMetricsContext(metrics);

  std::vector<std::thread> threads;
  int64_t numTotalEpisodes = 0;
  for (auto i = 0; i < 2; i++) {
    threads.emplace_back(worker, trainer, 10, 1, 100);
    numTotalEpisodes += 10;
  }
  numTotalEpisodes *= dist::globalContext()->size;

  static int64_t numReceived = 0;
  while (numReceived < numTotalEpisodes) {
    trainer->update();
    numReceived = trainer->numBatchesReceived;
    dist::allreduce(&numReceived, 1);
  }
  for (auto& th : threads) {
    EXPECT((th.join(), true));
  }

  EXPECT(numReceived == numTotalEpisodes);
  if (trainer->isServer()) {
    EXPECT(trainer->numFramesReceived == trainer->numCorrectFramesReceived);
    EXPECT(trainer->numFramesReceived == trainer->numMariosReceived);
    EXPECT(trainer->numFinalFramesReceived == numTotalEpisodes);
  } else {
    EXPECT(trainer->numFramesReceived == 0U);
  }
}

CASE("centraltrainer/partial[.hide]") {
  dist::init();

  auto metrics = std::make_shared<MetricsContext>();
  auto trainer = std::make_shared<PartialCentralTrainer>(
      dist::globalContext()->rank % 4 == 0,
      nullptr,
      nullptr,
      std::make_unique<BaseSampler>());
  trainer->setMetricsContext(metrics);

  std::vector<std::thread> threads;
  int64_t numTotalEpisodes = 0;
  // (total - partialLength) / interval + 1 + 1 from the hanging portion
  int64_t multiplier = (partialLength * 4) / sendInterval + 2;
  for (auto i = 0; i < 2; i++) {
    threads.emplace_back(
        worker,
        trainer,
        10,
        partialLength * 5 + 1,
        partialLength * 5 + sendInterval - 1);
    numTotalEpisodes += 10;
  }
  numTotalEpisodes *= dist::globalContext()->size;

  static int64_t numReceived = 0;
  while (numReceived < numTotalEpisodes * multiplier) {
    trainer->update();
    numReceived = trainer->numBatchesReceived;
    dist::allreduce(&numReceived, 1);
  }
  for (auto& th : threads) {
    EXPECT((th.join(), true));
  }

  EXPECT(numReceived == numTotalEpisodes * multiplier);
  if (trainer->isServer()) {
    EXPECT(trainer->numFramesReceived == trainer->numCorrectFramesReceived);
    EXPECT(trainer->numFramesReceived == trainer->numMariosReceived);
    EXPECT(trainer->numFinalFramesReceived == numTotalEpisodes);
    for (auto l : trainer->batchLengths) {
      EXPECT(l == partialLength);
    }
  } else {
    EXPECT(trainer->numFramesReceived == 0U);
  }
}

CASE("centraltrainer/continuous[.hide]") {
  dist::init();

  auto metrics = std::make_shared<MetricsContext>();
  auto trainer = std::make_shared<ContinuousCentralTrainer>(
      dist::globalContext()->rank % 4 == 0,
      nullptr,
      nullptr,
      std::make_unique<BaseSampler>());
  trainer->setMetricsContext(metrics);

  std::vector<std::thread> threads;
  int64_t numTotalBatches = 0;
  int64_t numEpisodeTails = 0;
  int64_t nThreads = 5;
  for (auto i = 0; i < nThreads; i++) {
    threads.emplace_back(worker, trainer, 10, 30, 30);
    numTotalBatches += 30;
    numEpisodeTails += 19;
  }
  // Sometimes we get edge effects
  numTotalBatches -= nThreads;
  numEpisodeTails -= nThreads;
  numTotalBatches *= dist::globalContext()->size;
  numEpisodeTails *= dist::globalContext()->size;

  int64_t numReceived = 0;
  while (numReceived < numTotalBatches) {
    trainer->update();
    numReceived = trainer->numBatchesReceived;
    dist::allreduce(&numReceived, 1);
  }
  for (auto& th : threads) {
    EXPECT((th.join(), true));
  }

  EXPECT(numReceived >= numTotalBatches);
  if (trainer->isServer()) {
    EXPECT(trainer->numFramesReceived == trainer->numCorrectFramesReceived);
    EXPECT(trainer->numFramesReceived == trainer->numMariosReceived);
    EXPECT(trainer->numFinalFramesReceived >= numEpisodeTails);
    for (auto l : trainer->batchLengths) {
      EXPECT(l == partialLength);
    }
  } else {
    EXPECT(trainer->numFramesReceived == 0U);
  }
}
