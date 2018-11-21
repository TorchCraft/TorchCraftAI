---
id: module-training
title : Module Training Blueprints
---

This page details how to use TorchCraftAI's training library [cpid](https://github.com/TorchCraft/TorchCraftAI/tree/master/cpid) to setup a basic reinforcement learning training loop.
We only discuss the main concepts here; see the [Building Placement RL Tutorial](bptut-rl.md) for a concrete use-case.

## Main Program

The code below is a basic skeleton for a training program.
The main ingredients:
- Create a model (`ag::Container`) and a trainer (`cpid::Trainer`) instance
- Launch a number of game threads, each continuously gathering experience by playing games
- In the main thread, periodically call `trainer->update()`.
  It returns `true` if the model has been updated, which will give you chance to monitor and log training progress.
- `cpid::Trainer::isDone()` is used to stop the game threads

```cpp
using namespace cherrypi;
namespace dist = cpid::distributed;

void runGameThread(std::shared_ptr<cpid::Trainer> trainer) {
  // ...
}

void runTraining(std::shared_ptr<cpid::Trainer> trainer) {
  // Launch simulation threads
  std::vector<std::thread> gameThreads;
  for (int i = 0; i < FLAGS_num_game_threads; i++) {
    gameThreads.emplace_back(runGameThread, trainer);
  }

  // Main thread runs trainer->update() and can do logging, plotting, ...
  while (true) {
    auto updated = trainer->update();
    if (!updated) {
      // update() is non-blocking and returns immediately if a model update
      // cannot be performed at this time. Avoid busy-waiting by sleeping for a
      // a little while.
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }

    // Monitor progress, checkpointing, ...

    // Check end conditions of training
    bool stop = ...;
    if (stop) {
      // Wake up game threads blocked in a trainer function and tell them to
      // stop.
      trainer->reset();
      trainer->setDone(true);
      break;
    }
  }

  // Wait for game threads to finish
  for (auto& thread : gameThreads) {
    thread.join();
  }
}

int main(int argc, char** argv) {
  // Initial setup, including cherrypi::init() and dist::init()

  auto model = MyFancyModel.make();
  auto trainer = std::make_shared<cpid::MyFavoriteTrainer>(model, ...);

  // Additional synchronization across workers (e.g. broadcast model
  // parameters)

  runTraining(trainer);

  return 0;
}
```

### Game Threads

Each game thread continuously gathers experience by playing games and records them using the trainer instance.
Most of the time, they will look a lot like this:

```cpp
void runGameThread(std::shared_ptr<cpid::Trainer> trainer) {
  // Here, a custom subclass of cherrypi::ScenarioProvider is used to set up the games.
  auto provider = std::make_unique<MyScenarioProvider>(...);

  while (!trainer->isDone()) {
    try {
      // Setup the game
      provider->cleanScenario();
      std::shared_ptr<BasePlayer> player1, player2;
      std::tie(player1, player2) = provider->spawnNextScenario(setupFn, setupFn);

      // Any additional setup, e.g. passing trainer or module pointers to a
      // custom module

      // Inform players about current game ID
      player1->state()->board()->post(Blackboard::kGameUidKey, gameId);
      player2->state()->board()->post(Blackboard::kGameUidKey, gameId);

      // Run the actual game
      while (true) {
        if (provider->isFinished(player->state()->currentFrame()) {
          break;
        }
        if (player1->state()->gameEnded() && player2->state()->gameEnded()) {
          break;
        }
        player1->step();
        player2->step();
      }

      // Post-game cleanup
    } catch (std::exception const& ex) {
      // Error during simulation
    }
  }
}
```

## Models and Modules

We'll need two things now -- a **model** (that implements a trainable policy, for example) and a **module** which uses it and works within the general bot framework.
The model is an [autograd container](https://github.com/TorchCraft/TorchCraftAI/blob/master/3rdparty/include/autogradpp/autograd.h).
The class definition will, in essence, look like this:

```cpp
AUTOGRAD_CONTAINER_CLASS(MyFancyModel) {
 public:
  // Example for declaring additional model parameters (e.g. the number of layers)
  TORCH_ARG(int, num_layers) = 2;

  // Initializes any child containers 
  void reset() override;

  // Computes the model outputs from the given inputs
  ag::Variant forward(ag::Variant input) override;
};
```

We'll also need a TorchCraftAI module that actually plays with the model, i.e. which connects it to the [UPC communication system](core-abstractions.html#upctuples).
The module setup depends heavily on the specific use-case: for example, one could re-use existing rules but use the model on select occasions to make a decision (e.g. Strategy, BuildingPlacer) or implement a low-level controller (SquadCombat behaviors, Builder).
For a controller-based setup one could implement a custom [controller](core-abstractions.html#controllers) that performs an action using the model every couple of steps, and have the module take care of selecting UPCs from the blackboard and spawning the controller instances if necessary.

For training purposes, module code will not call `MyFancyModel::forward()` directly but use the `forward()` function of `cpid::Trainer` instead.
This way, the trainer can avoid data races by using the model during parameter updates, batch updates from multiple game threads ([`cpid::AsyncBatcher`](https://github.com/TorchCraft/TorchCraftAI/blob/master/cpid/batcher.h)), sample actions from the model output in a generic manner (e.g. [`cpid::MultinomialSampler`](https://github.com/TorchCraft/TorchCraftAI/blob/master/cpid/sampler.h)) or dispatch the forward call to per-episode versions of the model as in [`cpid::ESTrainer`](https://github.com/TorchCraft/TorchCraftAI/blob/master/cpid/estrainer.h).

## Collecting Experience

In cpid, experiences are collected by submitting replay buffer frames via [`cpid::Trainer::step()`](https://github.com/TorchCraft/TorchCraftAI/blob/master/cpid/trainer.h).
Each episode is associated with a **game** ID and an optional **episode** ID and initially registered in the trainer with `cpid::Trainer::startEpisode()`.
If anything goes wrong during training and the episode cannot be finished, it's `cpid::Trainer::forceStopEpisode()` needs to be called to discard it.

Let's look a few different scenarios and how to best submit the collected experience to the trainer instance.
Note that the examples below only provides a brief illustration of the functionality and need to be further extended for a functional training.

### Stand-alone Model

This is a simplified setup, e.g. for micro-management scenarios.
Here, the module is merely a wrapper around the model's `forward()` function, without or with only minimal interaction with the rest of the bot.
A skeleton class of the TorchCraftAI module can look like this:

```cpp
class MyModule1 : public cherrypi::Module {
 public:
  // The training loop sets the current trainer instance during initialization
  // of a game
  void setTrainer(std::shared_ptr<cpid::Trainer> trainer) {
    trainer_ = trainer;
  }

  void onGameStart(State* state) override {
    // Generate new game ID for this episode
    auto gameId = cpid::genGameUID(dist::globalContext()->rank);

    // Register this game as a new episode in the trainer.
    // startEpisode() will return true if we're good to go.
    while (!trainer->startEpisode(gameId)) {
      if (trainer_->isDone()) {
        throw std::runtime_error(fmt::format("{} trainer is done", gameId));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    // Store the current episode information in an EpisodeHandle. If the handle
    // goes out of scope and the episode is not finished yet, the destructor
    // will call `trainer->forceStopEpisode()`.
    episode_ = cpid::EpisodeHandle(trainer, gameId);
  }

  void step(State* state) override {
    // Stop the game if told to do so
    if (trainer->isDone() ||
        !trainer->isActive(episode_.gameId, episode_.episodeKey)) {
      throw std::runtime_error(
          fmt::format("{} trainer/episode is done", episode_.gameId));
    }

    // Featurize state and call forward()
    auto state = featurizeStateForMyFancyModel(state);
    auto output = trainer->forward(state_, episode_.gameId);

    // Submit experience to the trainer. The reward we observe will be
    // associated to the *previous* state and output by the trainer.
    auto reward = observedReward(state);
    auto frame = trainer->makeFrame(output, state, reward);
    trainer->step(episode_.gameId, std::move(frame));

    // The forward() will sample an action from the model output if the trainer
    // has been constructed with an appropriate sampling class. Now we'll have
    // translate this action to a game action, e.g. by constructing a UPC and
    // posting it to the blackboard.
    auto upc = std::make_shared<UPCTuple>();
    // Fill in upc contents...

    // Since we did not require an upstream UPC to perform our actions, we'll
    // specify the omni-present "root" UPC as a parent when posting.
    state->board()->postUPC(std::move(upc), kRootUpcId, this);
  }

  void onGameEnd(State* state) override {
    // Submit final frame of the episode
    auto reward = observedReward(state);
    auto frame = trainer->makeFrame({}, {}, reward);
    trainer->step(episode_.gameId, std::move(frame), true);
  }

 private:
  std::shared_ptr<cpid::Trainer> trainer_;
  cpid::EpisodeHandle episode_;
};
```

### Low-level Controller Model

This setup is an extension of the previous one and tailored towards tighter integration with existing modules.
In the above example, we did not require an upstream UPC to initiate an action.
This is fine for micro-scenarios, but in a full game, higher-level modules will request specific actions at different points in the game; additionally, multiple actions may be requested in parallel.
Below, we will consider individual episodes for each UPC that should be performed with our low-level model.
The module scans the blackboard for relevant UPCs, and, if it finds a candidate, will instantiate a [Controller](core-abstractions.md#controllers) to perform the requested action.
The episode is then tied to the life-time of the controller.
The logic remains pretty similar, however instead of starting and finishing episodes in `onGameStart()` and `onGameEnd()` we'll use the controller's constructor and destructor.

```cpp
class MyController : public cherrypi::Controller {
 public:
  MyController(
      Unit* unit,
      std::shared_ptr<cpid::Trainer> trainer,
      cpid::EpisodeTuple episodeId)
      : unit_(unit), trainer_(std::move(trainer)) {
    // Register this game as a new episode in the trainer.
    // startEpisode() will return true if we're good to go.
    while (!trainer->startEpisode(episodeId.gameID, episodeId.episodeKey)) {
      if (trainer_->isDone()) {
        throw std::runtime_error(fmt::format("{} trainer is done", gameId));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    episode_ =
        cpid::EpisodeHandle(trainer, episodeId.gameID, episodeId.episodeKey);
  }

  virtual ~MyController() {
    // Submit final frame of the episode
    auto reward = observedReward(state);
    auto frame = trainer->makeFrame({}, {}, reward);
    trainer->step(episode_.gameId, std::move(frame), true);
  }

  // This is similar to MyModule1::step() in the previous example
  void step(State* state) override {
    // Stop the game if told to do so
    if (trainer->isDone() ||
        !trainer->isActive(episode_.gameId, episode_.episodeKey)) {
      throw std::runtime_error(
          fmt::format("{} trainer/episode is done", episode_.gameId));
    }

    // Featurize state and call forward()
    auto state = featurizeStateForMyFancyModel(state);
    auto output =
        trainer->forward(state_, episode_.gameId, episode_.episodeKey);

    // Submit experience to the trainer. The reward we observe will be
    // associated to the *previous* state and output by the trainer.
    auto reward = observedReward(state);
    auto frame = trainer->makeFrame(output, state, reward);
    trainer->step(episode_.gameId, std::move(frame));

    // Translate output to an appropriate UPC
    Controller::addUpc(unit_, ...);
    Controller::postUpcs(state);
  }

 private:
  Unit* unit_;
  std::shared_ptr<cpid::Trainer> trainer_;
  cpid::EpisodeHandle episode_;
};

class MyModule2 : public cherrypi::Module {
 public:
  // The training loop sets the current trainer instance during initialization
  // of a game
  void setTrainer(std::shared_ptr<cpid::Trainer> trainer) {
    trainer_ = trainer;
  }

  void onGameStart(State* state) {
    gameId_ = cpid::genGameUID(dist::globalContext()->rank);
  }

  void step(State* state) override {
    // For the current relevant UPCs on the Blackboard
    for (auto& it : relevantUpcs()) {
      auto upcId = it.first;
      auto& upc = it.second;
      state->board()->consumeUPC(upcId, this);

      // Select a single from upc.unit
      auto unit = selectUnit(upc);

      // Create a new task with a new controller instance
      auto controller =
          std::make_shared<MyController>(unit, trainer, {gameId_, upcId});
      auto task = std::make_shared<ControllerTask>(
          upcId, std::unordered_set<Unit*>{unit}, std::move(controller));
      state->board()->postTask(std::move(task), this, true);
    }

    // Update active controllers
    for (auto& task : state->board()->tasksOfModule(this)) {
      auto ctask = std::static_pointer_cast<ControllerTask>(task);
      auto controller = ctask->controller();
      controller->step(state);
    }
  }

 private:
  std::shared_ptr<cpid::Trainer> trainer_;
  cpid::GameUID gameId_;
};
```

### Higher-Level Models

For learning functionality that is not tied to direct unit control, we can broadly consider two approaches.
In the first case, the model runs in regular intervals alongside the bot and informs a rule-based model, e.g. for triggering a high-level action. 
The setup is similar to the stand-alone model then.
Another case is the tight integration with the UPC communication system, similar to the [building placement tutorial](bptut-rl.md).
Here, upstream modules trigger model actions which are then translated to macro-actions which might be durative.

Let's discuss the second case in more detail.
The amount of book-keeping that is required depends on the rewards we want to provide to the trainer:
- A end-of-game reward only (e.g. 1 for win, -1 for lose).
  In this case, we can submit every action taken with the model as a frame to the trainer and provide the final end-of-game frame with the reward in `onGameEnd()`.
- Detailed tracking of the outcome of every action.
  This can be done with [`ProxyTask`](https://github.com/TorchCraft/TorchCraftAI/blob/master/src/task.h) instances for each action or (if required) additional manual tracking in the module.

In the building placement use-case, macro-actions can be interleaved (since several buildings can be constructed at once) and are of different durations.
As a consequence, the action outcomes will be observed out-of-order with respect to the actions taken.
One solution to this is to submit the sequence of frames to the trainer instance only at the **end** of the game when all macro-actions are terminated.

Here's a short example of a higher-level module that uses tasks to track the outcome of actions and constructs all episode frames at the end of the game.
Model inputs and outputs are attached to each UPC that is being posted to avoid additional manual book-keeping.
There'll be a final end-of-game reward and smaller rewards for individual actions based on success or failure of the macro-action.

```cpp
struct MyPostData : public UpcPostData {
  MyPostData(ag::Variant state, ag::Variant output)
      : state(state), output(output) {}
  virtual ~MyPostData() = default;

  ag::Variant state;
  ag::Variant output;
  TaskStatus finalStatus = TaskStatus::Unknown;
};

class MyProxyTask : public cherrypi::ProxyTask {
 public:
  using ProxyTask::ProxyTask;

  void update(State* state) override {
    ProxyTasks::update(state);
    postData(state)->finalStatus = Task::status();
  }

  void cancel(State* state) override {
    ProxyTasks::cancel(state);
    postData(state)->finalStatus = TaskStatus::Cancelled;
  }

 private:
  MyPostData* postData(State* state) const {
    auto* storage = state->board()->upcStorage();
    auto const* post = storage->post(Task::targetUpcId_);
    return std::static_pointer_cast<MyPostData>(post->data).get();
  }
};

class MyModule3 : public cherrypi::Module {
 public:
  void setTrainer(std::shared_ptr<cpid::Trainer> trainer) {
    trainer_ = trainer;
  }

  // This is identical to MyModule1::onGameStart()
  void onGameStart(State* state) override {
    auto gameId = cpid::genGameUID(dist::globalContext()->rank);
    while (!trainer->startEpisode(gameId)) {
      if (trainer_->isDone()) {
        throw std::runtime_error(fmt::format("{} trainer is done", gameId));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    episode_ = cpid::EpisodeHandle(trainer, gameId);

    // We need persistent UPC storage to retain any data we post alongside the
    // actual UPCs for the duration of the game.
    state->board()->storage()->setPersistent(true);
  }

  void step(State* state) override {
    // Stop the game if told to do so
    if (trainer->isDone() ||
        !trainer->isActive(episode_.gameId, episode_.episodeKey)) {
      throw std::runtime_error(
          fmt::format("{} trainer/episode is done", episode_.gameId));
    }

    for (auto& it : relevantUpcs()) {
      auto upcId = it.first;
      auto& upc = it.second;

      // Take action with model
      state_ = featurizeStateForMyFancyModel(state, upc);
      output_ = trainer->forward(state_, episode_.gameId);

      // Generate UPC for macro-action
      auto upc = std::make_shared<UPCTuple>();
      // Fill in upc contents...

      // Attach data to this UPC so that we can recover states, outputs and
      // results of all actions at the end of the game.
      auto data = std::make_shared<MyPostData>(state, output);

      auto targetUpcId =
          state->board()->postUpc(std::move(upc), upcId, this, std::move(data));
      if (targetUpcId >= 0) {
        state->board()->consumeUPC(upcId, this);
        auto task = std::make_shared<MyProxyTask>(targetUpcId, upcId);
        board->postTask(std::move(task), this, true);
      }
    }
  }

  void onGameEnd(State* state) override {
    // Iterate over all UPCs that we posted
    float reward = 0.0f;
    int frames = 0;
    auto* storage = state->board()->upcStorage();
    for (auto const* post : storage->upcPostsFrom(this)) {
      auto* data = std::static_pointer_cast<MyPostData>(post->data).get();
      if (data->finalStatus != TaskData::Success &&
          data->finalStatus != TaskData::Failure) {
        // Task was cancelled or never picked up -- ignore this action
        continue;
      }

      auto frame = trainer->makeFrame(data->output, data->state, reward);
      trainer->step(episode_.gameId, std::move(frame));
      frames++;

      // The local reward for this action will end up in the *next* frame
      reward = data->finalStatus == TaskData::Success ? 0.2f : -0.2f;
    }
    if (frames == 0) {
      // Empty episode
      return;
    }

    // Global reward
    reward += state->won() ? 1.0f : -1.0f;
    auto frame = trainer->makeFrame({}, {}, reward);
    trainer->step(episode_.gameId, std::move(frame), true);
  }

 private:
  std::shared_ptr<Trainer> trainer_;
  cpid::EpisodeHandle episode_;
};
```
