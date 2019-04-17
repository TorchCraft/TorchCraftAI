/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * NOTE: each of these tests requires an empy redis instance available at
 * -redis_host and -redis_port.
 */

#include "test.h"

#include "cpid/redisclient.h"

#include <fmt/format.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

DEFINE_string(redis_host, "localhost", "Redis host");
DEFINE_int32(redis_port, 6379, "Redis port");

using namespace cpid;

CASE("redisclient/wronghost[.redis]") {
  std::unique_ptr<RedisClient> cl;
  EXPECT_THROWS(cl = std::make_unique<RedisClient>("foo", 1234));
}

CASE("redisclient/ping[.redis]") {
  std::unique_ptr<RedisClient> cl;
  EXPECT_NO_THROW(
      cl = std::make_unique<RedisClient>(FLAGS_redis_host, FLAGS_redis_port));
  EXPECT(cl->ping());
}

CASE("redisclient/setget[.redis]") {
  std::unique_ptr<RedisClient> cl;
  EXPECT_NO_THROW(
      cl = std::make_unique<RedisClient>(FLAGS_redis_host, FLAGS_redis_port));
  RedisReply reply;
  EXPECT_NO_THROW(reply = cl->set("foo", "bar"));
  EXPECT(reply.ok());
  EXPECT_NO_THROW(reply = cl->get("foo"));
  EXPECT(reply.stringv() == "bar");
}

CASE("redisclient/setget_manual[.redis]") {
  std::unique_ptr<RedisClient> cl;
  EXPECT_NO_THROW(
      cl = std::make_unique<RedisClient>(FLAGS_redis_host, FLAGS_redis_port));
  RedisReply reply;
  EXPECT_NO_THROW(reply = cl->command({"SET", "foo", "bar"}));
  EXPECT(reply.ok());
  EXPECT_NO_THROW(reply = cl->command({"GET", "foo"}));
  EXPECT(reply.stringv() == "bar");
}

CASE("redisclient/list/string[.redis]") {
  std::unique_ptr<RedisClient> cl;
  EXPECT_NO_THROW(
      cl = std::make_unique<RedisClient>(FLAGS_redis_host, FLAGS_redis_port));
  RedisReply reply;
  EXPECT_NO_THROW(reply = cl->command({"RPUSH", "mylist", "one"}));
  EXPECT(reply.integer() == 1);
  EXPECT_NO_THROW(reply = cl->command({"RPUSH", "mylist", "two"}));
  EXPECT(reply.integer() == 2);
  EXPECT_NO_THROW(reply = cl->command({"RPUSH", "mylist", "three"}));
  EXPECT(reply.integer() == 3);
  EXPECT_NO_THROW(reply = cl->command({"LRANGE", "mylist", "0", "0"}));
  std::vector<std::string_view> val{"one"};
  EXPECT(reply.at(0).stringv() == "one");
  EXPECT_NO_THROW(reply = cl->command({"LRANGE", "mylist", "-3", "2"}));
  val = {"one", "two", "three"};
  EXPECT(reply.size() == 3U);
  EXPECT(reply.stringvs() == val);
  EXPECT_NO_THROW(reply = cl->command({"LRANGE", "mylist", "5", "10"}));
  val = {};
  EXPECT(reply.stringvs() == val);
}

CASE("redisclient/pipeline[.redis]") {
  std::unique_ptr<RedisClient> cl;
  EXPECT_NO_THROW(
      cl = std::make_unique<RedisClient>(FLAGS_redis_host, FLAGS_redis_port));
  std::vector<RedisReply> replies;
  EXPECT_NO_THROW(
      replies = cl->commands({
          cl->format({"SET", "foo1", "bar"}),
          cl->format({"SET", "foo2", "baz"}),
          cl->format("SET %s %s", "foo3", "bal"),
      }));
  EXPECT(replies.size() == 3U);
  EXPECT(replies[0].ok());
  EXPECT(replies[1].ok());
  EXPECT(replies[2].ok());

  EXPECT_NO_THROW(
      replies = cl->commands({
          cl->format({"GET", "foo1"}),
          cl->format({"GET", "foo2"}),
          cl->format({"GET", "foo3"}),
      }));
  EXPECT(replies.size() == 3U);
  EXPECT(replies[0].string() == "bar");
  EXPECT(replies[1].stringv() == "baz");
  EXPECT(replies[2].stringv() == "bal");
}

CASE("redisclient/scan[.redis]") {
  std::unique_ptr<RedisClient> cl;
  EXPECT_NO_THROW(
      cl = std::make_unique<RedisClient>(FLAGS_redis_host, FLAGS_redis_port));
  RedisReply reply;
  std::set<std::string> keys;
  for (auto i = 0; i < 10; i++) {
    auto key = fmt::format("prefix:{}", i);
    keys.insert(key);
    EXPECT_NO_THROW(reply = cl->set(key, std::to_string(i)));
    EXPECT(reply.ok());
  }
  EXPECT_NO_THROW(reply = cl->set("nomatch", "bla"));
  EXPECT(reply.ok());

  // All at once
  EXPECT_NO_THROW(
      reply =
          cl->command({"SCAN", "0", "MATCH", "prefix:*", "COUNT", "10000"}));
  EXPECT(reply.size() == 2u);
  EXPECT(reply.at(0).stringv() == "0");
  EXPECT(reply.at(1).size() == 10u);
  auto tkeys = keys;
  for (auto i = 0U; i < reply.at(1).size(); i++) {
    std::string key;
    EXPECT_NO_THROW(key = reply.at(1).at(i).string());
    EXPECT(tkeys.find(key) != tkeys.end());
    tkeys.erase(key);
  }
  EXPECT(tkeys.empty());

  // Iteratively; note that COUNT is approximate
  tkeys = keys;
  std::string cursor = "0";
  do {
    EXPECT_NO_THROW(
        reply =
            cl->command({"SCAN", cursor, "MATCH", "prefix:*", "COUNT", "2"}));
    EXPECT(reply.size() == 2u);
    EXPECT(reply.at(0).isString());
    EXPECT(reply.at(1).isArray());
    for (auto i = 0U; i < reply.at(1).size(); i++) {
      std::string key;
      EXPECT_NO_THROW(key = reply.at(1).at(i).string());
      EXPECT(tkeys.find(key) != tkeys.end());
      tkeys.erase(key);
    }
    cursor = reply.at(0).string();
  } while (cursor != "0");
  EXPECT(tkeys.empty());
}

CASE("redisclient/multi[.redis]") {
  std::unique_ptr<RedisClient> cl;
  EXPECT_NO_THROW(
      cl = std::make_unique<RedisClient>(FLAGS_redis_host, FLAGS_redis_port));

  // Single commands
  RedisReply reply;
  EXPECT_NO_THROW(reply = cl->command("MULTI"));
  EXPECT(reply.ok());
  EXPECT_NO_THROW(reply = cl->command({"INCR", "foo"}));
  EXPECT(reply.status() == "QUEUED");
  EXPECT_NO_THROW(reply = cl->command({"INCR", "bar"}));
  EXPECT(reply.status() == "QUEUED");
  EXPECT_NO_THROW(reply = cl->command("EXEC"));
  EXPECT(reply.isArray());
  EXPECT(reply.size() == 2U);
  EXPECT(reply.at(0).integer() == 1);
  EXPECT(reply.at(1).integer() == 1);

  // Pipelined
  std::vector<RedisReply> replies;
  EXPECT_NO_THROW(
      replies = cl->commands({
          cl->format("MULTI"),
          cl->format({"INCR", "foo"}),
          cl->format({"INCR", "bar"}),
          cl->format("EXEC"),
      }));
  EXPECT(replies.size() == 4U);
  EXPECT(replies[0].ok());
  EXPECT(replies[1].status() == "QUEUED");
  EXPECT(replies[2].status() == "QUEUED");
  EXPECT(replies[3].isArray());
  EXPECT(replies[3].size() == 2U);
  EXPECT(replies[3].at(0).integer() == 2);
  EXPECT(replies[3].at(1).integer() == 2);
}

CASE("redisclient/multi_error[.redis]") {
  std::unique_ptr<RedisClient> cl;
  EXPECT_NO_THROW(
      cl = std::make_unique<RedisClient>(FLAGS_redis_host, FLAGS_redis_port));

  std::vector<RedisReply> replies;
  EXPECT_NO_THROW(
      replies = cl->commands({
          cl->format("MULTI"),
          cl->format({"SET", "a", "1"}),
          cl->format({"LPOP", "a"}),
          cl->format("EXEC"),
      }));
  EXPECT(replies.size() == 4U);
  EXPECT(replies[0].ok());
  EXPECT(replies[1].status() == "QUEUED");
  EXPECT(replies[2].status() == "QUEUED");
  EXPECT(replies[3].isArray());
  EXPECT(replies[3].size() == 2U);
  EXPECT(replies[3].at(0).ok());
  EXPECT(replies[3].at(1).isError());
}

CASE("redisclient/watch_failed[.redis]") {
  std::unique_ptr<RedisClient> cl1, cl2;
  EXPECT_NO_THROW(
      cl1 = std::make_unique<RedisClient>(FLAGS_redis_host, FLAGS_redis_port));
  EXPECT_NO_THROW(
      cl2 = std::make_unique<RedisClient>(FLAGS_redis_host, FLAGS_redis_port));

  // Client 1 watches a key and starts a transaction
  RedisReply reply;
  EXPECT_NO_THROW(reply = cl1->command({"WATCH", "mykey"}));
  EXPECT(reply.ok());
  EXPECT_NO_THROW(reply = cl1->command("MULTI"));
  EXPECT(reply.ok());
  EXPECT_NO_THROW(reply = cl1->command({"INCR", "foo"}));
  EXPECT(reply.status() == "QUEUED");

  // Client 2 sets a value
  EXPECT_NO_THROW(reply = cl2->command({"SET", "mykey", "value"}));
  EXPECT(reply.ok());

  // Client 1 executes the transaction and notices failure
  EXPECT_NO_THROW(reply = cl1->command("EXEC"));
  EXPECT(reply.isNil());
}
