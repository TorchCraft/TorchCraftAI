/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "zmqbufferedconsumer.h"

namespace cpid {
namespace detail {

RRClientWrapper::RRClientWrapper(
    size_t maxBacklogSize,
    std::vector<std::string> endpoints,
    std::shared_ptr<zmq::context_t> context)
    : maxBacklogSize_(maxBacklogSize),
      endpoints_(std::move(endpoints)),
      context_(std::move(context)) {}

void RRClientWrapper::updateEndpoints(std::vector<std::string> endpoints) {
  std::lock_guard<std::mutex> guard(mutex_);
  endpoints_ = std::move(endpoints);
  endpointsChanged_ = true;
}

void RRClientWrapper::perform(Action action, std::vector<char>&& msg) {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    if (rrc_ == nullptr) {
      rrc_ = std::make_unique<ReqRepClient>(
          [this](std::vector<char>&& request, void const* reply, size_t len) {
            // Queue for retry if reply is not affirmative
            if (detail::kConfirm.compare(
                    0,
                    detail::kConfirm.size(),
                    static_cast<char const*>(reply),
                    len) != 0) {
              VLOG_ALL(1) << "ZeroMQBufferedConsumer: got non-affirmative "
                             "reply, scheduling for retry";
              retries_.emplace(request);
            }
          },
          maxBacklogSize_,
          endpoints_,
          context_);
    }
    if (endpointsChanged_) {
      rrc_->updateEndpoints(endpoints_);
      endpointsChanged_ = false;
    }
  }

  auto pushRetries = [&] {
    auto n = retries_.size();
    for (auto i = 0U; i < n; i++) {
      auto retryMsg = std::move(retries_.front());
      retries_.pop();
      rrc_->request(std::move(retryMsg));
    }
  };

  switch (action) {
    case Action::Send: {
      pushRetries();
      rrc_->request(std::move(msg));
      break;
    }
    case Action::WaitForReplies: {
      rrc_->waitForReplies();
      break;
    }
    case Action::SendRetries: {
      pushRetries();
      break;
    }
    default:
      throw std::runtime_error("Unknown action");
  }
}

} // namespace detail
} // namespace cpid
