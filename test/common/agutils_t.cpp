/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "common/autograd.h"
#include "cuda_runtime_api.h"

#include <prettyprint/prettyprint.hpp>

CASE("common/repeat2d") {
  torch::Tensor var = torch::randn({16});
  torch::Tensor out = common::repeat2d(var, {7, 8});
  EXPECT(out.sizes().vec() == std::vector<int64_t>({16, 7, 8}));
  EXPECT(out.slice(1, 0, 1).eq(out.slice(1, 1, 2)).all().item<int32_t>());
  EXPECT(out.slice(1, 2, 3).slice(2, 3, 4).allclose(
      out.slice(1, 3, 4).slice(2, 5, 6)));

  EXPECT_THROWS(common::repeat2d(var.view({1, -1}), {7, 8}));
}

CASE("common/scatterSum2d/simple") {
  auto dataO = torch::empty({1, 10, 4}).fill_(1.0f);
  auto positionsO = torch::empty({1, 10, 2}, torch::kI32);
  for (auto i = 0U; i < positionsO.size(1); i++) {
    positionsO[0][i][0] = int(i);
    positionsO[0][i][1] = int(i * 2);
  }

  auto run = [&](auto const& device) {
    auto data = dataO.to(device);
    auto positions = positionsO.to(device);

    torch::Tensor res;
    EXPECT((res = common::scatterSum2d(positions, data, {20, 20}), true));
    EXPECT(res.sizes().vec() == std::vector<int64_t>({1, 4, 20, 20}));
    res = res[0].permute({1, 2, 0}); // use (Y,X,C) for easier testing
    EXPECT(res.sum().allclose(data.sum()));
    EXPECT(res[1][6].sum().item<float>() == 0.0f);
    EXPECT(res[2][4].sum().item<float>() == 4.0f); // 4-dim data
  };

  run(torch::kCPU);
  if (common::gpuAvailable()) {
    run(torch::kCUDA);
  }
}

CASE("common/scatterSum2d/pooling") {
  // Batch size 3: second element is empty, third element is not fully set
  auto dataO = torch::empty({3, 10, 4}).fill_(1.0f);
  auto positionsO = torch::empty({3, 10, 2}, torch::kI32).fill_(-1);
  float nel = 0;
  for (auto i = 0U; i < 4U; i++) {
    positionsO[0][i][0] = 3;
    positionsO[0][i][1] = 4;
    positionsO[2][i][0] = 3;
    positionsO[2][i][1] = 4;
    nel += 2;
  }
  for (auto i = 4U; i < positionsO.size(1); i++) {
    positionsO[0][i][0] = int(i);
    positionsO[0][i][1] = int(i * 2);
    nel += 1;
  }

  auto run = [&](auto const& device) {
    auto data = dataO.to(device);
    auto positions = positionsO.to(device);

    torch::Tensor res;
    torch::Tensor res2;
    EXPECT((res = common::scatterSum2d(positions, data, {20, 20}), true));
    EXPECT(res.sizes().vec() == std::vector<int64_t>({3, 4, 20, 20}));
    res = res.permute({0, 2, 3, 1}); // use (Y,X,C) for easier testing
    EXPECT(res.sum().item<float>() == nel * 4);
    EXPECT(res[0][1][6].sum().item<float>() == 0.0f);
    EXPECT(res[0][4][8].sum().item<float>() == 4.0f); // 4-dim data
    EXPECT(res[0][3][4].sum().item<float>() == 16.0f); // 4 pooled elements
    EXPECT(res[1].sum().item<float>() == 0.0f); // no item here
    EXPECT(res[2][1][6].sum().item<float>() == 0.0f);
    EXPECT(res[2][4][8].sum().item<float>() == 0.0f); // no item here
    EXPECT(res[2][3][4].sum().item<float>() == 16.0f); // 4 pooled elements
  };

  run(torch::kCPU);
  if (common::gpuAvailable()) {
    run(torch::kCUDA);
  }
}

CASE("common/scatterSum2d/batched") {
  ag::tensor_list datas, indices;
  for (auto i = 0U; i < 3; i++) {
    datas.emplace_back(torch::ones({i + 1, 4}));
    auto inds = torch::empty({i + 1, 2}, torch::kI64);
    for (auto j = 0U; j <= i; j++) {
      inds[j][0] = int(i);
      inds[j][1] = int(2 * i);
    }
    indices.emplace_back(std::move(inds));
  }
  auto dataBatch = common::makeBatch(datas, -1);
  auto indexBatch = common::makeBatch(indices, -1);

  torch::Tensor res;
  EXPECT((res = common::scatterSum2d(indexBatch, dataBatch, {10, 10}), true));
  EXPECT(res.size(0) == 3);
  EXPECT(res.size(1) == 4);
  EXPECT(res.size(2) == 10);
  EXPECT(res.size(3) == 10);

  EXPECT(res[0].sum().item<float>() == 4.f);
  EXPECT(res[1].sum().item<float>() == 8.f);
  EXPECT(res[2].sum().item<float>() == 12.f);
}

CASE("common/scatterSum2d/timed[.hidden]") {
  // This test case simply measures the perf of scatterSum on a few cases
  auto compareBatched =
      [](torch::Tensor indices, torch::Tensor values, int H, int W) {
        auto const constexpr reps = 1000;
        using hires_clock = std::chrono::steady_clock;
        auto start = hires_clock::now();
        for (auto i = 0U; i < reps; i++) {
          common::scatterSum2d(indices, values, {H, W});
        }
        cudaDeviceSynchronize();
        auto end = hires_clock::now();
        std::chrono::duration<double, std::milli> duration = end - start;
        VLOG(0) << "ScatterSum: " << duration.count() / 1000. / reps;
      };

  auto compare =
      [&compareBatched](
          torch::Tensor indices, torch::Tensor values, int H, int W) {
        VLOG(0) << "Batch Size 1";
        auto singleBatchIndices = indices.unsqueeze(0);
        auto singleBatchValues = values.unsqueeze(0);
        compareBatched(singleBatchIndices, singleBatchValues, H, W);

        VLOG(0) << "Batch Size 3";
        auto multiBatchIndices = singleBatchIndices.expand({3, -1, -1});
        auto multiBatchValues = singleBatchValues.expand({3, -1, -1});
        compareBatched(multiBatchIndices, multiBatchValues, H, W);
      };

  auto run = [&](int n, auto const& device) {
    auto opts = torch::TensorOptions(device);
    VLOG(0) << "Running with 512 positions of " << n << " elems each\n";
    {
      auto indices = torch::zeros({256, 2}, opts.dtype(torch::kLong));
      auto values = torch::randn({256, n}, opts);
      VLOG(0) << "All collisions on 16x16x" << n;
      compare(indices, values, 16, 16);
      VLOG(0) << "All collisions on 128x128x" << n;
      compare(indices, values, 128, 128);
    }

    {
      auto indices =
          at::stack(
              {torch::arange(0, 16, opts.dtype(at::kLong)).repeat({16, 1}),
               torch::arange(0, 16, opts.dtype(at::kLong)).repeat({16, 1}).t()},
              2)
              .contiguous()
              .view({-1, 2});
      indices = indices.repeat({2, 1});
      auto values = torch::randn({512, n}, opts);
      VLOG(0) << "Few (2) collisions on 16x16x" << n;
      compare(indices, values, 16, 16);
      VLOG(0) << "Few (2) collisions on 128x128x" << n;
      compare(indices, values, 128, 128);
    }

    {
      auto indices =
          at::stack(
              {torch::arange(0, 4, opts.dtype(at::kLong)).repeat({8, 1}),
               torch::arange(0, 8, opts.dtype(at::kLong)).repeat({4, 1}).t()},
              2)
              .contiguous()
              .view({-1, 2});
      indices = indices.repeat({16, 1});
      auto values = torch::randn({512, n}, opts);
      VLOG(0) << "Some (16) collisions on 16x16x" << n;
      compare(indices, values, 16, 16);
      VLOG(0) << "Some (16) collisions on 128x128x" << n;
      compare(indices, values, 128, 128);
    }

    {
      auto indices =
          at::stack(
              {torch::arange(0, 4, opts.dtype(at::kLong)).repeat({2, 1}),
               torch::arange(0, 2, opts.dtype(at::kLong)).repeat({4, 1}).t()},
              2)
              .contiguous()
              .view({-1, 2});
      indices = indices.repeat({4, 1});
      auto values = torch::randn({32, n}, opts);
      VLOG(0) << "Some (4) collisions with 32 positions on 16x16x" << n;
      compare(indices, values, 16, 16);
      VLOG(0) << "Some (4) collisions with 32 positions on 128x128x" << n;
      compare(indices, values, 128, 128);
    }
  };

  run(16, torch::kCPU);
  run(128, torch::kCPU);
  if (common::gpuAvailable()) {
    VLOG(0) << "Running on GPU\n";
    run(16, torch::kCUDA);
    run(128, torch::kCUDA);
  }
}

CASE("common/makeBatch") {
  ag::tensor_list lst;
  lst.emplace_back(torch::empty({6, 2}));
  lst.emplace_back(torch::empty({5, 2}));
  lst.emplace_back(torch::empty({7, 3}));
  auto batch = common::makeBatch(lst);
  EXPECT(batch.size(0) == 3);
  EXPECT(batch.size(1) == 7);
  EXPECT(batch.size(2) == 3);
  EXPECT(batch[0][6][2].item<float>() == 0);
}

CASE("common/pad2d") {
  int l = 1;
  int r = 2;
  int t = 3;
  int b = 4;
  int s = 5; // size of input
  torch::Tensor var = torch::ones({s, s, s});
  torch::Tensor out = common::pad2d(var, {l, r, t, b});
  EXPECT(out.sizes().vec() == std::vector<int64_t>({s, s + t + b, s + l + r}));
  EXPECT(out.sum().item<float>() == float(s * s * s));
  EXPECT(out.slice(1, 0, t).sum().item<float>() == 0.0f);
  EXPECT(out.slice(1, t + s, t + s + b - 1).sum().item<float>() == 0.0f);
  EXPECT(out.slice(2, 0, l).sum().item<float>() == 0.0f);
  EXPECT(out.slice(2, l + s, l + s + r - 1).sum().item<float>() == 0.0f);

  EXPECT_THROWS(common::pad2d(var, {1, 2, 3})); // not 4 paddings
  EXPECT_THROWS(common::pad2d(var.view({1, 2}), {1, 1, 1, 1})); // not 3D in
}

CASE("common/padNd") {
  auto d = 3U; // number of dimensions
  auto s = 5U; // size along a dimension
  auto p = 2U; // how many to pad before and after along a dimension

  auto input = torch::ones(std::vector<int64_t>(d, s), torch::kI64);
  auto output = common::padNd(input, std::vector<int64_t>(2 * d, p));

  // Check size
  EXPECT(output.sizes().vec() == std::vector<int64_t>(d, s + 2 * p));

  // Check sum is preserved
  EXPECT(output.sum().item<int64_t>() == input.sum().item<int64_t>());

  // Check corner are zeros
  for (auto i = 0U; i < d; i++) {
    EXPECT(output.slice(i, 0, p).sum().item<int64_t>() == 0);
    EXPECT(output.slice(i, p + s, p + 2 * s).sum().item<int64_t>() == 0);
  }
}

CASE("common/upsample") {
  std::map<common::UpsampleMode, int> m = {
      {common::UpsampleMode::Linear, 1},
      {common::UpsampleMode::Bilinear, 2},
      {common::UpsampleMode::Trilinear, 3},
  };

  for (auto& x : m) {
    auto mode = x.first;
    auto d = x.second;

    std::vector<int64_t> size(d + 2, 2);
    size[0] = 1;
    size[1] = 1;

    torch::Tensor in = torch::zeros(size);
    in.view({-1})[-1] = 1 << d; // 2 ** d

    std::vector<int64_t> outsize(d, 3);
    torch::Tensor out = common::upsample(in, mode, outsize)[0][0];

    EXPECT(out.sizes().vec() == outsize);
    torch::Tensor middle = out;
    for (auto i = 0; i < d; i++) {
      middle = middle[1];
    }
    EXPECT(middle.item<float>() == 1.0f);

    outsize = std::vector<int64_t>(d, 4);
    torch::Tensor out_nearest_size =
        common::upsample(in, common::UpsampleMode::Nearest, outsize)[0][0];
    torch::Tensor out_nearest_scft =
        common::upsample(in, common::UpsampleMode::Nearest, 2)[0][0];

    EXPECT(
        out_nearest_size.sum().item<int32_t>() == 1 << 2 * d); // (2 ** d) ** 2;
    EXPECT(
        out_nearest_scft.sum().item<int32_t>() == 1 << 2 * d); // (2 ** d) ** 2;
  }
}

CASE("common/(un)squash") {
  auto x = torch::zeros({1, 2, 3, 4, 5}, torch::kI64);

  x = common::squash(x, 1, 3);
  EXPECT(x.sizes().vec() == std::vector<int64_t>({1, 2 * 3 * 4, 5}));

  x = common::unsquash(x, 1, {2, 3, -1});
  EXPECT(x.sizes().vec() == std::vector<int64_t>({1, 2, 3, 4, 5}));
}

CASE("common/crossEntropyLoss") {
  auto n = 10U;
  auto c = 10U;
  auto h = 10U;
  auto w = 10U;

  auto generatePredict = [&]() { return torch::randn({n, c, h, w}); };
  auto generateTargetDeterministic = [&]() {
    return torch::multinomial(torch::ones({c}), n * h * w, true)
        .view({n, h, w});
  };
  auto generateTarget = [&]() { return at::softmax(generatePredict(), 1); };
  auto generateMask = [&]() {
    return torch::multinomial(torch::ones({2}), n * h * w, true)
        .view({n, 1, h, w})
        .to(at::kFloat);
  };

  using Reduction = Reduction::Reduction;
  auto themCE = [&](torch::Tensor predict,
                    torch::Tensor target,
                    torch::Tensor mask,
                    Reduction reduction) {
    auto reduce = reduction != Reduction::None;
    if (mask.defined() && reduce) {
      throw std::runtime_error("ATen doesn't support masked NLL loss");
    }

    auto predictSoftmax = at::log_softmax(predict, 1);

    torch::Tensor loss;
    if (target.ndimension() == 3) {
      loss = at::nll_loss2d(predictSoftmax, target, {}, reduction, -1);
    } else {
      if (reduce) {
        throw std::runtime_error("not implemented because this is a test");
      }

      loss = torch::zeros({n, h, w});

      // Weighted sum of deterministic losses.
      for (auto i = 0U; i < c; i++) {
        auto targetI =
            static_cast<int64_t>(i) * torch::ones({n, h, w}, torch::kI64);
        auto lossI = at::nll_loss2d(predictSoftmax, targetI, {}, reduction, -1);
        loss += target.select(1, i) * lossI;
      }
    }

    if (mask.defined()) {
      loss *= mask.squeeze(1);
    }
    return loss;
  };
  auto usCE = [&](torch::Tensor predict,
                  torch::Tensor target,
                  torch::Tensor mask,
                  Reduction reduction) {
    if (target.ndimension() == 3) {
      // Convert deterministic target to probability distributions.
      auto targetExtensive = torch::zeros({n, c, h, w});
      for (auto i = 0U; i < n; i++) {
        for (auto j = 0U; j < h; j++) {
          for (auto k = 0U; k < w; k++) {
            auto c = target[i][j][k].item<int64_t>();
            targetExtensive[i][c][j][k] = 1;
          }
        }
      }
      target = targetExtensive;
    }

    auto loss =
        common::crossEntropyLoss(predict, 1, target, {}, mask, reduction)
            .squeeze();
    return loss;
  };

  auto checkEqual = [&](torch::Tensor us, torch::Tensor them) {
    auto absError = (us - them).abs();
    auto norm = them.clone();
    common::zerosToOnes_(norm);
    auto relError = (absError / norm).max().item<float>();
    EXPECT(relError <= 1e-5);
  };
  auto checkDeterministic = [&](Reduction reduction) {
    auto predict = generatePredict();
    auto target = generateTargetDeterministic();

    auto them = themCE(predict, target, {}, reduction);
    auto us = usCE(predict, target, {}, reduction);
    checkEqual(us, them);
  };
  auto checkMask = [&](Reduction reduction) {
    auto predict = generatePredict();
    auto target = generateTarget();
    auto mask = generateMask();

    auto before = usCE(predict, target, mask, reduction);

    // Mess with masked predictions and targets.
    for (auto i = 0U; i < n; i++) {
      for (auto j = 0U; j < h; j++) {
        for (auto k = 0U; k < w; k++) {
          if (!mask[i][0][j][k].is_nonzero()) {
            for (auto l = 0U; l < c; l++) {
              predict[i][l][j][k] = 100;
              target[i][l][j][k] = -1;
            }
          }
        }
      }
    }

    auto after = usCE(predict, target, mask, reduction);

    checkEqual(after, before);
  };
  auto checkNonDeterministic = [&](Reduction reduction) {
    auto predict = generatePredict();
    auto target = generateTarget();

    auto them = themCE(predict, target, {}, reduction);
    auto us = usCE(predict, target, {}, reduction);
    checkEqual(us, them);
  };

  for (auto r : {Reduction::None, Reduction::Mean, Reduction::Sum}) {
    checkDeterministic(r);
    checkMask(r);
  }
  checkNonDeterministic(Reduction::None);
}

CASE("common/maskedSoftmax") {
  auto input = torch::ones({10});
  auto binMask = torch::zeros({10});
  auto mask = torch::zeros({10});
  int dim = 0;
  float clampEpsilon = 0.00001;

  auto expected = torch::zeros({10});

  // All elements are masked out
  auto y = common::maskedSoftmax(input, binMask, dim, clampEpsilon);
  auto yw = common::weightedMaskedSoftmax(input, mask, dim, clampEpsilon);
  expected.fill_(clampEpsilon);
  EXPECT(y.eq(expected).sum().item<int32_t>() == y.size(0));
  EXPECT(yw.eq(expected).sum().item<int32_t>() == y.size(0));

  // All but one element is masked out
  clampEpsilon = 0;
  binMask[0] = 1.0;
  mask[0] = 2.0;
  y = common::maskedSoftmax(input, binMask, dim, clampEpsilon);
  yw = common::weightedMaskedSoftmax(input, mask, dim, clampEpsilon);
  expected.fill_(0.0);
  expected[0] = 1.0;
  EXPECT(y.eq(expected).sum().item<int32_t>() == y.size(0));
  EXPECT(yw.eq(expected).sum().item<int32_t>() == y.size(0));

  // Two elements masked out
  binMask[1] = 1.0;
  mask[1] = 1.0;
  y = common::maskedSoftmax(input, binMask, dim, clampEpsilon);
  yw = common::weightedMaskedSoftmax(input, mask, dim, clampEpsilon);
  expected[0] = 0.5;
  expected[1] = 0.5;
  EXPECT(y.eq(expected).sum().item<int32_t>() == y.size(0));

  expected[0] = 2.0 / 3;
  expected[1] = 1.0 / 3;
  EXPECT(yw.eq(expected).sum().item<int32_t>() == y.size(0));

  // No elements are masked out
  binMask.fill_(1.0);
  mask.fill_(2.0);
  y = common::maskedSoftmax(input, binMask, dim, clampEpsilon);
  yw = common::weightedMaskedSoftmax(input, mask, dim, clampEpsilon);
  expected.fill_(0.1);
  EXPECT(y.eq(expected).sum().item<int32_t>() == y.size(0));
  EXPECT(yw.eq(expected).sum().item<int32_t>() == y.size(0));

  // Large unmasked values
  input.fill_(1000.0);
  input[0] = 100000.0;
  y = common::maskedSoftmax(input, binMask, dim, clampEpsilon);
  yw = common::weightedMaskedSoftmax(input, mask, dim, clampEpsilon);
  expected.fill_(0.0);
  expected[0] = 1.0;
  EXPECT(y.eq(expected).sum().item<int32_t>() == y.size(0));
  EXPECT(yw.eq(expected).sum().item<int32_t>() == y.size(0));

  // Large negative unmasked values
  input.fill_(-100000.0);
  input[0] = -1000.0;
  y = common::maskedSoftmax(input, binMask, dim, clampEpsilon);
  yw = common::weightedMaskedSoftmax(input, mask, dim, clampEpsilon);
  expected.fill_(0.0);
  expected[0] = 1.0;
  EXPECT(y.eq(expected).sum().item<int32_t>() == y.size(0));
  EXPECT(yw.eq(expected).sum().item<int32_t>() == y.size(0));

  // Large masked values
  input.fill_(1.0);
  input[0] = 1000000.0;
  mask[0] = 0.0;
  mask[1] = 0.0;
  binMask[0] = 0.0;
  binMask[1] = 0.0;
  y = common::maskedSoftmax(input, binMask, dim, clampEpsilon);
  yw = common::weightedMaskedSoftmax(input, mask, dim, clampEpsilon);
  expected.fill_(0.125);
  expected[0] = 0.0;
  expected[1] = 0.0;
  EXPECT(y.eq(expected).sum().item<int32_t>() == y.size(0));
  EXPECT(yw.eq(expected).sum().item<int32_t>() == y.size(0));
}

CASE("common/maskedMax") {
  auto x = torch::tensor({1, 2, 3, 4, 5, 6}).view({2, 3});
  auto mask = torch::tensor({0, 1, 0, 1, 0, 1}).view({2, 3});
  auto dim = 1;
  auto got = common::maskedMax(x, mask, dim, false);

  auto expectedMax = torch::tensor({2, 6});
  auto expectedArgmax = torch::tensor({1, 2}, at::kLong);
  EXPECT(std::get<0>(got).equal(expectedMax));
  EXPECT(std::get<1>(got).equal(expectedArgmax));
}

CASE("common/assertSize") {
  at::IntList sizes{1, 2};
  auto good = torch::ones({1, 2});
  auto wrongDimensions = torch::ones(2);
  auto wrongSizes = torch::ones({1, 3});
  common::assertSize("good", good, sizes);
  EXPECT_THROWS(common::assertSize("wrongDimensions", wrongDimensions, sizes));
  EXPECT_THROWS(common::assertSize("wrongSizes", wrongSizes, sizes));
}

CASE("common/WeightSummary") {
  // Linear weights are [ Tensor[1, N] weights, Tensor[] bias ]

  torch::NoGradGuard guard;
  auto apple = ag::Linear(3, 1).make();
  auto banana = ag::Linear(4, 1).make();
  auto cherry = ag::Linear(5, 1).make();

  apple->parameters()[0][0][0].zero_();
  apple->parameters()[0][0][1].zero_().add_(3);
  apple->parameters()[0][0][2].zero_().add_(4);
  for (auto& parameter : banana->parameters()) {
    parameter.zero_().add_(2);
  }
  for (auto& parameter : cherry->parameters()) {
    parameter.zero_();
  }
  cherry->parameters()[0][0][4].add_(std::numeric_limits<float>::quiet_NaN());

  apple->parameters()[1].zero_();
  banana->parameters()[1].zero_().add_(100);
  cherry->parameters()[1].zero_();

  auto appleSummary = common::WeightSummary(*apple);
  auto bananaSummary = common::WeightSummary(*banana);
  auto cherrySummary = common::WeightSummary(*cherry);

  EXPECT(appleSummary.weights == 4);
  EXPECT(bananaSummary.weights == 5);
  EXPECT(cherrySummary.weights == 6);

  EXPECT(appleSummary.zeroes == 2);
  EXPECT(bananaSummary.zeroes == 0);
  EXPECT(cherrySummary.zeroes == 5);

  EXPECT(appleSummary.nans == 0);
  EXPECT(bananaSummary.nans == 0);
  EXPECT(cherrySummary.nans == 1);

  float appleNorm1 = (3 + 4) / 4.;
  float appleNorm2 = sqrt(3 * 3 + 4 * 4) / 4.;
  float bananaNorm1 = (2 * 4 + 100) / 5.;
  float bananaNorm2 = sqrt(2 * 2 * 4 + 100 * 100) / 5.;
  constexpr float epsilon = 0.001;
  EXPECT(abs(appleSummary.norm1 - appleNorm1) < epsilon);
  EXPECT(abs(appleSummary.norm2 - appleNorm2) < epsilon);
  EXPECT(abs(bananaSummary.norm1 - bananaNorm1) < epsilon);
  EXPECT(abs(bananaSummary.norm2 - bananaNorm2) < epsilon);
  EXPECT(std::isnan(cherrySummary.norm1));
  EXPECT(std::isnan(cherrySummary.norm2));
}

CASE("common/ConvBlock[hide]") {
  std::vector<bool> deconv{false, true};
  std::vector<bool> gated{false, true};
  std::vector<bool> residual{true, false};
  std::vector<bool> bottleNeck{true, false};
  std::vector<bool> batchN{true, false};
  std::vector<int> kernel{1, 3, 5};
  std::vector<int> stride{1, 2, 3, 4, 5};
  std::vector<int> dilation{1, 2, 3};
  std::vector<int> nLayers{1, 2, 3};

  // check various option combinations
  for (bool dec : deconv) {
    for (bool res : residual) {
      for (bool bot : bottleNeck) {
        for (bool bn : batchN) {
          for (int k : kernel) {
            for (int s : stride) {
              for (int d : dilation) {
                for (int l : nLayers) {
                  for (bool g : gated) {
                    if (l == 1 && bot)
                      continue;
                    VLOG(4)
                        << " deconv " << dec << " residual " << res
                        << " batchnorm " << bn << " kernel " << k << " stride "
                        << s << " dilation " << d << " layers " << l;
                    auto block = common::ConvBlock()
                                     .nInFeats(32)
                                     .nOutFeats(64)
                                     .deconv(dec)
                                     .kernelSize(k)
                                     .stride(s)
                                     .dilation(d)
                                     .residual(res)
                                     .batchNorm(bn)
                                     .bottleNeck(bot)
                                     .nLayers(l)
                                     .gated(g)
                                     .make();

                    torch::Tensor test = torch::zeros({5, 32, 10, 11});

                    auto out = block->forward({test})[0];
                    EXPECT(out.sizes().size() == 4U);
                    EXPECT(out.size(0) == 5);
                    EXPECT(out.size(1) == 64);
                    if (dec) {
                      EXPECT(out.size(2) == (10 - 1) * s + 1);
                      EXPECT(out.size(3) == (11 - 1) * s + 1);
                    } else {
                      EXPECT(out.size(2) == (10 - 1) / s + 1);
                      EXPECT(out.size(3) == (11 - 1) / s + 1);
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

CASE("common/extendIndex") {
  auto opts = torch::TensorOptions(at::kLong);
  auto y = torch::arange(3, opts).repeat({3, 1}).view({3, 1, 3});
  auto x = common::extendIndex(y, 1, 3);
  std::vector<int64_t> sizes = {3, 3, 3};
  EXPECT(x.sizes().vec() == sizes);
  EXPECT(x.sum().item<int64_t>() == y.numel());
}

CASE("common/maskedCopy_") {
  auto x = std::vector<float>{12, 12, 12};
  auto y = std::vector<float>{1, 2, 3};
  auto m = std::vector<char>{0, 1, 0};
  auto z = std::vector<float>{12, 2, 12};

  auto floatTensor = [](std::vector<float>& t) {
    return torch::from_blob(t.data(), t.size());
  };
  auto byteTensor = [](std::vector<char>& t) {
    return torch::from_blob(t.data(), t.size(), at::kByte);
  };

  auto X = floatTensor(x);
  common::maskedCopy_(X, byteTensor(m), floatTensor(y));
  EXPECT(X.equal(floatTensor(z)));
}

CASE("common/takeNd") {
  for (auto device : {at::kCUDA, at::kCPU}) {
    if (!common::gpuAvailable() && device == at::kCUDA) {
      continue;
    }

    auto options = torch::TensorOptions(device);
    auto x = torch::arange(6, options).view({3, 2});
    auto index =
        torch::tensor({2, 1, 1, 0}, options.dtype(at::kLong)).view({2, 2});

    auto y = common::takeNd(x, index);
    auto expected = torch::tensor({5, 2}, options.dtype(at::kFloat));
    EXPECT(y.equal(expected));
  }
}

CASE("common/putNd_") {
  auto x = torch::arange(6).view({3, 2});
  auto index = torch::tensor({2, 1, 1, 0}, at::kLong).view({2, 2});
  auto source = torch::tensor({7, 8}, at::kFloat);

  common::putNd_(x, index, source);
  auto expected = torch::tensor({0, 1, 8, 3, 4, 7}, at::kFloat).view({3, 2});
  EXPECT(x.equal(expected));
}

CASE("common/indexMean") {
  auto source = 2 * torch::arange(3 * 10 * 3, at::kLong).view({3, 10, 3});
  auto index = torch::tensor({0, 1, 2, 0, 1, 2, 0, 1, 2, 0}, at::kLong);
  auto got = common::indexMean(3, 1, index, source);

  auto expected = source.select(1, 0).unsqueeze(1).repeat({1, 3, 1}); // 3x3x3
  expected.select(1, 0).add_(3 * 9);
  expected.select(1, 1).add_(3 * 8);
  expected.select(1, 2).add_(3 * 10);

  EXPECT(got.equal(expected));
}
using namespace common;

CASE("common/EncoderDecoder[hide]") {
  std::vector<bool> residual{true, false};
  std::vector<bool> bottleNeck{true, false};
  std::vector<bool> batchN{true, false};
  std::vector<int> kernel{1, 3, 5};
  std::vector<int> stride{1, 2, 3};
  std::vector<int> nLayers{1, 2, 3};
  std::vector<int> nBlocks{1, 2, 3};
  std::vector<ConcatType> concat{
      ConcatType::None, ConcatType::Input, ConcatType::Mirror};
  std::vector<UpsamplingType> upsample{
      UpsamplingType::None, UpsamplingType::Bilin, UpsamplingType::Deconv};
  std::vector<DecodeType> decode{
      DecodeType::None, DecodeType::Conv, DecodeType::Deconv};
  std::vector<DilationScheme> dilate{DilationScheme::None,
                                     DilationScheme::Linear,
                                     DilationScheme::Exponential};

  // check various option combinations
  for (bool res : residual) {
    for (bool bot : bottleNeck) {
      for (bool bn : batchN) {
        for (int k : kernel) {
          for (int s : stride) {
            for (int l : nLayers) {
              for (int b : nBlocks) {
                for (auto conc : concat) {
                  for (auto ups : upsample) {
                    for (auto dec : decode) {
                      for (auto d : dilate) {
                        if (l == 1 && bot)
                          continue;
                        if (dec == DecodeType::Deconv &&
                            conc != ConcatType::None) {
                          continue;
                        }
                        if (dec == DecodeType::None && s != 1) {
                          continue;
                        }

                        if (s != 1 && dec != DecodeType::None &&
                            ups == UpsamplingType::None) {
                          continue;
                        }
                        switch (conc) {
                          case ConcatType::None:
                            VLOG(4) << "Concat None";
                            break;
                          case ConcatType::Input:
                            VLOG(4) << "Concat Input";
                            break;
                          case ConcatType::Mirror:
                            VLOG(4) << "Concat Mirror";
                            break;
                        };
                        switch (ups) {
                          case UpsamplingType::None:
                            VLOG(4) << "Upsampling None";
                            break;
                          case UpsamplingType::Bilin:
                            VLOG(4) << "Upsampling Bilin";
                            break;
                          case UpsamplingType::Deconv:
                            VLOG(4) << "Upsampling Deconv";
                            break;
                        };
                        switch (dec) {
                          case DecodeType::None:
                            VLOG(4) << "Decode None";
                            break;
                          case DecodeType::Conv:
                            VLOG(4) << "DecodeType_ Conv";
                            break;
                          case DecodeType::Deconv:
                            VLOG(4) << "DecodeType_ deConv";
                            break;
                        };
                        VLOG(4) << " residual " << res << " batchnorm " << bn
                                << " kernel " << k << " stride " << s
                                << " layers " << l;
                        int minSize = 10 * std::pow((double)s, b);
                        auto net = EncoderDecoder()
                                       .inShape({32, minSize, minSize + 1})
                                       .intermSize(46)
                                       .nOutFeats(64)
                                       .concatInput(conc)
                                       .upsampling(ups)
                                       .decodeType(dec)
                                       .dilationType(d)
                                       .kernelSize(k)
                                       .stride(s)
                                       .residual(res)
                                       .batchNorm(bn)
                                       .bottleNeck(bot)
                                       .nInnerLayers(l)
                                       .numBlocks(b)
                                       .make();

                        torch::Tensor test =
                            torch::zeros({5, 32, minSize, minSize + 1});

                        auto out = net->forward({test})[0];
                        EXPECT(out.sizes().size() == 4U);
                        EXPECT(out.size(0) == 5);
                        EXPECT(out.size(1) == 64);
                        EXPECT(out.size(2) == minSize);
                        EXPECT(out.size(3) == minSize + 1);
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
