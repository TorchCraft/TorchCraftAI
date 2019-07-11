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
#include <common/logging.h>
#include <common/rand.h>
#include <common/serialization.h>
#include <common/utils.h>

#include <cereal/archives/json.hpp>
#include <fmt/format.h>
#include <glog/logging.h>
#include <prettyprint/prettyprint.hpp>

using json = nlohmann::json;

namespace cpid {

namespace {
std::string rolePattern(std::string_view role) {
  // All workers are identified by `$N$role_$id`
  return fmt::format("?{}_*", role);
}
std::string assertEnv(char const* name) {
  char* value;
  ASSERT(
      value = std::getenv(name),
      fmt::format("Environment variable {} is not set!", name));
  return value;
}
constexpr std::string_view kRedisMetricsKey = "metrics";
constexpr char kCpid2kIdEnv[] = "CPID2K_ID";
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

Cpid2kWorkerInfo Cpid2kWorkerInfo::withLocalIpFromEnvVars() {
  Cpid2kWorkerInfo info = withLocalIp();
  info.id = assertEnv(kCpid2kIdEnv);
  return info;
}

bool Cpid2kWorkerInfo::roleIs(std::string_view role) {
  return common::gmatch(id, rolePattern(role));
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
  registerCommand("v", [](json const& j) {
    int newVerboseLevel = j.get<int>();
    VLOG(0) << "Updated -v (" << FLAGS_v << ") to " << newVerboseLevel;
    FLAGS_v = newVerboseLevel;
  });
  registerCommand("vfilter", [](json const& j) {
    std::string vfilter = j.get<std::string>();
    VLOG(0) << "Updated -vfilter (" << FLAGS_vfilter << ") to " << vfilter;
    FLAGS_vfilter = vfilter;
  });
  registerCommand("hb_interval", [this](json const& j) {
    auto hbIntervalMs = j.get<int64_t>();
    VLOG(0) << "Updated hb_interval (" << intervalMs_ << ") to "
            << hbIntervalMs;
    intervalMs_ = hbIntervalMs;
  });
  th_ = std::thread(&Cpid2kHeartBeater::run, this);
}

Cpid2kHeartBeater::~Cpid2kHeartBeater() {
  stop_.store(true);
  th_.join();
}

void Cpid2kHeartBeater::registerCommand(
    std::string_view name,
    CommandImpl impl) {
  std::unique_lock l(commandsImplM_);
  commandsImpl_[std::string(name)] = std::move(impl);
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

std::string Cpid2kHeartBeater::commandsKey() const {
  return fmt::format("{}:commands:{}", prefix_, info_.id);
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
  common::setCurrentThreadName("heartbeater");

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

      auto commandsReply =
          redis_->command(redis_->format({"GETSET", commandsKey(), ""}));
      if (commandsReply.isString()) {
        executeCommands(commandsReply.string());
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
      break;
    }
  }

  if (!stop_.load()) {
    consideredDead_.store(true);
  }
}

void Cpid2kHeartBeater::executeCommands(std::string command) {
  if (command.empty()) {
    return;
  }
  VLOG(0) << "Received commands: " << command;
  try {
    json obj = json::parse(command);
    std::unique_lock l(commandsImplM_);
    for (json::iterator it = obj.begin(); it != obj.end(); ++it) {
      auto impl = commandsImpl_.find(it.key());
      if (impl == commandsImpl_.end()) {
        VLOG(0) << "Error: skipped unknown command " << it.key();
        continue;
      }
      try {
        impl->second(it.value());
      } catch (std::exception const& e) {
        VLOG(0) << "Exception parsing command " << it.key() << ": " << e.what();
      }
    }
  } catch (std::exception const& e) {
    VLOG(0) << "Exception parsing commands: " << e.what();
  }
}

Cpid2kGlobalState::Cpid2kGlobalState(
    std::string prefix,
    int64_t updateIntervalMs)
    : prefix_(std::move(prefix)) {
  pcInterval_ = std::chrono::milliseconds(updateIntervalMs);
}

void Cpid2kGlobalState::update(RedisClient& client) {
  auto lock = std::lock_guard(mutex_);
  while (true) {
    try {
      tryUpdate(client);
      return;
    } catch (std::exception const& ex) {
      // Verify connectivity
      if (client.isConnected()) {
        throw; // Something else went wrong, bubble up
      }

      VLOG(0) << fmt::format(
          "{} error during global state update, retrying: {}",
          prefix_,
          ex.what());
    }

    // Re-connect and retry; reconnect() throws if not successful
    // If we can connect, simply try again.
    client.reconnect();
  }
}

bool Cpid2kGlobalState::isDone() {
  return isDone_;
}

/// Provides information about peers, filtered by role.
std::vector<Cpid2kWorkerInfo> Cpid2kGlobalState::peers(std::string_view role) {
  auto lock = std::lock_guard(mutex_);
  auto rp = rolePattern(role);
  std::vector<Cpid2kWorkerInfo> result;
  for (auto const& winfo : peers_) {
    if (common::gmatch(winfo.id, rp)) {
      result.push_back(winfo);
    }
  }
  return result;
}

std::vector<std::string> Cpid2kGlobalState::serviceEndpoints(
    std::string const& serviceName) {
  auto lock = std::lock_guard(mutex_);
  std::vector<std::string> endpoints;
  for (auto const& winfo : peers_) {
    auto it = winfo.services.find(serviceName);
    if (it != winfo.services.end()) {
      endpoints.push_back(fmt::format("tcp://{}:{}", winfo.host, it->second));
    }
  }
  return endpoints;
}

/// Fetches global job meta-data from the central redis instance.
void Cpid2kGlobalState::tryUpdate(RedisClient& client) {
  if (Clock::now() - lastPeersCheck_ < pcInterval_) {
    return;
  }

  auto replies = client.commands({
      client.format({"GET", fmt::format("{}:done", prefix_)}),
      client.format({"GET", fmt::format("{}:peerv", prefix_)}),
  });
  ASSERT(replies.size() == 2);

  isDone_ = replies[0].isString() && replies[0].string() == "true";
  int64_t newPeerv =
      replies[1].isString() ? std::stol(replies[1].string()) : -1;
  lastPeersCheck_ = Clock::now();
  if (newPeerv == peerv_) {
    VLOG(3) << fmt::format("{} peerv unchanged at {}", prefix_, peerv_);
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
    auto reply = client.command(
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
  std::vector<Cpid2kWorkerInfo> peers;
  if (!keys.empty()) {
    std::vector<std::string_view> args;
    args.push_back("MGET");
    for (auto const& key : keys) {
      args.push_back(key);
    }
    auto reply = client.command(args);

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
  }

  VLOG(2) << fmt::format(
      "{} got information about {} peers", prefix_, peers.size());
  peers_ = std::move(peers);
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
      hb_(std::move(info), prefix, std::move(host), port, hbIntervalMs),
      gs_(prefix, hbIntervalMs / 2) {
  pcInterval_ = std::chrono::milliseconds(hbIntervalMs / 2);
}

Cpid2kWorker::~Cpid2kWorker() {}

std::unique_ptr<Cpid2kWorker> Cpid2kWorker::fromEnvVars(
    Cpid2kWorkerInfo const& info) {
  return std::make_unique<cpid::Cpid2kWorker>(
      std::move(info),
      assertEnv("CPID2K_REDIS_PREFIX"),
      assertEnv("CPID2K_REDIS_HOST"),
      std::stoi(assertEnv("CPID2K_REDIS_PORT")));
}

std::unique_ptr<Cpid2kWorker> Cpid2kWorker::fromEnvVars() {
  if (!std::getenv(kCpid2kIdEnv)) {
    return nullptr;
  }
  return fromEnvVars(Cpid2kWorkerInfo::withLocalIpFromEnvVars());
}

Cpid2kWorkerInfo const& Cpid2kWorker::info() const {
  return info_;
}

std::string_view Cpid2kWorker::prefix() const {
  return prefix_;
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

  gs_.update(*threadLocalClient());
  return gs_.isDone();
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
  gs_.update(*threadLocalClient());
  return gs_.peers(role);
}

std::vector<std::string> Cpid2kWorker::serviceEndpoints(
    std::string const& serviceName) {
  gs_.update(*threadLocalClient());
  return gs_.serviceEndpoints(serviceName);
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
  gs_.update(*threadLocalClient());

  // Collect relevant peers to determine rank and size
  auto peers = gs_.peers(role);
  std::vector<std::string> peerIds;
  for (auto const& winfo : peers) {
    peerIds.push_back(winfo.id);
  }
  if (peerIds.empty()) {
    throw std::runtime_error(fmt::format(
        "No peers found matching role '{}' (pattern '{}')",
        role,
        rolePattern(role)));
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
/// If there will be no worker with the given role, throw an exception.
bool Cpid2kWorker::waitForOne(
    std::string_view role,
    std::chrono::milliseconds timeout) {
  int64_t count = numWorkersWithRoleInSpec(role);
  if (count == 0) {
    throw std::runtime_error(
        fmt::format("No such worker in job spec: {}", role));
  }

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
  int64_t count = numWorkersWithRoleInSpec(role);
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

void Cpid2kWorker::appendMetrics(
    std::string_view metricsName,
    nlohmann::json const& json) {
  auto redis = threadLocalClient();
  auto reply = redis->command(
      {"RPUSH",
       fmt::format(
           "{}:{}:{}:{}", prefix_, kRedisMetricsKey, info_.id, metricsName),
       json.dump(-1, ' ')});
  if (reply.isError()) {
    LOG(WARNING) << "[cpid2k] Unable to appendMetrics: " << reply.error();
  }
}

void Cpid2kWorker::publishEvent(std::string_view key, nlohmann::json data) {
  auto redis = threadLocalClient();
  auto reply = redis->command({"PUBLISH",
                               fmt::format("{}:{}:{}", prefix_, key, info_.id),
                               data.dump(-1, ' ')});
  if (reply.isError()) {
    LOG(WARNING) << "[cpid2k] Unable to publishEvent: " << reply.error();
  }
}

std::shared_ptr<RedisClient> Cpid2kWorker::redisClient(std::thread::id id) {
  auto it = threadClients_.find(id);
  if (it != threadClients_.end()) {
    return it->second;
  }
  std::string name =
      fmt::format("cpid2k_worker_{}_t{}", info().id, threadClients_.size());
  threadClients_[id] = std::make_shared<RedisClient>(host_, port_, name);
  return threadClients_[id];
}

int Cpid2kWorker::numWorkersWithRoleInSpec(std::string_view role) {
  // Parse job specification to determine the number of workers we need to wait
  // for. The pattern is the same as rolePattern() but without the '_*' suffix
  // for concrete worker IDs.
  auto pattern = fmt::format("?{}", role);
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
}

Cpid2kMetrics::Cpid2kMetrics(
    std::shared_ptr<Cpid2kWorker> worker,
    std::chrono::milliseconds sendInterval,
    size_t subsample)
    : worker_(worker),
      sendInterval_(sendInterval),
      subsample_(subsample),
      stop_(false) {
  ASSERT(subsample_ > 0);
  if (worker && common::Rand::rand() % subsample_ == 0) {
    thr_ = std::thread(&Cpid2kMetrics::run, this);
  }
}
Cpid2kMetrics::~Cpid2kMetrics() {
  if (thr_.joinable()) {
    stop_ = true;
    thr_.join();
  }
}

namespace {
struct FnAggregator : Cpid2kMetrics::Aggregator {
  FnAggregator(
      std::string_view type,
      std::function<float(float, float)> fn,
      float initialValue)
      : fn(fn), currentValue(initialValue) {
    this->type = type;
  }
  virtual void add(float value) override {
    currentValue = fn(currentValue, value);
  }
  virtual nlohmann::json value() const override {
    return nlohmann::json(currentValue);
  }
  virtual float floatValue() const override {
    return currentValue;
  }

  std::function<float(float, float)> fn;
  float currentValue = 0;
};

struct MeanAggregator : Cpid2kMetrics::Aggregator {
  MeanAggregator() : currentValue(0), count(0) {
    type = "mean_agg";
  }
  virtual void add(float value) override {
    currentValue += value;
    count += 1;
  }
  virtual nlohmann::json value() const override {
    ASSERT(count > 0);
    return {
        {"sum", currentValue},
        {"sum_coefs", count},
    };
  }
  virtual float floatValue() const override {
    return currentValue / float(count);
  }

  float currentValue = 0;
  int count = 0;
};
} // namespace

Cpid2kMetrics::TimerMs::TimerMs(
    std::shared_ptr<Cpid2kMetrics> m,
    const std::string& name,
    AggregationType agg,
    const std::string& prefix)
    : m_(m), name_(name), agg_(agg), prefix_(prefix) {
  start_ = std::chrono::steady_clock::now();
}

void Cpid2kMetrics::TimerMs::stop() {
  if (running_) {
    auto end = std::chrono::steady_clock::now();
    elapsed_ += end - start_;
    running_ = false;
  }
}

void Cpid2kMetrics::TimerMs::resume() {
  if (!running_) {
    running_ = true;
    start_ = std::chrono::steady_clock::now();
  }
}

Cpid2kMetrics::TimerMs::~TimerMs() {
  if (!m_) {
    return;
  }
  stop();
  m_->push({{name_, elapsed_.count(), agg_}}, prefix_);
  VLOG_IF(1, (common::Rand::rand() % 1000 == 0 || VLOG_IS_ON(2)))
      << fmt::format("{}: {}ms", name_, elapsed_.count());
}

void Cpid2kMetrics::push(
    std::vector<EventMetric> const& metrics,
    std::string prefix) {
  if (!enabled()) {
    return;
  }
  if (prefix.empty()) {
    prefix = worker_ ? worker_->prefix() : "";
  }
  std::unique_lock l(aggregatorsMutex_);
  auto& agtors = aggregators_[prefix];
  for (auto const& m : metrics) {
    auto it = agtors.find(m.name);
    std::unique_ptr<Aggregator> agg;
    if (it == agtors.end()) {
      agtors[m.name] = [&]() -> std::unique_ptr<Aggregator> {
        switch (m.aggregation) {
          case AggregateMax:
            return std::make_unique<FnAggregator>(
                "max",
                [](float a, float b) -> float { return std::max<float>(a, b); },
                m.value);
          case AggregateMin:
            return std::make_unique<FnAggregator>(
                "min",
                [](float a, float b) -> float { return std::min<float>(a, b); },
                m.value);
          case AggregateSum:
            return std::make_unique<FnAggregator>(
                "sum", [](float a, float b) -> float { return a + b; }, 0);
          case AggregateCumSum:
            return std::make_unique<FnAggregator>(
                "cumsum", [](float a, float b) -> float { return a + b; }, 0);
          case AggregateLast:
            return std::make_unique<FnAggregator>(
                "last", [](float, float b) -> float { return b; }, m.value);
          case AggregateMean:
            return std::make_unique<MeanAggregator>();
          default:
            ASSERT(false, "Unknown aggregation type");
        }
      }();
      it = agtors.find(m.name);
    }
    for (int i = 0; i < subsample_; ++i) {
      it->second->add(m.value);
    }
  }
}

bool Cpid2kMetrics::enabled() const {
  return !worker_ || thr_.joinable();
}

std::unordered_map<std::string, float> Cpid2kMetrics::aggregateLocal(
    std::string const& prefix) const {
  std::unordered_map<std::string, float> ret;
  std::unique_lock l(aggregatorsMutex_);
  auto agtorsIt = aggregators_.find(prefix);
  if (agtorsIt != aggregators_.end()) {
    for (auto const& it : agtorsIt->second) {
      ret[it.first] = it.second->floatValue();
    }
  }
  return ret;
}

void Cpid2kMetrics::run() {
  common::setCurrentThreadName("metrics");
  std::uniform_real_distribution<> dist(0, 1);
  auto lastSent = Clock::now() - sendInterval_ * common::Rand::sample(dist);
  while (!stop_) {
    if (Clock::now() - lastSent < sendInterval_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      continue;
    }
    lastSent = Clock::now();
    decltype(aggregators_) aggregators;
    {
      std::unique_lock l(aggregatorsMutex_);
      std::swap(aggregators, aggregators_);
    }

    if (aggregators.empty()) {
      continue;
    }
    auto rds = worker_->threadLocalClient();
    auto now = common::timestamp();
    std::vector<std::string> allCommandsStr;
    for (auto const& prefixMetrics : aggregators) {
      std::vector<std::string> commandAsStr = {
          "RPUSH",
          fmt::format("{}:metricEvents", prefixMetrics.first),
      };
      for (auto const& it : prefixMetrics.second) {
        commandAsStr.emplace_back(
            nlohmann::json({{"time", now},
                            {"type", std::string(it.second->type)},
                            {"name", it.first},
                            {"value", it.second->value()}})
                .dump(-1, ' '));
      }
      std::vector<std::string_view> eventsAsStrView;
      for (auto const& cmd : commandAsStr) {
        eventsAsStrView.push_back(cmd);
      }
      allCommandsStr.emplace_back(rds->format(eventsAsStrView));
    }
    auto replies = rds->commands(allCommandsStr);
    for (auto const& reply : replies) {
      if (reply.isError()) {
        LOG(WARNING) << "[cpid2k] Unable to push metrics: " << reply.error();
        continue;
      }
    }
  }
}
} // namespace cpid
