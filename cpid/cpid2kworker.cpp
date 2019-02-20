/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "cpid2kworker.h"

#include "netutils.h"
#include "redisclient.h"
#include "redisstore.h"

#include <common/assert.h>
#include <common/checksum.h>
#include <common/serialization.h>
#include <common/utils.h>

#include <cereal/archives/json.hpp>
#include <fmt/format.h>
#include <glog/logging.h>
#include <nlohmann/json.hpp>
#include <prettyprint/prettyprint.hpp>

using json = nlohmann::json;

namespace cpid {

namespace {
std::string rolePattern(std::string_view role) {
  // All workers are identified by `$N$role_$id`
  return fmt::format("?{}_*", role);
}
} // namespace

std::string const Cpid2kWorker::kAnyRole = "*";
std::chrono::milliseconds const Cpid2kWorker::kNoTimeout =
    std::chrono::milliseconds::zero();
std::chrono::milliseconds const Cpid2kWorker::kDefaultTimeout =
    std::chrono::milliseconds(-1);

Cpid2kWorkerInfo Cpid2kWorkerInfo::withLocalIp() {
  Cpid2kWorkerInfo info;
  info.host = netutils::getInterfaceAddresses()[0];
  return info;
}

Cpid2kHeartBeater::Cpid2kHeartBeater(
    Cpid2kWorkerInfo info,
    std::string prefix,
    std::string_view host,
    int port,
    int64_t intervalMs)
    : info_(std::move(info)),
      prefix_(std::move(prefix)),
      intervalMs_(intervalMs) {
  redis_ = std::make_unique<RedisClient>(
      host, port, fmt::format("{}:{}:hearbeater", prefix, info_.id));
  boot();
  th_ = std::thread(&Cpid2kHeartBeater::run, this);
}

Cpid2kHeartBeater::~Cpid2kHeartBeater() {
  stop_.store(true);
  th_.join();
}

bool Cpid2kHeartBeater::consideredDead() const {
  return consideredDead_.load();
}

std::string Cpid2kHeartBeater::bootKey() const {
  return fmt::format("{}:boot:{}", prefix_, info_.id);
}

std::string Cpid2kHeartBeater::deadKey() const {
  return fmt::format("{}:dead:{}", prefix_, info_.id);
}

std::string Cpid2kHeartBeater::heartBeatKey() const {
  return fmt::format("{}:heartbeat:{}", prefix_, info_.id);
}

std::string Cpid2kHeartBeater::heartBeatData() const {
  using namespace std::chrono;
  std::ostringstream os;
  {
    cereal::JSONOutputArchive ar(os);
    ar(cereal::make_nvp(
        "timestamp",
        duration_cast<seconds>(system_clock::now().time_since_epoch())
            .count()));
    ar(cereal::make_nvp("data", info_));
  }
  return os.str();
}

void Cpid2kHeartBeater::boot() {
  // At startup, atomically replace the "boot" entry for this worker with the
  // first heartbeat. If this fails (e.g. "boot" was already deleted, throw).
  // WATCH boot:$wid
  // if not EXISTS boot:$wid:
  //   # Considered dead, exit
  // MULTI
  // DEL boot:$wid
  // SETEX heartbeat:$id $timeout $workerData
  // EXEC
  try {
    auto replies = redis_->commands({
        redis_->format({"WATCH", bootKey()}),
        redis_->format({"EXISTS", bootKey()}),
    });
    if (replies.size() != 2 || !replies.at(0).ok()) {
      throw std::runtime_error("Can't watch boot key");
    }
    if (replies.at(1).integer() != 1) {
      throw std::runtime_error("Can't find boot key");
    }

    // Send initial heartbeat
    replies = redis_->commands({
        redis_->format("MULTI"),
        redis_->format({"DEL", bootKey()}),
        redis_->format({"PSETEX",
                        heartBeatKey(),
                        std::to_string(intervalMs_),
                        heartBeatData()}),
        redis_->format("EXEC"),
    });
    if (replies.size() != 4) {
      throw std::runtime_error("Unexpected number of replies");
    }
    if (replies[3].isNil()) { // WATCH failed
      throw std::runtime_error("Boot key changed");
    }
  } catch (std::exception const& ex) {
    throw std::runtime_error(
        info_.id + " can't send initial heartbeat: " + ex.what());
  }
}

void Cpid2kHeartBeater::run() {
  // Executes periodically:
  // WATCH dead:$id
  // if EXISTS dead:$id:
  //   # I'm considered dead -- exit
  // MULTI
  // PSETEX heartbeat:$id $timeout $workerData
  // EXEC
  using namespace std::chrono;
  using Clock = Cpid2kWorker::Clock;
  bool retry = false;
  auto lastSent = Clock::now();

  while (!stop_.load()) {
    // We'll be sending heartbeats four times as frequently as the expiration of
    // the heartbeat key to account for unexpected delays and give us some time
    // to recover from e.g. dropped connections or networking issues.
    if (!retry) {
      std::this_thread::sleep_for(milliseconds(intervalMs_ / 4));
    } else {
      std::this_thread::sleep_for(milliseconds(intervalMs_ / 10));
    }

    try {
      // Check for death
      auto replies = redis_->commands({
          redis_->format({"WATCH", deadKey()}),
          redis_->format({"EXISTS", deadKey()}),
      });
      if (!replies.at(0).ok()) {
        VLOG(0) << fmt::format(
            "{} heartbeat: failed watching death key, will try again next time",
            info_.id);
        continue;
      }
      if (replies.at(1).integer() != 0) {
        VLOG(0) << fmt::format(
            "{} heartbeat: considered dead by upstream -- that's all folks!",
            info_.id);
        break;
      }

      // Set heartbeat while watching for changes to the "dead" key
      replies = redis_->commands({
          redis_->format("MULTI"),
          redis_->format({"PSETEX",
                          heartBeatKey(),
                          std::to_string(intervalMs_),
                          heartBeatData()}),
          redis_->format("EXEC"),
      });
      if (replies.at(2).isNil()) {
        // EXEC failed, presumably due to the dead key being set
        VLOG(0) << fmt::format(
            "{} heartbeat: considered dead by upstream -- that's all folks!",
            info_.id);
        break;
      }
      if (!replies.at(2).at(0).ok()) {
        VLOG(0) << fmt::format(
            "{} heartbeat: can't set heartbeat, will try again shortly",
            info_.id);
        std::this_thread::sleep_for(milliseconds(100));
        break;
      }

      // Success!
      retry = false;
      lastSent = Clock::now();
    } catch (std::exception const& ex) {
      if (!redis_->isConnected()) {
        VLOG(0) << fmt::format(
            "{} heartbeat: client disconnected, trying to reconnect", info_.id);
        try {
          redis_->reconnect();
        } catch (std::exception const& ex) {
          VLOG(0) << fmt::format(
              "{} heartbeat: can't reconnect: {}", info_.id, ex.what());
        }
      } else {
        VLOG(0) << fmt::format(
            "{} heartbeat: can't set heartbeat, will try again shortly: {}",
            info_.id,
            ex.what());
      }
    }

    if (Clock::now() - lastSent > milliseconds(intervalMs_) * 2) {
      auto ms = duration_cast<milliseconds>(Clock::now() - lastSent).count();
      VLOG(0) << fmt::format(
          "{} heartbeat: could not send hearbeat for {}ms, will consider "
          "myself dead",
          info_.id,
          ms);
    }
  }

  if (!stop_.load()) {
    consideredDead_.store(true);
  }
}

Cpid2kWorker::Cpid2kWorker(
    Cpid2kWorkerInfo info,
    std::string prefix,
    std::string host,
    int port,
    int64_t hbIntervalMs)
    : info_(info),
      prefix_(prefix),
      host_(host),
      port_(port),
      hb_(std::move(info),
          std::move(prefix),
          std::move(host),
          port,
          hbIntervalMs) {
  // TODO: check peerv during heartbeats (= single request)?
  pcInterval_ = std::chrono::milliseconds(hbIntervalMs / 2);
}

Cpid2kWorker::~Cpid2kWorker() {}

Cpid2kWorkerInfo const& Cpid2kWorker::info() const {
  return info_;
}

/// Checks whether this worker is considered dead by the scheduler
bool Cpid2kWorker::consideredDead() const {
  return hb_.consideredDead();
}

/// Checks whether the training job is considered to be done.
/// This function returns true if
/// - `consideredDead()` returns true
/// - the global `done` flag is set in the Redis database
bool Cpid2kWorker::isDone() {
  if (consideredDead()) {
    return true;
  }

  auto lock = std::lock_guard(mutex_);
  updateGlobalState();
  return isDone_;
}

/// Returns a prefixed key.
/// This should be used when working with the Redis database using
/// `threadLocalClient()`.
std::string Cpid2kWorker::redisKey(std::string_view key) const {
  return fmt::format("{}:{}", prefix_, key);
}

/// Returns a Redis client dedicated to the calling thread.
/// The client can be used to perform custom operations on the Redis database.
/// hiredis clients are not thread-safe, hence we'lll keep around a dedicated
/// client for each thread that requires one.
std::shared_ptr<RedisClient> Cpid2kWorker::threadLocalClient() {
  auto lock = std::lock_guard(mutex_);
  auto rds = redisClient(std::this_thread::get_id());
  if (!rds->isConnected()) {
    // One-time effort to re-connect. This will throw on failure.
    rds->reconnect();
  }
  return rds;
}

/// Provides information about peers, filtered by role.
std::vector<Cpid2kWorkerInfo> Cpid2kWorker::peers(std::string_view role) {
  auto lock = std::lock_guard(mutex_);
  updateGlobalState();

  auto rp = rolePattern(role);
  std::vector<Cpid2kWorkerInfo> result;
  for (auto const& winfo : peers_) {
    if (common::gmatch(winfo.id, rp)) {
      result.push_back(winfo);
    }
  }
  return result;
}

std::vector<std::string> Cpid2kWorker::serviceEndpoints(
    std::string const& serviceName) {
  auto lock = std::lock_guard(mutex_);
  updateGlobalState();

  // Update list of endpoints of server accepting episodes
  std::vector<std::string> endpoints;
  for (auto const& winfo : peers_) {
    auto it = winfo.services.find(serviceName);
    if (it != winfo.services.end()) {
      endpoints.push_back(fmt::format("tcp://{}:{}", winfo.host, it->second));
    }
  }
  return endpoints;
}

/**
 * Provides a cpid::distributed context among workers with the given by role.
 *
 * If this function succeeds, rendez-vous has been successful. If rendez-vous
 * fails or there are no peers available for the given role, an exception will
 * be thrown. The worker calling this function is required to match the given
 * role.
 *
 * Contexts are cached for re-use and invalidated (and hence re-constructed) if
 * job constellation changes.
 *
 * `timeout` describes the timeout for all *gloo* primitives performed with this
 * context. The rendez-vous will be done with timeout that is twice as high as
 * the one specified. The default timeout value is 1.5 times the heartbeat
 * interval -- the intention is to notice job constellation changes upon
 * retries, and failing peers will be detected once their heartbeat expires.
 */
distributed::Context& Cpid2kWorker::dcontext(
    std::string const& role, // need std::string here for unordered_map lookup
    std::chrono::milliseconds timeout) {
  auto lock = std::lock_guard(mutex_);
  updateGlobalState();

  // Collect relevant peers to determine rank and size
  auto rp = rolePattern(role);
  std::vector<std::string> peerIds;
  for (auto const& winfo : peers_) {
    if (common::gmatch(winfo.id, rp)) {
      peerIds.push_back(winfo.id);
    }
  }
  if (peerIds.empty()) {
    throw std::runtime_error(fmt::format(
        "No peers found matching role '{}' (pattern '{}')", role, rp));
  }
  std::sort(peerIds.begin(), peerIds.end());

  // Check whether only non-relevant peers changed. In this case we don't need
  // to renew the context.
  bool hasContext = dcontexts_.find(role) != dcontexts_.end();
  if (hasContext && dcontextIds_[role] == peerIds) {
    VLOG(2) << fmt::format("{} old context ok!", info_.id);
    return *dcontexts_[role].get();
  }
  VLOG(2) << fmt::format(
                 "{} rebuilding context because hasContext={} and "
                 "dcontextIds_[{}]=",
                 info_.id,
                 hasContext,
                 role)
          << dcontextIds_[role];
  if (VLOG_IS_ON(2)) {
    std::ostringstream pinfo;
    for (auto i = 0U; i < peerIds.size(); i++) {
      pinfo << fmt::format("{}={} ", peerIds[i], i);
    }
    VLOG(2) << fmt::format("{} my list of peers: {}", info_.id, pinfo.str());
  }

  // Ok, build new context then
  dcontexts_.erase(role);
  int size = int(peerIds.size());
  int rank = [&] {
    auto it = std::lower_bound(peerIds.begin(), peerIds.end(), info_.id);
    if (it == peerIds.end() || *it != info_.id) {
      throw std::runtime_error(
          "Can't construct a context that I'm not part of (I'm '" + info_.id +
          "')");
    }
    return int(std::distance(peerIds.begin(), it));
  }();

  auto allIds = common::joinVector(peerIds, '|');
  auto digest = common::toHex(common::md5sum(allIds));
  auto rdvuKey = fmt::format("{}:c10d:{}", prefix_, digest);
  VLOG(1) << fmt::format(
      "{} rendez-vous with key {} (rank {} size {})",
      info_.id,
      rdvuKey,
      rank,
      size);
  auto store = std::make_shared<RedisStore>(rdvuKey, host_, port_);

  if (timeout == kDefaultTimeout) {
    timeout = std::chrono::milliseconds(hb_.intervalMs() * 3 / 2);
  }

  auto storeTimeout = std::chrono::duration_cast<std::chrono::seconds>(timeout);
  if (storeTimeout.count() < 1) {
    storeTimeout = std::chrono::seconds(1);
  }
  store->setTimeout(storeTimeout);

  dcontexts_[role] =
      std::make_unique<distributed::Context>(store, rank, size, timeout);
  dcontextIds_[role] = peerIds;
  return *dcontexts_[role].get();
}

/// Discards the cpid::distributed context that was previously created for
/// workers with the specified role.
void Cpid2kWorker::discardDContext(std::string const& role) {
  VLOG(2) << fmt::format(
      "{} discarding dcontext for role '{}'", info_.id, role);

  auto lock = std::lock_guard(mutex_);
  dcontexts_.erase(role);
  dcontextIds_.erase(role);
}

/// Block until a worker with the specified role is available, or until a
/// specified time has passed.
/// Returns true once a worker is available and false if the function times
/// out. A timeout of zero will disable timing out.
bool Cpid2kWorker::waitForOne(
    std::string_view role,
    std::chrono::milliseconds timeout) {
  auto start = Clock::now();
  while (peers(role).size() < 1) {
    if (timeout != kNoTimeout && Clock::now() - start > timeout) {
      return false;
    }
    std::this_thread::sleep_for(pcInterval_ + std::chrono::milliseconds(10));
  }
  return true;
}

/// Block until a all workers with the specified role are available, or until a
/// specified time has passed.
/// Returns true once all workers are available and false if the function times
/// out. A timeout of zero will disable timing out.
bool Cpid2kWorker::waitForAll(
    std::string_view role,
    std::chrono::milliseconds timeout) {
  // Parse job specification to determine the number of workers we need to wait
  // for. The pattern is the same as rolePattern() but without the '_*' suffix
  // for concrete worker IDs.
  auto pattern = fmt::format("?{}", role);
  int64_t count = [&] {
    auto reply = threadLocalClient()->get(fmt::format("{}:jobspec", prefix_));
    auto data = reply.string();
    int n = 0;
    try {
      auto spec = json::parse(data);
      for (auto const& part : spec) {
        if (common::gmatch(part["name"].get<std::string>(), pattern)) {
          n += part["count"].get<int>();
        }
      }
    } catch (std::exception const& ex) {
      throw std::runtime_error(
          fmt::format("Cannot parse jobspec: {}", ex.what()));
    }
    return n;
  }();

  VLOG(2) << fmt::format(
      "{} waiting for {} peers with role {}", info_.id, count, role);
  auto start = Clock::now();
  while (int64_t(peers(role).size()) < count) {
    if (timeout != kNoTimeout && Clock::now() - start > timeout) {
      return false;
    }
    std::this_thread::sleep_for(pcInterval_ + std::chrono::milliseconds(10));
  }
  return true;
}

std::shared_ptr<RedisClient> Cpid2kWorker::redisClient(std::thread::id id) {
  auto it = threadClients_.find(id);
  if (it != threadClients_.end()) {
    return it->second;
  }
  threadClients_[id] = std::make_shared<RedisClient>(host_, port_);
  return threadClients_[id];
}

/// Fetches global job meta-data with (limited) retries on failure.
void Cpid2kWorker::updateGlobalState() {
  while (true) {
    try {
      updateGlobalStateImpl();
      return;
    } catch (std::exception const& ex) {
      // Verify connectivity
      auto rds = redisClient(std::this_thread::get_id());
      if (rds->isConnected()) {
        throw; // Something else went wrong, bubble up
      }

      VLOG(0) << fmt::format(
          "{} error during global state update, retrying: {}",
          info_.id,
          ex.what());
    }

    // Re-connect and retry; reconnect() throws if not successful
    // If we can connect, simply try again.
    redisClient(std::this_thread::get_id())->reconnect();
  }
}

/// Fetches global job meta-data from the central redis instance.
void Cpid2kWorker::updateGlobalStateImpl() {
  if (Clock::now() - lastPeersCheck_ < pcInterval_) {
    return;
  }

  auto rds = redisClient(std::this_thread::get_id());
  auto replies = rds->commands({
      rds->format({"GET", fmt::format("{}:done", prefix_)}),
      rds->format({"GET", fmt::format("{}:peerv", prefix_)}),
  });
  ASSERT(replies.size() == 2);

  isDone_ = replies[0].isString() && replies[0].string() == "true";
  int64_t newPeerv = std::stol(replies[1].string());
  lastPeersCheck_ = Clock::now();
  if (newPeerv == peerv_) {
    VLOG(3) << fmt::format("{} peerv unchanged at {}", info_.id, peerv_);
    return;
  }
  peerv_ = newPeerv;

  // Fetch list of all peers by scanning the database for heartbeats. This is an
  // iterative process, i.e. we'll keep track of a cursor and query the database
  // multiple times. Note that the COUNT option is only a hint and there's no
  // reason to expect that the SCAN call return min(num_matching, batch_size)
  // elements. See also https://redis.io/commands/scan.
  auto batchSize = 256;
  auto pattern = fmt::format("{}:heartbeat:*", prefix_);
  std::string cursor = "0";
  std::vector<std::string> keys;
  do {
    auto reply = rds->command(
        {"SCAN", cursor, "MATCH", pattern, "COUNT", std::to_string(batchSize)});
    if (reply.size() != 2) {
      throw std::runtime_error("Can't scan heartbeat table");
    }
    cursor = reply.at(0).string();
    for (auto const& r : reply.at(1)) {
      keys.push_back(r.string());
    }
  } while (cursor != "0");

  // Fetch peer data
  std::vector<std::string_view> args;
  args.push_back("MGET");
  for (auto const& key : keys) {
    args.push_back(key);
  }
  auto reply = rds->command(args);

  std::vector<Cpid2kWorkerInfo> peers;
  for (auto const& r : reply) {
    if (r.isNil()) {
      continue;
    }

    peers.emplace_back();
    common::IMembuf buf(r.stringv());
    std::istream is(&buf);
    cereal::JSONInputArchive ar(is);
    ar(cereal::make_nvp("data", peers.back()));
  }
  VLOG(2) << fmt::format(
      "{} got information about {} peers", info_.id, peers.size());
  peers_ = std::move(peers);
}

} // namespace cpid
