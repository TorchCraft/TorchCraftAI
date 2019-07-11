/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "cpid/a2c.h"
#include "cpid/evaluator.h"
#include "cpid/sampler.h"
#include "cpid/sarsa.h"
#include "cpid/trainer.h"

using std::chrono::system_clock;
using namespace torch::optim;

namespace {
class ToyModel : public ag::Container_CRTP<ToyModel> {
 public:
  struct MyException {};
  at::Tensor value;
  bool throws = false;

  void reset() override {
    value = add(torch::zeros({1}), "value");
  }
  ag::Variant forward(ag::Variant inp) override {
    if (throws) {
      throw MyException();
    }
    // Special case for SARSA
    ag::Variant state =
        inp.getDict().count("state") > 0 ? inp.getDict().at("state") : inp;
    if (state.getDict().count(cpid::kPiKey)) {
      return ag::Variant(
          {{cpid::kPiKey, state.getDict().at(cpid::kPiKey).get()},
           {cpid::kValueKey, value}, // For A2C
           {cpid::kQKey, value}, // For SARSA
           {"batch_info",
            ag::VariantDict{{cpid::kPiKey,
                             state.getDict()
                                 .at("batch_info")
                                 .getDict()
                                 .at(cpid::kPiKey)}}}});
    }
    auto batchSize = state.getDict().at("feature").get().size(0);
    return ag::VariantDict{
        {cpid::kPiKey, torch::zeros({batchSize, 2}).softmax(0)},
        {cpid::kQKey, torch::ones({batchSize})},
        {cpid::kValueKey, torch::ones({batchSize}) * value}};
  }
};

template <typename T>
void basicTests(lest::env& lest_env) {
  auto model = ToyModel().make();
  auto trainer = T::createTrainer(model, nullptr);
  auto state = ag::VariantDict{{"feature", torch::ones({1})}};

  for (int i = 0; i < 2 * T::kTrainerBatchSize; ++i) {
    auto episode = trainer->startEpisode();
    EXPECT(trainer->isActive(episode));
    auto out = trainer->forward(state, episode);
    EXPECT(out.isDict());
    out = trainer->sample(out);
    for (int j = 0; j < T::kReturnsLength - 1; ++j) {
      trainer->step(episode, trainer->makeFrame(out, state, 0.0));
    }
    EXPECT(trainer->isActive(episode));
    trainer->step(episode, trainer->makeFrame(out, state, 5.0), true);
    EXPECT(!trainer->isActive(episode));
    if (T::kBlocksWhenCanUpdate) {
      EXPECT_NO_THROW(trainer->update());
    }
  }
  if (!T::kBlocksWhenCanUpdate) {
    trainer->update();
    trainer->update();
  }
  if (T::kLearnsValueFunction) {
    EXPECT(model->value[0].item<float>() > 1.0);
  }
}

template <typename T>
void subbatches(lest::env& lest_env) {
  if (T::kBlocksWhenCanUpdate) {
    return;
  }

  auto model = ToyModel().make();
  auto batcher = std::make_unique<cpid::SubBatchAsyncBatcher>(2, model);
  auto trainer = T::createTrainer(model, std::move(batcher));

  auto addBatch = [&](int size) {
    auto episode = trainer->startEpisode();
    auto state = ag::VariantDict{{"Pi", torch::ones({size, 2}).softmax(1)}};
    auto out = trainer->forward(state, episode);
    out = trainer->sample(out);
    EXPECT(out.getDict().at("Pi").get().sizes() == at::IntList({size, 2}));
    EXPECT(out.getDict().at("V").get().sizes() == at::IntList({1}));
    trainer->step(episode, trainer->makeFrame(out, state, 0.0));
    trainer->step(episode, trainer->makeFrame(out, state, 0.0));
    trainer->step(episode, trainer->makeFrame(out, state, 0.0));
    trainer->step(episode, trainer->makeFrame(out, state, 1.0), true);
  };

  addBatch(1);
  addBatch(5);
  addBatch(10);
  addBatch(3);
  EXPECT_NO_THROW(trainer->update());
  EXPECT_NO_THROW(trainer->update());
  if (T::kLearnsValueFunction) {
    EXPECT(model->value[0].item<float>() > 1.0);
  }
}

template <typename T>
void edgecases(lest::env& lest_env) {
  auto model = ToyModel().make();
  auto state = ag::VariantDict{{"feature", torch::ones({1})}};

  GIVEN("stop episode before any step") {
    auto trainer = T::createTrainer(model, nullptr);
    auto episode = trainer->startEpisode();
    trainer->forceStopEpisode(episode);
  }

  GIVEN("number of episodes running above trainerBatchSize") {
    auto trainer = T::createTrainer(model, nullptr);
    std::vector<cpid::EpisodeHandle> handles;
    for (int i = 0; i < 20; ++i) {
      auto episode = trainer->startEpisode();
      auto out = trainer->forward(state, episode);
      out = trainer->sample(out);
      trainer->step(episode, trainer->makeFrame(out, state, 0.0));
      trainer->step(episode, trainer->makeFrame(out, state, 0.0));
      trainer->step(episode, trainer->makeFrame(out, state, 0.0));
      handles.push_back(std::move(episode));
    }
    for (auto& episode : handles) {
      auto out = trainer->forward(state, episode);
      out = trainer->sample(out);
      trainer->step(episode, trainer->makeFrame(out, state, 0.0), true);
    }
    EXPECT_NO_THROW(handles.clear());
    EXPECT_NO_THROW(trainer->update());
  }

  GIVEN("reuse finished episode") {
    auto trainer = T::createTrainer(model, nullptr);
    auto episode = trainer->startEpisode();
    auto out = trainer->forward(state, episode);
    out = trainer->sample(out);
    trainer->step(episode, trainer->makeFrame(out, state, 0.0), true);

    auto episode2 = trainer->startEpisode();
    trainer->step(episode2, trainer->makeFrame(out, state, 0.0));

    // Should ignore the call
    trainer->step(episode, trainer->makeFrame(out, state, 0.0), true);
    EXPECT(true);
  }

  GIVEN("throwing model during rollout") {
    auto trainer = T::createTrainer(model, nullptr);
    model->throws = true;
    auto episode = trainer->startEpisode();
    EXPECT_THROWS_AS(trainer->forward(state, episode), ToyModel::MyException);
    model->throws = false;
  }

  GIVEN("throwing model during update") {
    auto trainer = T::createTrainer(model, nullptr);
    auto episode = trainer->startEpisode();
    auto out = trainer->forward(state, episode);
    out = trainer->sample(out);
    for (int j = 0; j < T::kReturnsLength - 1; ++j) {
      trainer->step(episode, trainer->makeFrame(out, state, 0.0));
    }
    trainer->step(episode, trainer->makeFrame(out, state, 5.0), true);
    model->throws = true;
    EXPECT_THROWS_AS(trainer->update(), ToyModel::MyException);
    model->throws = false;
  }
}

template <typename T>
void multithreaded(lest::env& lest_env) {
  auto model = ToyModel().make();
  auto state = ag::VariantDict{{"feature", torch::ones({1})}};
  constexpr int kNumThreads = 20;
  std::vector<std::thread> threads;
  std::atomic<bool> finished;
  auto trainerTrain = T::createTrainer(model, nullptr);
  auto runThread = [&](int threadId, std::shared_ptr<cpid::Trainer> trainer) {
    torch::NoGradGuard g_;
    while (!finished.load()) {
      auto episode = trainer->startEpisode();
      if (!episode) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }
      auto out = trainer->forward(state, episode);
      if (!trainer->isActive(episode)) {
        continue;
      }
      out = trainer->sample(out);
      trainer->step(episode, trainer->makeFrame(out, state, 0.0));
      trainer->step(episode, trainer->makeFrame(out, state, 0.0));
      if (threadId % 2) {
        // Make half of the workers forceStopEpisode
        continue;
      }
      trainer->step(episode, trainer->makeFrame(out, state, 1.0), true);
    }
  };
  auto startWorkers = [&](std::shared_ptr<cpid::Trainer> trainer) {
    finished.store(false);
    for (std::size_t i = 0; i < kNumThreads; i++) {
      threads.emplace_back(runThread, i, trainer);
    }
  };
  auto stopWorkers = [&](std::shared_ptr<cpid::Trainer> trainer) {
    finished.store(true);
    trainer->reset();
    for (std::size_t i = 0; i < kNumThreads; i++) {
      threads[i].join();
    }
    threads.clear();
  };

  for (int i = 0; i < 10; ++i) {
    startWorkers(trainerTrain);
    for (int j = 0; j < 5; ++j) {
      EXPECT_NO_THROW(trainerTrain->update());
    }
    EXPECT_NO_THROW(stopWorkers(trainerTrain));

    auto trainerEval = trainerTrain->makeEvaluator(
        100, std::make_unique<cpid::MultinomialSampler>());
    startWorkers(trainerEval);
    while (!trainerEval->update()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    EXPECT_NO_THROW(stopWorkers(trainerEval));
  }
  EXPECT(true);
}

template <typename T>
void runAllTrainerTests(lest::env& lest_env) {
  basicTests<T>(lest_env);
  subbatches<T>(lest_env);
  edgecases<T>(lest_env);
  multithreaded<T>(lest_env);
}

struct TestDefault {
  static constexpr bool kBlocksWhenCanUpdate = false;
  static constexpr bool kLearnsValueFunction = false;

  static constexpr int kReturnsLength = 4;
  static constexpr int kTrainerBatchSize = 2;
};

struct TestA2C : public TestDefault {
  static constexpr bool kLearnsValueFunction = true;

  static std::shared_ptr<cpid::A2C> createTrainer(
      ag::Container model,
      std::unique_ptr<cpid::AsyncBatcher> batcher = nullptr) {
    if (!batcher) {
      batcher = std::make_unique<cpid::AsyncBatcher>(model, 2);
    }
    auto optimizer =
        std::make_shared<SGD>(model->parameters(), SGDOptions(1.0f));
    auto trainer = std::make_shared<cpid::A2C>(
        model,
        optimizer,
        std::make_unique<cpid::MultinomialSampler>(),
        std::move(batcher),
        /* returnsLength*/ kReturnsLength,
        /* updateFreq*/ 1,
        /* trainerBatchSize*/ kTrainerBatchSize,
        /* discount */ 0.99,
        /* ratio_clamp*/ 10,
        /* entropy_ratio*/ 0.01,
        /* policy_ratio*/ 1,
        /* overlappingUpdates*/ true,
        /* gpuMemoryEfficient*/ true,
        /* reduceGradients*/ true,
        /* maxGradientNorm*/ -1);
    trainer->setMetricsContext(std::make_shared<cpid::MetricsContext>());
    return trainer;
  }
};

struct TestSARSA : public TestDefault {
  static constexpr bool kBlocksWhenCanUpdate = true; // Because we are on-policy
  static std::shared_ptr<cpid::Sarsa> createTrainer(
      ag::Container model,
      std::unique_ptr<cpid::AsyncBatcher> batcher = nullptr) {
    if (!batcher) {
      batcher = std::make_unique<cpid::AsyncBatcher>(model, 2);
    }
    auto optimizer =
        std::make_shared<SGD>(model->parameters(), SGDOptions(1.0f));
    auto trainer = std::make_shared<cpid::Sarsa>(
        model,
        optimizer,
        std::make_unique<cpid::MultinomialSampler>(),
        std::move(batcher),
        /* returnsLength*/ kReturnsLength,
        /* trainerBatchSize*/ kTrainerBatchSize,
        /* discount */ 0.99,
        /* gpuMemoryEfficient*/ true);
    trainer->setMetricsContext(std::make_shared<cpid::MetricsContext>());
    return trainer;
  }
};

} // namespace

#define TEST_TRAINER_IMPL(name, T)   \
  SCENARIO("trainerimpl/" name) {    \
    runAllTrainerTests<T>(lest_env); \
  }

TEST_TRAINER_IMPL("a2c", TestA2C);
TEST_TRAINER_IMPL("sarsa", TestSARSA);
