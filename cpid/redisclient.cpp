/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "redisclient.h"

#include <unordered_map>

#include <fmt/format.h>

namespace cpid {

namespace {

template <typename ForwardIt>
std::string formatT(ForwardIt begin, ForwardIt end) {
  int argc = int(std::distance(begin, end));
  std::vector<char const*> argv;
  std::vector<size_t> argvlen;
  for (auto it = begin; it != end; ++it) {
    argv.push_back(it->data());
    argvlen.push_back(it->size());
  }

  char* target;
  int len = redisFormatCommandArgv(&target, argc, argv.data(), argvlen.data());

  if (len == -1) {
    throw std::runtime_error("Out of memory");
  } else if (len == -2) {
    throw std::runtime_error("Invalid format string");
  } else {
    auto str = std::string(target, len);
    redisFreeCommand(target);
    return str;
  }

  throw std::runtime_error("Unknown error code: " + std::to_string(len));
}

} // namespace

RedisClient::RedisClient(
    std::string_view host,
    int port,
    std::string_view name) {
  struct timeval timeout = {.tv_sec = 10};
  redis_ = redisConnectWithTimeout(host.data(), port, timeout);
  if (redis_ == nullptr) {
    throw std::runtime_error("Cannot initialize redis client");
  }
  if (redis_->err != 0) {
    throw std::runtime_error(
        "Error connecting to redis: " + std::string(redis_->errstr));
  }

  if (name.size() > 0) {
    auto reply = command({"CLIENT", "SETNAME", name});
    if (reply.isError()) {
      throw std::runtime_error(fmt::format(
          "Failed to set requested name '{}';: {}", name, reply.error()));
    }
  }
}

RedisClient::~RedisClient() {
  if (redis_ != nullptr) {
    redisFree(redis_);
  }
}

std::string_view RedisClient::host() const {
  return std::string_view(redis_->tcp.host);
}

int RedisClient::port() const {
  return redis_->tcp.port;
}

bool RedisClient::isConnected() const {
  // This is not completely bullet-proof. We'll get I/O errors on timeouts, for
  // example, and EOF errors if the connction was dropped.
  if (redis_->err == REDIS_ERR_IO || redis_->err == REDIS_ERR_EOF) {
    return false;
  }
  return true;
}

void RedisClient::reconnect() const {
  if (redisReconnect(redis_) != REDIS_OK) {
    throw std::runtime_error(
        "Error connecting to redis: " + std::string(redis_->errstr));
  }
}

std::string RedisClient::format(char const* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  char* target;
  int len = redisvFormatCommand(&target, fmt, args);
  va_end(args);

  if (len == -1) {
    throw std::runtime_error("Out of memory");
  } else if (len == -2) {
    throw std::runtime_error("Invalid format string");
  } else {
    auto str = std::string(target, len);
    redisFreeCommand(target);
    return str;
  }

  throw std::runtime_error("Unknown error code: " + std::to_string(len));
}

std::string RedisClient::format(std::initializer_list<std::string_view> args) {
  return formatT(args.begin(), args.end());
}

std::string RedisClient::format(std::vector<std::string_view> const& args) {
  return formatT(args.begin(), args.end());
}

RedisReply RedisClient::command(char const* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  void* reply = redisvCommand(redis_, fmt, args);
  va_end(args);

  if (reply == nullptr) {
    throw std::runtime_error(redis_->errstr);
  }
  return RedisReply(reply);
}

RedisReply RedisClient::command(std::initializer_list<std::string_view> args) {
  return command(format(args));
}

RedisReply RedisClient::command(std::vector<std::string_view> const& args) {
  return command(format(args));
}

/// Sends a formatted command
RedisReply RedisClient::command(std::string_view cmd) {
  int ret = redisAppendFormattedCommand(redis_, cmd.data(), cmd.size());
  if (ret != REDIS_OK) {
    throw std::runtime_error(redis_->errstr);
  }

  return getReply();
}

/**
 * Sends a list of formatted commands in a single request.
 *
 * This function peforms pipelining, assuming all elements in `cmds` are
 * formatted Redis commands. The function will wait for all replies (i.e.
 * `cmds.size()` replies) and then return all of them.
 */
std::vector<RedisReply> RedisClient::commands(
    std::vector<std::string> const& cmds) {
  for (auto const& cmd : cmds) {
    int ret = redisAppendFormattedCommand(redis_, cmd.c_str(), cmd.size());
    if (ret != REDIS_OK) {
      throw std::runtime_error(redis_->errstr);
    }
  }

  std::vector<RedisReply> replies;
  for (auto i = 0U; i < cmds.size(); i++) {
    replies.emplace_back(getReply());
  }
  return replies;
}

RedisReply RedisClient::getReply() {
  void* reply;
  int ret = redisGetReply(redis_, &reply);
  if (ret != REDIS_OK || reply == nullptr) {
    throw std::runtime_error(redis_->errstr);
  }
  return RedisReply(reply);
}

bool RedisClient::ping() {
  auto reply = command("PING");
  auto v = reply.statusv();
  return v == "PONG" || v == "pong";
}

RedisReply RedisClient::set(std::string_view key, std::string_view value) {
  return command({"SET", key, value});
}

RedisReply RedisClient::get(std::string_view key) {
  return command({"GET", key});
}

redisContext* RedisClient::ctx() {
  return redis_;
}

RedisReply::RedisReply(void* reply, bool own)
    : reply_(static_cast<redisReply*>(reply)), owns_(own) {
  if (reply_->type == REDIS_REPLY_ARRAY) {
    elements_.reserve(reply_->elements);
    for (size_t i = 0; i < reply_->elements; i++) {
      elements_.push_back(RedisReply(reply_->element[i], false));
    }
  }
}

RedisReply::RedisReply(RedisReply&& other)
    : reply_(other.reply_),
      owns_(other.owns_),
      elements_(std::move(other.elements_)) {
  other.reply_ = nullptr;
}

RedisReply::~RedisReply() {
  if (reply_ != nullptr && owns_) {
    freeReplyObject(reply_);
  }
}

RedisReply& RedisReply::operator=(RedisReply&& other) {
  if (reply_ != nullptr) {
    freeReplyObject(reply_);
  }
  reply_ = other.reply_;
  owns_ = other.owns_;
  elements_ = std::move(other.elements_);
  other.reply_ = nullptr;
  return *this;
}

bool RedisReply::isString() const {
  return reply_ != nullptr && reply_->type == REDIS_REPLY_STRING;
}

bool RedisReply::isArray() const {
  return reply_ != nullptr && reply_->type == REDIS_REPLY_ARRAY;
}

bool RedisReply::isInteger() const {
  return reply_ != nullptr && reply_->type == REDIS_REPLY_INTEGER;
}

bool RedisReply::isNil() const {
  return reply_ != nullptr && reply_->type == REDIS_REPLY_NIL;
}

bool RedisReply::isStatus() const {
  return reply_ != nullptr && reply_->type == REDIS_REPLY_STATUS;
}

bool RedisReply::isError() const {
  return reply_ != nullptr && reply_->type == REDIS_REPLY_ERROR;
}

std::string RedisReply::string() const {
  ensureType(REDIS_REPLY_STRING);
  return std::string(reply_->str, reply_->len);
}

std::string_view RedisReply::stringv() const {
  ensureType(REDIS_REPLY_STRING);
  return std::string_view(reply_->str, reply_->len);
}

/// Convenience method for array replies consisting of strings.
std::vector<std::string_view> RedisReply::stringvs() const {
  ensureType(REDIS_REPLY_ARRAY);
  std::vector<std::string_view> res(elements_.size());
  for (auto i = 0U; i < elements_.size(); i++) {
    res[i] = elements_[i].stringv();
  }
  return res;
}

int64_t RedisReply::integer() const {
  ensureType(REDIS_REPLY_INTEGER);
  return reply_->integer;
}

std::string RedisReply::status() const {
  ensureType(REDIS_REPLY_STATUS);
  return std::string(reply_->str, reply_->len);
}

std::string_view RedisReply::statusv() const {
  ensureType(REDIS_REPLY_STATUS);
  return std::string_view(reply_->str, reply_->len);
}

std::string RedisReply::error() const {
  ensureType(REDIS_REPLY_ERROR);
  return std::string(reply_->str, reply_->len);
}

bool RedisReply::ok() const {
  ensureType(REDIS_REPLY_STATUS);
  return strcasecmp(reply_->str, "ok") == 0;
}

size_t RedisReply::size() const {
  ensureType(REDIS_REPLY_ARRAY);
  return elements_.size();
}

RedisReply& RedisReply::at(size_t index) {
  ensureType(REDIS_REPLY_ARRAY);
  return elements_.at(index);
}

RedisReply::Iterator RedisReply::begin() {
  ensureType(REDIS_REPLY_ARRAY);
  return elements_.begin();
}

RedisReply::Iterator RedisReply::end() {
  ensureType(REDIS_REPLY_ARRAY);
  return elements_.end();
}

void RedisReply::ensureType(int type) const {
  if (reply_ == nullptr) {
    throw std::runtime_error("No reply set");
  }
  if (type != REDIS_REPLY_ERROR && reply_->type == REDIS_REPLY_ERROR) {
    throw std::runtime_error("Error: " + std::string(reply_->str));
  }
  if (reply_->type != type) {
    static std::unordered_map<decltype(REDIS_REPLY_ARRAY), std::string>
        typeToStr = {{REDIS_REPLY_ARRAY, "ARRAY"},
                     {REDIS_REPLY_ERROR, "ERROR"},
                     {REDIS_REPLY_INTEGER, "INTEGER"},
                     {REDIS_REPLY_NIL, "NIL"},
                     {REDIS_REPLY_STATUS, "STATUS"},
                     {REDIS_REPLY_STRING, "STRING"}};
    throw std::runtime_error(fmt::format(
        "Expected reply of type {}, got {}",
        typeToStr[type],
        typeToStr[reply_->type]));
  }
  // ok!
}

} // namespace cpid
