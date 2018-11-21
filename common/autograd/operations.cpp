/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "operations.h"

#include "debug.h"
#include <glog/logging.h>

namespace common {

torch::Tensor repeat2d(torch::Tensor data, at::IntList sizes) {
  if (data.dim() != 1) {
    throw std::runtime_error("Single-dimension tensor expected");
  }
  return data.unsqueeze(1).unsqueeze(1).expand(
      {data.size(0), sizes[0], sizes[1]});
}

namespace {

// scatterSum2d() with a single index_add call
torch::Tensor scatterSum2d_single(
    torch::Tensor positions,
    torch::Tensor data,
    at::IntList sizes) {
  auto sizeY = sizes[0];
  auto sizeX = sizes[1];
  auto sizeB = data.size(0);
  auto sizeN = data.size(1);
  auto sizeC = data.size(2);

  auto ys = positions.select(2, 0).squeeze();
  auto xs = positions.select(2, 1).squeeze();

  // Filter out data with at least one negative coordinate
  auto xMask = xs.ge(0);
  auto yMask = ys.ge(0);
  auto mask = xMask.mul(yMask).ge(1);
  auto dataMask = mask.reshape({sizeB, sizeN, 1}).expand({sizeB, sizeN, sizeC});
  auto maskedData = data.masked_select(dataMask).reshape({-1, sizeC});

  // Map 2D space to 1D
  auto indices = xs.add(ys, sizeX);
  if (sizeB > 1) {
    // Add additional offset based on the batch
    auto bs = torch::arange(0, sizeB, positions.options());
    bs = at::unsqueeze(bs, 1).expand({sizeB, sizeN});
    indices.add_(bs, sizeX * sizeY);
  }

  indices = indices.masked_select(mask).to(torch::kLong).flatten();
  auto dest = torch::zeros({sizeX * sizeY * sizeB, sizeC}, data.options());
  dest = dest.index_add_(0, indices, maskedData);
  dest = dest.reshape({sizeB, sizeY, sizeX, sizeC});
  return dest.permute({0, 3, 1, 2});
}

// scatterSum2d() with multiple scatter_add calls into the same buffer
torch::Tensor scatterSum2d_iterative(
    torch::Tensor positions,
    torch::Tensor data,
    at::IntList sizes) {
  auto sizeY = sizes[0];
  auto sizeX = sizes[1];
  auto sizeB = data.size(0);
  auto sizeN = data.size(1);
  auto sizeC = data.size(2);

  auto posCpu = positions.to(at::kCPU).to(at::kInt);
  auto dest = torch::zeros({sizeB, sizeC, sizeY, sizeX}, data.options());
  for (auto b = 0; b < sizeB; b++) {
    std::vector<std::vector<int64_t>> planeDest;
    std::vector<std::vector<int64_t>> planeEls;
    std::vector<std::vector<bool>> planeMap;

    // offset = Y * stride + X
    auto offsets =
        at::add(posCpu[b].select(1, 1), posCpu[b].select(1, 0), sizeX);
    auto oacc = offsets.accessor<int, 1>();

    planeDest.push_back(std::vector<int64_t>());
    planeEls.push_back(std::vector<int64_t>());
    planeMap.push_back(std::vector<bool>(sizeY * sizeX));
    int n = 0;
    for (auto i = 0U; i < sizeN; i++) {
      auto offset = oacc[i];
      if (offset < 0) {
        break; // end of items in this batch element
      }
      n++;
      auto planeIdx = 0U;
      while (planeIdx < planeDest.size()) {
        if (planeMap[planeIdx][offset] == false) {
          planeDest[planeIdx].push_back(offset);
          planeEls[planeIdx].push_back(i);
          planeMap[planeIdx][offset] = true;
          break;
        }
        planeIdx++;
      }

      // New plane required?
      if (planeIdx >= planeDest.size()) {
        planeDest.push_back(std::vector<int64_t>());
        planeEls.push_back(std::vector<int64_t>());
        planeMap.push_back(std::vector<bool>(sizeY * sizeX));
        planeDest[planeIdx].push_back(offset);
        planeEls[planeIdx].push_back(i);
        planeMap[planeIdx][offset] = true;
      }
    }
    if (n == 0) {
      continue;
    }

    auto numPlanes = planeDest.size();
    VLOG(4) << "Requiring " << numPlanes << " planes for " << n << " positions";

    // Gather data for each plane
    std::vector<torch::Tensor> planeSrc;
    for (auto i = 0U; i < numPlanes; i++) {
      auto index = torch::from_blob(
                       planeEls[i].data(),
                       {int64_t(planeEls[i].size())},
                       torch::TensorOptions()
                           .device(torch::kCPU)
                           .dtype(torch::kLong)
                           .requires_grad(false))
                       .clone()
                       .to(data.options().device());
      auto src = at::index_select(data[b], 0, index);
      planeSrc.push_back(src);
    }

    // Scatter onto planes and sum up
    auto destb = torch::zeros(
        {sizeY, sizeX, sizeC}, data.options().requires_grad(false));
    auto destv = destb.view({-1, sizeC});
    for (auto i = 0U; i < numPlanes; i++) {
      auto index = torch::from_blob(
                       planeDest[i].data(),
                       {int64_t(planeDest[i].size()), 1},
                       torch::TensorOptions()
                           .device(torch::kCPU)
                           .dtype(torch::kLong)
                           .requires_grad(false))
                       .clone()
                       .to(data.options().device())
                       .expand({int(planeDest[i].size()), sizeC});
      destv.scatter_add_(0, index, planeSrc[i]);
    }
    dest[b].copy_(destb.permute({2, 0, 1}));
  }
  return dest;
}

} // namespace

torch::Tensor
scatterSum2d(torch::Tensor positions, torch::Tensor data, at::IntList sizes) {
  if (positions.dim() != 3) {
    throw std::runtime_error("Three-dimensional position tensor expected");
  }
  if (data.dim() != 3) {
    throw std::runtime_error("Three-dimensional data tensor expected");
  }
  if (positions.size(0) != data.size(0) || positions.size(1) != data.size(1)) {
    throw std::runtime_error("# of elements in positions and data must match");
  }

  // If we're operating on a GPU, do a single scatter() call instead. This will
  // be faster but requires memory (depending on the number of duplicate
  // positions).
  // On the CPU, the single-scatter implementation is actually quite slow.
  if (data.options().device().is_cuda()) {
    return scatterSum2d_single(positions, data, sizes);
  } else {
    return scatterSum2d_iterative(positions, data, sizes);
  }
}

torch::Tensor makeBatch(ag::tensor_list const& lst, double pad) {
  auto batchSize = lst.size();
  if (batchSize < 1) {
    throw std::runtime_error("makeBatch: Batch cannot have 0 elements");
  }

  std::vector<int64_t> sizes = lst[0].sizes().vec();
  bool sizeMismatch = false;
  for (auto i = 1U; i < lst.size(); i++) {
    auto elemSize = lst[i].sizes();
    for (auto j = 0U; j < elemSize.size(); j++) {
      sizeMismatch = sizeMismatch || elemSize[j] != sizes[j];
      sizes[j] = std::max(sizes[j], elemSize[j]);
    }
  }
  if (!sizeMismatch) {
    // if all the elements have the same size, we use stack, which is faster
    return at::stack(lst);
  }
  sizes.insert(sizes.begin(), batchSize);

  auto batch = torch::empty(sizes, lst[0].options()).fill_(pad);

  for (auto i = 0U; i < batchSize; i++) {
    auto slice = batch[i];
    auto elemSize = lst[i].sizes();
    for (auto j = 0U; j < elemSize.size(); j++) {
      slice = slice.narrow(j, 0, lst[i].size(j));
    }
    slice.copy_(lst[i]);
  }

  return batch;
}

ag::Variant makeBatchVariant(
    const std::vector<ag::Variant>& queries,
    double pad) {
  if (queries.size() == 0) {
    throw std::runtime_error("makeBatch: Batch cannot have 0 elements");
  }

  auto isTensor = [](const ag::Variant& v) {
    return v.isTensor() || (v.isTensorList() && v.getTensorList().size() == 1);
  };

  if (isTensor(queries[0])) {
    // we have a list of tensors, make a tensor_list
    ag::tensor_list query;
    for (const auto& q : queries) {
      query.emplace_back(q[0]);
    }
    return makeBatch(query, pad);
  }
  if (queries[0].isDict()) {
    // we batch key by key
    ag::VariantDict result;
    auto const& dict = queries[0].getDict();
    for (const auto& elem : dict) {
      const std::string& key = elem.first;
      std::vector<ag::Variant> curQuery;
      for (size_t i = 0; i < queries.size(); ++i) {
        curQuery.push_back(queries[i].getDict().at(key));
      }
      result[key] = makeBatchVariant(curQuery, pad);
    }
    return result;
  }
  if (queries[0].isTensorList()) {
    // we batch element by element
    ag::tensor_list batch;
    const size_t size = queries[0].getTensorList().size();
    batch.reserve(size);
    for (size_t i = 0; i < size; ++i) {
      ag::tensor_list curQuery;
      curQuery.reserve(queries.size());
      for (size_t j = 0; j < queries.size(); ++j) {
        curQuery.push_back(queries[j][i]);
      }
      batch.push_back(makeBatch(curQuery, pad));
    }
    return batch;
  }
  throw std::runtime_error(
      "makeBatch: variant type not supported at the moment");
  return ag::Variant(0);
}

std::vector<ag::Variant>
unBatchVariant(ag::Variant batch, int stride, bool maskOut, double maskValue) {
  std::vector<ag::Variant> reply;
  if (batch.isTensorList() || batch.isTensor()) {
    const ag::tensor_list& out = batch.isTensorList()
        ? batch.getTensorList()
        : ag::tensor_list{batch.get()};
    if (out.empty()) {
      return {};
    }

    auto actualBatchSize = out[0].size(0);
    if (actualBatchSize % stride != 0) {
      LOG(FATAL) << "Got a batch size of " << actualBatchSize
                 << ", which is not compatible with a stride of " << stride;
    }
    for (size_t i = 0; i < out.size(); ++i) {
      if (out[i].size(0) != actualBatchSize) {
        LOG(FATAL) << "Batch dimension for variable " << i << " is "
                   << out[i].size(0) << " and doesn't match its expected size "
                   << actualBatchSize;
      }
    }
    for (decltype(actualBatchSize) i = 0; i < actualBatchSize / stride; ++i) {
      ag::tensor_list res;
      for (const auto& v : out) {
        torch::Tensor curSlice = v.slice(0, i * stride, (i + 1) * stride);
        if (maskOut) {
          torch::Tensor diff = curSlice.add(-maskValue).abs();
          curSlice = curSlice.masked_select(diff.gt(1e-4));
        }
        if (stride == 1) {
          curSlice = curSlice.squeeze(0);
        }
        res.emplace_back(std::move(curSlice));
      }
      if (res.size() == 1) {
        reply.emplace_back(ag::Variant(std::move(res[0])));
      } else {
        reply.emplace_back(ag::Variant(std::move(res)));
      }
    }
    return reply;
  }
  if (batch.isDict()) {
    const ag::VariantDict& out = batch.getDict();
    std::vector<ag::Variant> result;
    for (const auto& item : out) {
      const std::string& key = item.first;
      std::vector<ag::Variant> unBatched =
          unBatchVariant(item.second, stride, maskOut, maskValue);
      if (result.size() == 0) {
        result.reserve(unBatched.size());
        for (size_t i = 0; i < unBatched.size(); ++i) {
          result.emplace_back(ag::VariantDict());
        }
      }
      for (size_t i = 0; i < unBatched.size(); ++i) {
        result[i].getDict()[key] = std::move(unBatched[i]);
      }
    }
    return result;
  }
  throw std::runtime_error("unBatch: unsupported batch type");
  return {};
}

torch::Tensor pad2d(torch::Tensor input, at::IntList pad) {
  int nPads = pad.size();
  if (nPads != 4) {
    throw std::runtime_error("4 paddings expected");
  }
  int nDim = input.ndimension();
  if (nDim != 3 && nDim != 4) {
    throw std::runtime_error("Only {3,4}D tensor supported atm");
  }
  // Padding indices
  int pad_l = 0;
  int pad_r = 1;
  int pad_t = 2;
  int pad_b = 3;
  // int pad_fr = 4;
  // int pad_bk = 5;

  std::vector<int64_t> pSizes(input.sizes().begin(), input.sizes().end());
  pSizes[nDim - 2] += pad[pad_t] + pad[pad_b];
  pSizes[nDim - 1] += pad[pad_l] + pad[pad_r];
  auto output = torch::zeros(pSizes, input.options());
  output.slice(nDim - 1, pad[pad_l], input.size(nDim - 1) + pad[pad_l])
      .slice(nDim - 2, pad[pad_t], input.size(nDim - 2) + pad[pad_t])
      .copy_(input);
  return output;
}

torch::Tensor padNd(torch::Tensor input, at::IntList pad) {
  auto nPads = pad.size();
  auto nDim = static_cast<size_t>(input.ndimension());
  if (2 * nDim != nPads) {
    throw std::runtime_error(
        "Inconsistent number of paddings and input dimensions");
  }

  auto inputSizes = input.sizes();
  auto outputSizes = inputSizes.vec();
  for (auto d = 0U; d < nDim; d++) {
    outputSizes[d] += pad[2 * d] + pad[2 * d + 1];
  }
  auto output = torch::zeros(outputSizes, input.options());

  auto slice = output;
  for (auto d = 0U; d < nDim; d++) {
    slice = slice.slice(d, pad[2 * d], inputSizes[d] + pad[2 * d]);
  }
  slice.copy_(input);

  return output;
}

torch::Tensor flip(torch::Tensor x, int dim) {
  // Would be easy if we could have negative strides...
  auto n = x.size(dim);
  auto index = torch::arange(n - 1, -1, -1, x.options().dtype(at::kLong));
  return x.index_select(dim, index);
}

namespace {
std::vector<int64_t>
outputSize(int dim, torch::Tensor input, at::IntList size, int scaleFactor) {
  if (!size.empty()) {
    return size.vec();
  }
  std::vector<int64_t> outputSize;
  for (auto d = 0; d < dim; d++) {
    outputSize.push_back(input.size(d + 2) * scaleFactor);
  }
  return outputSize;
}

torch::Tensor upsample(
    torch::Tensor input,
    UpsampleMode mode,
    at::IntList size,
    int scaleFactor) {
  auto outsize = outputSize(input.dim() - 2, input, size, scaleFactor);
  if (input.dim() == 3 && mode == UpsampleMode::Nearest) {
    return at::upsample_nearest1d(input, outsize);
  } else if (input.dim() == 4 && mode == UpsampleMode::Nearest) {
    return at::upsample_nearest2d(input, outsize);
  } else if (input.dim() == 5 && mode == UpsampleMode::Nearest) {
    return upsample_nearest3d(input, outsize);
  } else if (input.dim() == 3 && mode == UpsampleMode::Linear) {
    return at::upsample_linear1d(input, outsize, true);
  } else if (input.dim() == 4 && mode == UpsampleMode::Bilinear) {
    return at::upsample_bilinear2d(input, outsize, true);
  } else if (input.dim() == 5 && mode == UpsampleMode::Trilinear) {
    return at::upsample_trilinear3d(input, outsize, true);
  } else {
    throw std::runtime_error(
        "unsupported mode for dimension " + std::to_string(input.dim()));
  }
}
} // namespace

torch::Tensor
upsample(torch::Tensor input, UpsampleMode mode, at::IntList size) {
  return upsample(input, mode, size, 0);
}

torch::Tensor
upsample(torch::Tensor input, UpsampleMode mode, int scaleFactor) {
  return upsample(input, mode, {}, scaleFactor);
}

void zerosToOnes_(torch::Tensor x) {
  x.masked_fill_(x.eq(0), 1);
}

#ifndef WITHOUT_POSIX
torch::Tensor tensorFromNpyArray(
    cnpy::NpyArray array,
    torch::TensorOptions op) {
  std::vector<int64_t> size_vec;
  for (auto i = 0U; i < array.shape.size(); i++) {
    size_vec.push_back(array.shape[i]);
  }
  at::IntList size(size_vec);

  auto tensor = torch::from_blob(array.data<void>(), size, op.device_index(-1));
  return tensor.clone().to(op.device());
}
#endif //WITHOUT_POSIX

torch::Tensor squash(torch::Tensor x, int i, int j) {
  auto inputSize = x.sizes();
  std::vector<int64_t> outputSize;
  for (auto d = 0; d < static_cast<int>(inputSize.size()); d++) {
    if (d > i && d <= j) {
      outputSize[i] *= inputSize[d];
    } else {
      outputSize.push_back(inputSize[d]);
    }
  }

  return x.view(outputSize);
}

torch::Tensor unsquash(torch::Tensor x, int i, at::IntList sizes) {
  auto inputSize = x.sizes();
  std::vector<int64_t> outputSize;
  outputSize.insert(outputSize.end(), inputSize.begin(), inputSize.begin() + i);
  outputSize.insert(outputSize.end(), sizes.begin(), sizes.end());
  outputSize.insert(
      outputSize.end(), inputSize.begin() + i + 1, inputSize.end());

  return x.view(outputSize);
}

torch::Tensor maskedSum(torch::Tensor x, torch::Tensor mask) {
  auto res = (mask * x).sum();
  return res;
}

torch::Tensor maskedMean(torch::Tensor x, torch::Tensor mask) {
  auto res = maskedSum(x, mask);
  auto num = mask.expand(x.sizes()).sum();
  if (num.is_nonzero()) {
    res /= num;
  }

  return res;
}

namespace {
torch::Tensor
maskedReduce(torch::Tensor x, torch::Tensor mask, bool sizeAverage) {
  if (sizeAverage) {
    return maskedMean(x, mask);
  } else {
    return maskedSum(x, mask);
  }
}
} // namespace

torch::Tensor mseLoss(
    torch::Tensor x,
    torch::Tensor y,
    torch::Tensor mask,
    bool sizeAverage,
    bool reduce) {
  auto diff = x - y;
  auto res = mask * at::mul(diff, diff);

  if (reduce) {
    res = maskedReduce(res, mask, sizeAverage);
  }

  return res;
}

torch::Tensor crossEntropyLoss(
    torch::Tensor input,
    int dim,
    torch::Tensor target,
    torch::Tensor weight,
    torch::Tensor mask,
    Reduction::Reduction reduction) {
  if (!mask.defined()) {
    mask = torch::ones({1}, input.options());
  }

  if (!weight.defined()) {
    weight = torch::ones({input.size(dim)}, input.options()); // C
  }
  weight = unsqueezes(dim, weight, input.ndimension() - dim - 1);

  // We need to compute -sum y_i log(x_i) where y_i is a target distribution
  // and x_i the predicted distribution (result of softmax layer on input).
  // This reduces to -sum y_i z_i + log sum exp z_i where z_i is the raw input.
  // The last term of the difference can be computed with the log sum exp trick.
  auto sumProd = (weight * input * target).sum(dim, true); // *x1x*

  auto sub = std::get<0>(input.max(dim, true)); // *x1x*
  auto logSumExp = at::log(at::exp(input - sub).sum(dim, true)) + sub; // *x1x*

  // XXX can we avoid computing logSumExps for masked indices?
  auto res = -mask * (sumProd - logSumExp);
  if (reduction != Reduction::None) {
    res = maskedReduce(res, mask, reduction == Reduction::Mean);
  }
  return res;
}

torch::Tensor nllLoss(
    torch::Tensor input,
    int dim,
    torch::Tensor target,
    torch::Tensor weight,
    torch::Tensor mask,
    Reduction::Reduction reduction) {
  if (!mask.defined()) {
    mask = torch::ones({1}, input.options());
  }

  if (!weight.defined()) {
    weight = torch::ones({input.size(1)}, input.options()); // C
  }
  weight = unsqueezes(dim, weight, input.ndimension() - dim - 1);

  // We need to compute -sum y_i log(x_i).
  auto log = at::log(input);
  // Set -inf to 0.
  log.masked_fill_(log == -std::numeric_limits<float>::infinity(), 0);

  auto res = -(weight * mask * target * log).sum(dim, true); // Nx1xHxW
  if (reduction != Reduction::None) {
    res = maskedReduce(res, mask, reduction == Reduction::Mean);
  }
  return res;
}

void clipGradientNorms(std::vector<torch::Tensor> parameters, float maxNorm) {
  torch::NoGradGuard guard;
  auto totalNorm = 0.0;
  for (auto& p : parameters) {
    auto grad = p.grad();
    totalNorm += at::mul(grad, grad).sum().item<float>();
  }
  totalNorm = std::sqrt(totalNorm);

  auto coef = maxNorm / totalNorm; // No need to worry about totalNorm = 0
  if (coef < 1) {
    for (auto& p : parameters) {
      p.grad().mul_(coef);
    }
  }
}

torch::Tensor maskedSoftmax(
    torch::Tensor input,
    torch::Tensor mask,
    int dim,
    float clampEpsilon) {
  // Validate inputs
  auto inputShape = input.sizes();
  for (auto i = 0u; i < inputShape.size(); i++) {
    if (inputShape[i] != mask.size(i)) {
      throw std::runtime_error("The mask and input must be the same shape.");
    }
  }
  if (input.type() != mask.type()) {
    throw std::runtime_error("The mask and input type must be the same.");
  }

  // First subtract the minimum value to have the min in the masked tensor be 0
  auto y = input.mul(mask);
  y = y.sub(std::get<0>(y.min(dim, true)));

  // Then subtract the max of the unmasked values before exponential so the max
  // of the masked tensor is 0
  y = y.sub(std::get<0>(y.max(dim, true)));

  y = mask.mul(at::exp(y));
  return (y / (y.sum(dim, true) + clampEpsilon)).clamp(clampEpsilon, 1);
}

std::tuple<torch::Tensor, torch::Tensor>
maskedMax(torch::Tensor input, torch::Tensor mask, int dim, bool keepDim) {
  // First subtract the minimum value to have the min in the masked tensor be 0
  auto subbed = std::get<0>(input.min(dim, true));
  auto y = input - subbed; // values range from 0 to max - min

  auto res = (y * mask).max(dim, true);

  // Re-add the subbed amount to the max value.
  auto max = std::get<0>(res) + subbed;
  auto argmax = std::get<1>(res);

  if (!keepDim) {
    max = max.squeeze(dim);
    argmax = argmax.squeeze(dim);
  }
  return std::make_tuple(max, argmax);
}

torch::Tensor weightedMaskedSoftmax(
    torch::Tensor input,
    torch::Tensor mask,
    int dim,
    float clampEpsilon) {
  // Validate inputs
  auto inputShape = input.sizes();
  for (auto i = 0u; i < inputShape.size(); i++) {
    if (inputShape[i] != mask.size(i)) {
      throw std::runtime_error("The mask and input must be the same shape.");
    }
  }
  if (input.type() != mask.type()) {
    throw std::runtime_error("The mask and input type must be the same.");
  }

  // First subtract the minimum value to have the min in the masked tensor be 0
  auto binm = mask.gt(0).toType(at::kFloat);
  auto y = input.mul(binm);
  y = y.sub(std::get<0>(y.min(dim, true)));

  // Then subtract the max of the unmasked values before exponential so the max
  // of the masked tensor is 0
  y = y.sub(std::get<0>(y.max(dim, true)));

  y = mask.mul(at::exp(y));
  return (y / (y.sum(dim, true) + clampEpsilon)).clamp(clampEpsilon, 1);
}

torch::Tensor
selectIndex(torch::Tensor x, torch::Tensor y, int axis, bool keepDim) {
  auto res = x.gather(axis, y);
  if (!keepDim) {
    res = res.squeeze(axis);
  }
  return res;
}

torch::Tensor extendIndex(torch::Tensor y, int axis, int d) {
  auto sizes = y.sizes().vec();
  sizes[axis] = d;
  auto x = torch::zeros(sizes, y.options().dtype(at::kByte));

  x.scatter_(axis, y, 1);
  return x;
}

void maskedCopy_(torch::Tensor x, torch::Tensor mask, torch::Tensor source) {
  auto values = source.masked_select(mask);
  x.masked_scatter_(mask, values);
}

torch::Tensor
maskedCopy(torch::Tensor x, torch::Tensor mask, torch::Tensor source) {
  // When mask is 1, we have source, otherwise we have x.
  return mask * source + (1 - mask) * x;
}

namespace {
// Index NxD is a vector of indices for tensor D-dim tensor x.
// Returns a N tensor that indices the flat tensor x.
torch::Tensor indexTo1D(torch::Tensor x, torch::Tensor index) {
  if (index.device().is_cpu()) {
    auto stridesVec = x.strides().vec();
    auto strides =
        torch::from_blob(stridesVec.data(), x.ndimension(), at::kLong)
            .to(index.device());
    auto index1D = index.mv(strides);
    return index1D;
  } else {
    // Sadly (add)mv doesn't exist for CUDA long tensors.
    // Let's use an unoptimized (short) for-loop, since in practice N=2.
    auto index1D = torch::zeros({index.size(0)}, index.options());
    for (auto d = 0; d < x.ndimension(); d++) {
      index1D += x.stride(d) * index.select(1, d);
    }
    return index1D;
  }
}
} // namespace

void putNd_(
    torch::Tensor x,
    torch::Tensor index,
    torch::Tensor source,
    bool accumulate) {
  auto index1D = indexTo1D(x, index);
  x.view({-1}).put_(index1D, source, accumulate);
}

torch::Tensor takeNd(torch::Tensor x, torch::Tensor index) {
  auto index1D = indexTo1D(x, index);
  return x.view({-1}).take(index1D);
}

torch::Tensor
indexMean(int size, int dim, torch::Tensor index, torch::Tensor source) {
  // First, let's squash all the batch dimensions to go the 2D case.
  source = source.transpose(0, dim).contiguous();
  auto N = source.size(0);
  auto sizes = source.sizes().vec();
  source = source.view({N, -1}); // NxX
  auto X = source.size(1);

  // NxX+1
  auto source_aug = at::cat({source, torch::ones({N, 1}, source.options())}, 1);

  auto x = torch::zeros({(int64_t)size, X + 1}, source.options());
  x.index_add_(0, index, source_aug);
  auto sums = x.slice(1, 0, X); // size x X
  auto counts = x.select(1, X).unsqueeze(1); // size x 1

  zerosToOnes_(counts);
  x = sums / counts; // size x X

  sizes[0] = size;
  return x.view(sizes).transpose(0, dim).contiguous();
}

torch::Tensor unsqueezes(int before, torch::Tensor x, int after) {
  std::vector<int64_t> sizes;
  for (auto i = 0; i < before; i++) {
    sizes.push_back(1);
  }
  for (auto size : x.sizes()) {
    sizes.push_back(size);
  }
  for (auto i = 0; i < after; i++) {
    sizes.push_back(1);
  }
  return x.view(sizes);
}

torch::Tensor meshGrid(ag::tensor_list tensors) {
  auto N = tensors.size();

  // Compute size to which we should expand tensors.
  std::vector<int64_t> size;
  for (auto tensor : tensors) {
    size.push_back(tensor.size(0));
  }

  // Expand all tensors to same size.
  ag::tensor_list expanded;
  for (auto i = 0U; i < N; i++) {
    expanded.push_back(unsqueezes(i, tensors[i], N - 1 - i).expand(size));
  }

  // Stack on last dimension.
  return at::stack(expanded, N);
}

ag::Variant applyTransform(ag::Variant input, const TensorTransform& fun) {
  if (input.isTensor()) {
    return ag::Variant(fun(input.get()));
  }
  if (input.isTensorList()) {
    ag::tensor_list& l = input.getTensorList();
    ag::tensor_list result;
    for (const auto& v : l) {
      result.emplace_back(fun(v));
    }
    return result;
  }
  if (input.isDict()) {
    ag::VariantDict& in = input.getDict();
    ag::VariantDict result;
    for (const auto& v : in) {
      result[v.first] = applyTransform(v.second, fun);
    }
    return result;
  }
  return input;
}

at::Device getVariantDevice(ag::Variant x) {
  if (x.isTensor()) {
    return x.get().options().device();
  } else if (x.isTensorList()) {
    return x[0].options().device();
  } else if (x.isDict()) {
    return getVariantDevice((*x.getDict().begin()).second);
  } else {
    LOG(FATAL) << "Trying to get device from unsupported variant";
  }
  return at::kCPU;
}
} // namespace common
