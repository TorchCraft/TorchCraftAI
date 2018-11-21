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

struct MyReplayBufferFrame : CerealizableReplayBufferFrame {
  // Note: this struct has to be registered (at global scope) with
  // CEREAL_REGISTER_TYPE(MyReplayBufferFrame)
  std::string s;
  torch::Tensor t;
  uint32_t i;
  std::vector<float> fs;

  MyReplayBufferFrame() {
    s = kMarioString;
    t = torch::rand({10, 10, 10});
    i = common::Rand::rand() % std::numeric_limits<uint32_t>::max();
    fs.resize((common::Rand::rand() % 20) + 1);
  }
  virtual ~MyReplayBufferFrame() = default;

  template <class Archive>
  void serialize(Archive& ar) {
    ar(cereal::base_class<CerealizableReplayBufferFrame>(this), s, t, i, fs);
  }
};

class MyCentralTrainer : public CentralTrainer {
 public:
  using CentralTrainer::CentralTrainer;

  size_t numEpisodesReceived = 0;
  size_t numFramesReceived = 0;
  size_t numCorrectFramesReceived = 0;
  size_t numMariosReceived = 0;

 protected:
  void receivedEpisode(GameUID const& gameId, EpisodeKey const& episodeKey)
      override {
    numEpisodesReceived++;

    // Verify episode type
    auto& episode = replayer_.get(gameId, episodeKey);
    for (auto const& frame : episode) {
      numFramesReceived++;
      auto f = std::dynamic_pointer_cast<MyReplayBufferFrame>(frame);
      if (f != nullptr) {
        numCorrectFramesReceived++;
        if (f->s == kMarioString) {
          numMariosReceived++;
        }
      }
    }
  }
};

void worker(std::shared_ptr<Trainer> trainer, int numEpisodes) {
  std::uniform_int_distribution<int> eplen(1, 100);
  for (auto i = 0; i < numEpisodes; i++) {
    auto gameId = genGameUID(dist::globalContext()->rank);
    while (!trainer->startEpisode(gameId)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    auto episode = cpid::EpisodeHandle(trainer, gameId);

    auto len = common::Rand::sample(eplen);
    for (auto j = 0; j < len; j++) {
      auto frame = std::make_shared<MyReplayBufferFrame>();
      trainer->step(episode.gameID, frame);
    }
    auto frame = std::make_shared<MyReplayBufferFrame>();
    trainer->step(episode.gameID, frame, true);

    std::this_thread::sleep_for(
        std::chrono::milliseconds(common::Rand::rand() % 50));
  }
}

} // namespace

CEREAL_REGISTER_TYPE(MyReplayBufferFrame)

// Feel free to run this test with `mpirun -np 8` or sth for testing.
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
    threads.emplace_back(worker, trainer, 10);
    numTotalEpisodes += 10;
  }
  numTotalEpisodes *= dist::globalContext()->size;

  static int64_t numReceived = 0;
  while (numReceived < numTotalEpisodes) {
    trainer->update();
    numReceived = trainer->numEpisodesReceived;
    dist::allreduce(&numReceived, 1);
  }
  for (auto& th : threads) {
    EXPECT((th.join(), true));
  }

  EXPECT(numReceived == numTotalEpisodes);
  if (trainer->isServer()) {
    EXPECT(trainer->numFramesReceived == trainer->numCorrectFramesReceived);
    EXPECT(trainer->numFramesReceived == trainer->numMariosReceived);
  } else {
    EXPECT(trainer->numFramesReceived == 0U);
  }
}
