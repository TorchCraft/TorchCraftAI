/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "trainer.h"
#include "batcher.h"
#include "common/rand.h"
#include "evaluator.h"
#include "sampler.h"
#include <fmt/format.h>
#include <random>
#include <unistd.h>

#ifndef HOST_NAME_MAX
#if defined(_POSIX_HOST_NAME_MAX)
#define HOST_NAME_MAX _POSIX_HOST_NAME_MAX
#elif defined(MAXHOSTNAMELEN)
#define HOST_NAME_MAX MAXHOSTNAMELEN
#endif
#endif /* HOST_NAME_MAX */

using Episode = cpid::ReplayBuffer::Episode;
using Store = cpid::ReplayBuffer::Store;

namespace cpid {

namespace {
std::atomic<uint64_t> atomicID_;
std::string getRandomPrefix() {
  static std::string s = "";
  static std::once_flag once;
  std::call_once(once, []() { s = common::randId(5); });

  return s;
}
} // namespace

GameUID genGameUID() {
  uint64_t id = atomicID_++;
  char hostname[HOST_NAME_MAX];
  gethostname(hostname, HOST_NAME_MAX);

  return fmt::format("{}-{}-{}", hostname, getRandomPrefix(), id);
}

Episode& ReplayBuffer::append(
    GameUID uid,
    EpisodeKey key,
    std::shared_ptr<ReplayBufferFrame> value,
    bool isDone) {
  std::lock_guard<std::shared_timed_mutex> lock(replayerRWMutex_);
  auto done =
      (dones_.find(uid) != dones_.end() &&
       dones_[uid].find(key) != dones_[uid].end());
  if (done) {
    VLOG(0) << "Error: Trying to insert frame into finished episode";
  }

  auto& map = *(
      storage_.insert({uid, std::unordered_map<EpisodeKey, Episode>()}).first);
  auto& vec = *(map.second.insert({key, Episode()}).first);
  vec.second.push_back(std::move(value));

  if (isDone) {
    auto doneset = dones_.insert({uid, std::unordered_set<EpisodeKey>()}).first;
    doneset->second.insert(key);
  }
  return vec.second;
}

std::size_t ReplayBuffer::size() const {
  std::shared_lock<std::shared_timed_mutex> lock(replayerRWMutex_);
  std::size_t n = 0;
  for (auto const& it : storage_) {
    n += it.second.size();
  }
  return n;
}

std::size_t ReplayBuffer::size(GameUID const& id) const {
  std::shared_lock<std::shared_timed_mutex> lock(replayerRWMutex_);
  return storage_.at(id).size();
}

std::size_t ReplayBuffer::sizeDone() const {
  std::shared_lock<std::shared_timed_mutex> lock(replayerRWMutex_);
  std::size_t n = 0;
  for (auto const& it : dones_) {
    n += it.second.size();
  }
  return n;
}

std::size_t ReplayBuffer::sizeDone(GameUID const& id) const {
  std::shared_lock<std::shared_timed_mutex> lock(replayerRWMutex_);
  return dones_.at(id).size();
}

Episode& ReplayBuffer::get(GameUID const& uid, EpisodeKey const& key) {
  std::shared_lock<std::shared_timed_mutex> lock(replayerRWMutex_);
  return storage_[uid][key];
}

bool ReplayBuffer::has(GameUID const& uid, EpisodeKey const& key) {
  std::shared_lock<std::shared_timed_mutex> lock(replayerRWMutex_);
  auto const& it = storage_.find(uid);
  if (it == storage_.end()) {
    return false;
  }
  if (it->second.find(key) == it->second.end()) {
    return false;
  }
  return true;
}

bool ReplayBuffer::isDone(GameUID const& uid, EpisodeKey const& key) {
  std::shared_lock<std::shared_timed_mutex> lock(replayerRWMutex_);
  auto const& it = dones_.find(uid);
  if (it == dones_.end()) {
    return false;
  }
  if (it->second.find(key) == it->second.end()) {
    return false;
  }
  return true;
}

void ReplayBuffer::erase(GameUID const& id, EpisodeKey const& key) {
  std::lock_guard<std::shared_timed_mutex> lock(replayerRWMutex_);
  storage_[id].erase(key);
  dones_[id].erase(key);
  if (dones_[id].size() == 0) {
    dones_.erase(id);
  }
  if (storage_[id].size() == 0) {
    storage_.erase(id);
  }
}

void ReplayBuffer::clear() {
  std::lock_guard<std::shared_timed_mutex> lock(replayerRWMutex_);
  dones_.clear();
  storage_.clear();
}

std::vector<std::pair<EpisodeTuple, std::reference_wrapper<Episode>>>
ReplayBuffer::getAllEpisodes() {
  std::lock_guard<std::shared_timed_mutex> lock(replayerRWMutex_);
  std::vector<std::pair<EpisodeTuple, std::reference_wrapper<Episode>>>
      episodes;
  for (auto& game : dones_) {
    for (auto& ep : game.second) {
      episodes.push_back(std::make_pair(
          EpisodeTuple{game.first, ep}, std::ref(storage_[game.first][ep])));
    }
  }
  return episodes;
}

std::vector<std::pair<EpisodeTuple, std::reference_wrapper<Episode>>>
ReplayBuffer::sample(uint32_t num) {
  auto engine = common::Rand::makeRandEngine<std::mt19937>();
  return sample(engine, num);
}

Trainer::Trainer(
    ag::Container model,
    ag::Optimizer optim,
    std::unique_ptr<BaseSampler> sampler,
    std::unique_ptr<AsyncBatcher> batcher)
    : model_(model),
      optim_(optim),
      sampler_(std::move(sampler)),
      batcher_(std::move(batcher)) {
  if (optim_) {
    optim_->zero_grad();
  }
  epGuard_ = std::make_shared<HandleGuard>();
};

ag::Variant Trainer::forward(ag::Variant inp, EpisodeHandle const& handle) {
  ag::Variant out;
  if (batcher_) {
    out = batcher_->batchedForward(inp);
  } else {
    torch::NoGradGuard g_;
    out = model_->forward(inp);
  }
  return out;
}

ag::Variant Trainer::forwardUnbatched(ag::Variant in, ag::Container model) {
  if (!model) {
    model = model_;
  }
  if (batcher_) {
    auto out = model_->forward(batcher_->makeBatch({in}));
    return batcher_->unBatch(out, false, -1)[0];
  }
  return model_->forward(in);
}

void Trainer::step(
    EpisodeHandle const& handle,
    std::shared_ptr<ReplayBufferFrame> v,
    bool isDone) {
  std::lock_guard<std::shared_timed_mutex> lock(activeMapMutex_);
  auto& uid = handle.gameID();
  auto& k = handle.episodeKey();
  if (!(actives_.find(uid) != actives_.end() &&
        actives_[uid].find(k) != actives_[uid].end())) {
    VLOG(3) << fmt::format("({},{}) is not active!\n", uid, k);
    return;
  }
  auto& episode = replayer_.append(uid, k, v, isDone);
  stepFrame(uid, k, episode);
  if (isDone) {
    {
      actives_[uid].erase(k);
      if (actives_[uid].size() == 0) {
        actives_.erase(uid);
      }
    }
    stepEpisode(uid, k, episode);
  }
};

ag::Container Trainer::model() const {
  return model_;
}

ag::Optimizer Trainer::optim() const {
  return optim_;
}

ReplayBuffer& Trainer::replayBuffer() {
  return replayer_;
}

std::shared_ptr<Evaluator> Trainer::evaluatorFactory(
    ag::Container model,
    std::unique_ptr<BaseSampler> s,
    size_t n,
    ForwardFunction func) {
  return std::make_shared<Evaluator::make_shared_enabler>(
      model, std::move(s), n, func);
}

std::shared_ptr<Evaluator> Trainer::makeEvaluator(
    size_t,
    std::unique_ptr<BaseSampler> sampler) {
  throw std::runtime_error("Trainer does not support evaluation");
}

void Trainer::setTrain(bool train) {
  train_ = train;
  if (train) {
    model_->train();
  } else {
    model_->eval();
  }
}

void Trainer::setDone(bool done) {
  done_.store(done);
}

Trainer::EpisodeHandle Trainer::startEpisode() {
  std::lock_guard<std::shared_timed_mutex> lock(activeMapMutex_);
  auto uid = genGameUID();
  actives_[uid].insert(kDefaultEpisodeKey);
  return EpisodeHandle(this, uid, kDefaultEpisodeKey);
}

void Trainer::forceStopEpisode(EpisodeHandle const& handle) {
  std::lock_guard<std::shared_timed_mutex> lock(activeMapMutex_);
  auto& uid = handle.gameID();
  auto& k = handle.episodeKey();
  if (actives_.find(uid) != actives_.end() &&
      actives_[uid].find(k) != actives_[uid].end()) {
    replayer_.erase(uid, k);
    actives_[uid].erase(k);
    if (actives_[uid].size() == 0) {
      actives_.erase(uid);
    }
  }
}

bool Trainer::isActive(EpisodeHandle const& handle) {
  std::shared_lock<std::shared_timed_mutex> lock(activeMapMutex_);
  auto& uid = handle.gameID();
  auto& k = handle.episodeKey();
  return actives_.find(uid) != actives_.end() &&
      actives_[uid].find(k) != actives_[uid].end();
}

void Trainer::reset() {
  auto forceStopEpisodeWithoutLock =
      [this](GameUID const& uid, EpisodeKey const& k) {
        if (actives_.find(uid) != actives_.end() &&
            actives_[uid].find(k) != actives_[uid].end()) {
          replayer_.erase(uid, k);
          actives_[uid].erase(k);
          if (actives_[uid].size() == 0) {
            actives_.erase(uid);
          }
        }
      };
  std::lock_guard<std::shared_timed_mutex> lock(activeMapMutex_);
  ReplayBuffer::UIDKeyStore activesCopy(actives_);
  for (auto const& iter : activesCopy) {
    for (auto const& k : iter.second) {
      forceStopEpisodeWithoutLock(iter.first, k);
    }
  }
  actives_.clear();
}

ag::Variant Trainer::sample(ag::Variant in) {
  return sampler_->sample(std::move(in));
}

void Trainer::setBatcher(std::unique_ptr<AsyncBatcher> batcher) {
  batcher_ = std::move(batcher);
}

EpisodeHandle::EpisodeHandle(Trainer* trainer, GameUID id, EpisodeKey k)
    : trainer_(trainer),
      gameID_(std::move(id)),
      episodeKey_(std::move(k)),
      guard_(trainer->epGuard_){};

EpisodeHandle::operator bool() const {
  return trainer_ && !guard_.expired();
}

EpisodeHandle::~EpisodeHandle() {
  if (*this) {
    // The trainer still there.
    trainer_->forceStopEpisode(*this);
  }
}

EpisodeHandle::EpisodeHandle(EpisodeHandle&& other)
    : EpisodeHandle(other.trainer_, other.gameID_, other.episodeKey_) {
  other.trainer_ = nullptr;
}

EpisodeHandle& EpisodeHandle::operator=(EpisodeHandle&& other) {
  if (*this) {
    trainer_->forceStopEpisode(*this);
  }
  trainer_ = other.trainer_;
  gameID_ = std::move(other.gameID_);
  episodeKey_ = std::move(other.episodeKey_);
  guard_ = std::move(other.guard_);
  other.trainer_ = nullptr;

  return *this;
}

EpisodeKey const& Trainer::EpisodeHandle::episodeKey() const {
  if (!(*this)) {
    throw std::runtime_error("Stale EpisodeHandle");
  }
  return episodeKey_;
}

GameUID const& Trainer::EpisodeHandle::gameID() const {
  if (!(*this)) {
    throw std::runtime_error("Stale EpisodeHandle");
  }
  return gameID_;
}

std::ostream& operator<<(std::ostream& os, EpisodeHandle const& handle) {
  os << handle.gameID();
  return os;
}

} // namespace cpid
