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
  ZeroMQBufferedProducer<std::string> prod(2, N * 2, std::string(), context);
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

CASE("zmqprodcons/e2e/fair") {
  auto context = std::make_shared<zmq::context_t>();
  auto constexpr N = 20;
  ZeroMQBufferedProducer<std::string> prod1(2, N * 2, std::string(), context);
  ZeroMQBufferedProducer<std::string> prod2(2, N * 2, std::string(), context);
  ZeroMQBufferedConsumer<std::string> cons1(
      1, 4, {prod1.endpoint(), prod2.endpoint()}, context);
  ZeroMQBufferedConsumer<std::string> cons2(
      1, 4, {prod1.endpoint(), prod2.endpoint()}, context);

  std::atomic<size_t> ncharsSent{0};
  auto produceStrings = [&](ZeroMQBufferedConsumer<std::string>* cons,
                            size_t sz) {
    auto rengine = common::Rand::makeRandEngine<std::mt19937>();
    for (int i = 0; i < N; i++) {
      std::string s;
      for (size_t j = 0; j < sz; j++) {
        s += char('a' + (rengine() % 26));
      }
      ncharsSent += s.size();
      cons->enqueue(std::move(s));
    }
  };
  std::thread clT1(produceStrings, &cons1, 1024);
  std::thread clT2(produceStrings, &cons2, 2048);

  size_t ncharsRecv1 = 0;
  size_t ncharsRecv2 = 0;
  auto fetchStrings = [&](ZeroMQBufferedProducer<std::string>* prod,
                          size_t* dest) {
    for (int i = 0; i < N; i++) {
      auto ed = prod->get();
      *dest += ed->size();
    }
  };
  std::thread srvT1(fetchStrings, &prod1, &ncharsRecv1);
  std::thread srvT2(fetchStrings, &prod2, &ncharsRecv2);

  clT1.join();
  clT2.join();
  srvT1.join();
  srvT2.join();
  EXPECT(ncharsSent.load() == ncharsRecv1 + ncharsRecv2);
  EXPECT(ncharsRecv1 == ncharsRecv2);
}

CASE("zmqprodcons/prod/full_buffer") {
  auto context = std::make_shared<zmq::context_t>();
  auto constexpr N = 20;
  // Small producer queue will trigger negative replies and retries by the
  // consumer.
  auto constexpr QS = 10;
  ZeroMQBufferedProducer<std::string> prod(1, QS, std::string(), context);
  ZeroMQBufferedConsumer<std::string> cons(0, N, {prod.endpoint()}, context);

  // Enqueue messages. Some of them will be denied since the producer queue runs
  // full. The consumer will keep them around for retries.
  size_t ncharsSentFirstQS{0};
  size_t ncharsSent{0};
  auto rengine = common::Rand::makeRandEngine<std::mt19937>();
  int nsent = 0;
  auto sendOneString = [&]() {
    size_t sz = 1 + (rengine() % 1023);
    std::string s;
    for (size_t j = 0; j < sz; j++) {
      s += char('a' + (rengine() % 26));
    }
    if (nsent < QS) {
      ncharsSentFirstQS += s.size();
    }
    ncharsSent += s.size();
    cons.enqueue(std::move(s));
    nsent++;
  };
  for (int i = 0; i < N; i++) {
    sendOneString();
  }

  // Finally, grab all messages. The consumer will not re-queue messages
  // automatically unless we enqueue further ones.
  size_t ncharsRecv = 0;
  for (int i = 0; i < N; i++) {
    if (i == QS) {
      EXPECT(ncharsSentFirstQS == ncharsRecv);
      // We should have some room now -- flush pending retries
      cons.flush();
    }
    auto ed = prod.get();
    ncharsRecv += ed->size();
  }

  EXPECT(ncharsSent == ncharsRecv);
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
    prods.push_back(std::make_shared<ZeroMQBufferedProducer<Data>>(
        numThreadsP, 128, std::string(), context));
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
