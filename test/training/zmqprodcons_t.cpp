/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "cpid/zmqbufferedconsumer.h"
#include "cpid/zmqbufferedproducer.h"

#include <autogradpp/serialization.h>
#include <common/rand.h>
#include <common/utils.h>

#include <fmt/format.h>
#include <glog/logging.h>
#include <zmq.hpp>

using namespace cpid;

CASE("zmqprodcons/e2e") {
  auto context = std::make_shared<zmq::context_t>();
  auto constexpr N = 20;
  ZeroMQBufferedProducer<std::string> prod(2, N * 2);
  ZeroMQBufferedConsumer<std::string> cons(1, 4, {prod.endpoint()}, context);

  std::atomic<size_t> ncharsSent{0};
  auto produceStrings = [&] {
    auto rengine = common::Rand::makeRandEngine<std::mt19937>();
    for (int i = 0; i < N; i++) {
      size_t sz = 1 + (rengine() % 1023);
      std::string s;
      for (size_t j = 0; j < sz; j++) {
        s += char('a' + (rengine() % 26));
      }
      ncharsSent += s.size();
      cons.enqueue(std::move(s));
    }
  };
  std::thread clT1(produceStrings);
  std::thread clT2(produceStrings);

  size_t ncharsRecv = 0;
  std::thread srvT([&] {
    for (int i = 0; i < N * 2; i++) {
      auto ed = prod.get();
      ncharsRecv += ed->size();
    }
  });

  clT1.join();
  clT2.join();
  srvT.join();
  EXPECT(ncharsSent.load() == ncharsRecv);
}

CASE("zmqcons/retries[.hide]") {
  auto context = std::make_shared<zmq::context_t>();
  auto constexpr nrounds = 4;
  auto const N = int(std::pow(2, nrounds));
  std::atomic<size_t> nrecv{0};
  std::atomic<size_t> ncharsAccepted{0};
  // Our server denies every other request
  ReqRepServer srv([&](void const* buf,
                       size_t len,
                       ReqRepServer::ReplyFn reply) {
    common::IMembuf mbuf(std::string_view(static_cast<char const*>(buf), len));
    common::zstd::istream is(&mbuf);
    cereal::BinaryInputArchive ar(is);
    std::string s;
    ar(s);
    // To make things simple, accept and ignore empty string requests
    if (s.length() == 0) {
      reply(detail::kConfirm.c_str(), detail::kConfirm.size());
      return;
    }

    nrecv++;
    if (nrecv % 2 == 0) {
      reply(detail::kDeny.c_str(), detail::kDeny.size());
      VLOG(0) << "reply deny";
    } else {
      reply(detail::kConfirm.c_str(), detail::kConfirm.size());
      ncharsAccepted += s.length();
      VLOG(0) << "reply ok, got " << s.length();
    }
  });
  ZeroMQBufferedConsumer<std::string> cons(0, N, {srv.endpoint()}, context);

  size_t nsent = 0;
  size_t ncharsSent{0};
  auto rengine = common::Rand::makeRandEngine<std::mt19937>();
  auto sendOneString = [&]() {
    size_t sz = 1 + (rengine() % 1023);
    std::string s;
    for (size_t j = 0; j < sz; j++) {
      s += char('a' + (rengine() % 26));
    }
    ncharsSent += s.size();
    cons.enqueue(std::move(s));
    nsent++;
  };

  // Send out N requests -- these will all be sent out as-is without retries.
  for (auto i = 0; i < N; i++) {
    sendOneString();
  }

  // The server rejects every other message so we need to trigger log(N)=nrounds
  // rounds of resends to get everything accepted.
  size_t expectedRecv = N;
  size_t pending = N;
  for (auto i = 0; i < nrounds; i++) {
    while (nrecv.load() < expectedRecv) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // We did not accept everything yet
    EXPECT(ncharsAccepted.load() < ncharsSent);

    // Trigger resends. On every round our resends are cut in half so we need to
    // enqueue a sufficient number of empty strings for this.
    for (int j = pending; j <= N; j++) {
      cons.enqueue(std::string());
    }
    pending /= 2;
    expectedRecv += pending;
  }

  while (nrecv.load() < expectedRecv) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  // Done!
  EXPECT(ncharsAccepted.load() == ncharsSent);
}

CASE("zmqprod/full_buffer") {
  auto context = std::make_shared<zmq::context_t>();
  auto constexpr QS = 10;
  ZeroMQBufferedProducer<std::string> prod(1, QS);
  ReqRepClient client(1, {prod.endpoint()}, context);

  // The producer has two queues so we should be able to get QS*2 affirmative
  // replies out of it for accessing any string.
  auto naccepted = 0;
  for (auto i = 0; naccepted < QS * 2 && i < QS * 10; i++) {
    common::OMembuf buf;
    std::string s = "hello";
    {
      common::zstd::ostream os(&buf);
      cereal::BinaryOutputArchive ar(os);
      ar(s);
    }
    auto reply = client.request(buf.takeData()).get();
    if (std::string_view(reply.data(), reply.size()) == detail::kConfirm) {
      naccepted++;
    }
  }
  EXPECT(naccepted == QS * 2);

  // Every other request will now result in a "deny" message
  for (auto i = 0; i < 5; i++) {
    common::OMembuf buf;
    std::string s = "hello";
    {
      common::zstd::ostream os(&buf);
      cereal::BinaryOutputArchive ar(os);
      ar(s);
    }
    auto reply = client.request(buf.takeData()).get();
    EXPECT(std::string_view(reply.data(), reply.size()) == detail::kDeny);
  }
}

namespace {
void bench(
    size_t numProds,
    size_t numThreadsP,
    size_t numCons,
    size_t numThreadsC,
    size_t msize = 512 * 1024) {
  using Data = std::vector<char>;
  // 4 threads for ZMQ, 8k max sockets
  auto context = std::make_shared<zmq::context_t>(4, 8192);

  std::atomic<size_t> nrecv{0};
  std::vector<std::string> endpoints;
  std::vector<std::thread> prodTs;
  std::vector<std::shared_ptr<ZeroMQBufferedProducer<Data>>> prods;
  for (auto i = 0U; i < numProds; i++) {
    prods.push_back(
        std::make_shared<ZeroMQBufferedProducer<Data>>(numThreadsP, 128));
    endpoints.push_back(prods.back()->endpoint());
    prodTs.emplace_back([&, prod = prods.back()] {
      while (true) {
        auto d = prod->get();
        if (!d.has_value()) {
          break;
        }
        nrecv += d.value().size();
      }
    });
  }

  std::atomic<bool> stop{false};
  std::vector<std::thread> conTs;
  for (auto i = 0U; i < numCons; i++) {
    // We're using a fairly small buffer here. Production is instant, but the
    // producer will accrue a future for every request that it sends. If the
    // buffer is full (which is what this test is aiming for) we'll have to wait
    // for up to buffer_size futures on every enqueue().
    conTs.emplace_back([&] {
      ZeroMQBufferedConsumer<Data> cons(numThreadsC, 128, endpoints, context);
      Data d(msize);
      auto rengine = common::Rand::makeRandEngine<std::mt19937>();
      auto dist = std::uniform_int_distribution<char>(
          std::numeric_limits<char>::min(), std::numeric_limits<char>::max());
      // Half zeros, half random data
      std::generate(
          d.begin() + msize / 2, d.end(), [&] { return dist(rengine); });
      while (!stop.load()) {
        cons.enqueue(d);
        cons.enqueue(d);
        cons.enqueue(d);
        cons.enqueue(d);
      }
    });
  }

  // Warmup
  auto start = std::chrono::steady_clock::now();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  for (int i = 0; i < 10; i++) {
    auto oldnrecv = nrecv.load();
    auto tstart = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto nbytes = nrecv.load() - oldnrecv;
    auto elapsed = std::chrono::steady_clock::now() - tstart;
    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    std::cerr << fmt::format(
                     "{:.1f}s {:.1f} GBytes {:.1f} Gbits/s",
                     double(ms) / 1000,
                     double(nbytes) / 1e9,
                     double(nbytes) / (1.25e+8 * double(ms) / 1e3))
              << std::endl;
  }
  auto elapsed = std::chrono::steady_clock::now() - start;
  auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
  auto nbytes = nrecv.load();
  std::cerr << fmt::format(
                   "TOTAL {:.1f}s {:.1f} GBytes {:.1f} Gbits/s",
                   double(ms) / 1000,
                   double(nbytes) / 1e9,
                   double(nbytes) / (1.25e+8 * double(ms) / 1e3))
            << std::endl;

  std::cerr << "Trying to shut down" << std::endl;
  stop.store(true);
  for (auto& th : conTs) {
    th.join();
  }
  for (auto& p : prods) {
    p->stop();
  }
  for (auto& th : prodTs) {
    th.join();
  }
}
} // namespace

CASE("zmqprodcons/bench/fanin[hide]") {
  auto numCores = std::thread::hardware_concurrency();
  EXPECT((bench(1, 8, numCores, 1, 1024 * 1024), true));
}

CASE("zmqprodcons/bench/1v1[hide]") {
  EXPECT((bench(1, 8, 1, 8, 1024 * 1024), true));
}

CASE("zmqprodcons/bench/fanout[hide]") {
  auto numCores = std::thread::hardware_concurrency();
  EXPECT((bench(numCores, 1, 1, 8, 1024 * 1024), true));
}

CASE("zmqprodcons/bench/nvn[hide]") {
  auto numCores = std::thread::hardware_concurrency();
  EXPECT((bench(numCores / 2, 1, numCores / 2, 1, 1024 * 1024), true));
}

CASE("zmqprodcons/bench/nv8n[hide]") {
  auto numProds = std::thread::hardware_concurrency() / 10;
  EXPECT((bench(numProds, 2, numProds * 8, 1, 1024 * 1024), true));
}
