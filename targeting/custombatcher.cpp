/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "custombatcher.h"
#include "common/autograd.h"
#include "cpid/batcher.h"
#include "keys.h"

namespace {
// list of keys to which we apply hetereogenous batching
const std::unordered_set<std::string> het_keys{keys::kAllyData,
                                               keys::kAllyPos,
                                               keys::kEnemyData,
                                               keys::kEnemyPos,
                                               keys::kSamplingHist,
                                               keys::kPairsData,
                                               keys::kMaskKey};

// correspondance between an hetereogenous key and the key containing the number
// of items per batch element
const std::unordered_map<std::string, std::string> num_keys{
    {keys::kAllyData, keys::kNumAllies},
    {keys::kAllyPos, ""},
    {keys::kEnemyData, keys::kNumEnemies},
    {keys::kEnemyPos, ""},
    {keys::kSamplingHist, ""},
    {keys::kPairsData, ""},
    {keys::kMaskKey, ""}};

// list of keys that need hetereogenous unbatching
const std::unordered_set<std::string> het_unbatch_keys{keys::kPiKey,
                                                       keys::kPiPlayKey,
                                                       keys::kSigmaKey};

torch::Tensor fromVec(std::vector<long> v) {
  return torch::from_blob(
             v.data(),
             {(int)v.size()},
             torch::TensorOptions()
                 .device(torch::kCPU)
                 .dtype(torch::kLong)
                 .requires_grad(false))
      .clone();
}

} // namespace

CustomBatcher::CustomBatcher(
    ag::Container model,
    int batchSize,
    int padValue,
    bool stripOutput)
    : cpid::AsyncBatcher(model, batchSize, padValue, stripOutput) {}

std::vector<ag::Variant>
CustomBatcher::unBatch(const ag::Variant& o, bool stripOutput, double) {
  const ag::VariantDict& dict = o.getDict();
  const int batchSize = dict.at(keys::kPolSize).get().size(0);

  std::vector<ag::Variant> res(batchSize, ag::VariantDict{});

  torch::Tensor pol_size_tens = dict.at(keys::kPolSize).get().to(torch::kCPU);
  auto pol_size = pol_size_tens.accessor<long, 1>();

  for (const auto& v : dict) {
    const std::string& key = v.first;
    std::vector<ag::Variant> current_unbatch;
    if (het_unbatch_keys.count(key) > 0) {
      int currentStart = 0;
      for (int i = 0; i < batchSize; ++i) {
        const int cur_pol_size = pol_size[i];
        current_unbatch.emplace_back(dict.at(key).get().slice(
            0, currentStart, currentStart + cur_pol_size));
        currentStart += cur_pol_size;
      }

    } else {
      current_unbatch = common::unBatchVariant(v.second);
    }

    if (batchSize != (int)current_unbatch.size()) {
      LOG(FATAL) << "Didn't find the correct batch size for key " << key
                 << ". Expected " << batchSize << " but got "
                 << current_unbatch.size();
    }
    for (size_t i = 0; i < res.size(); ++i) {
      res[i].getDict()[key] = std::move(current_unbatch[i]);
    }
  }

  return res;
}

ag::Variant CustomBatcher::makeBatch(
    const std::vector<ag::Variant>& queries,
    double padValue) {
  if (queries.size() == 0) {
    LOG(FATAL) << "Expected at least one query to batch";
  }
  if (!queries.front().isDict()) {
    if (queries.front().isTensor()) {
      std::vector<torch::Tensor> all_tensors;
      for (const auto& t : queries) {
        all_tensors.push_back(t.get().view(-1));
      }
      return torch::cat(all_tensors);
    }
    return common::makeBatchVariant(queries, padValue);
  }
  ag::VariantDict res;
  const ag::VariantDict& dict = queries.front().getDict();

  // The idea is that different query may have a different number of
  // allies/enemies. To avoid padding to force a square batch, we collapse
  // everything on the batch dimension. This is more efficient, but the models
  // must be expecting that.

  std::unordered_map<std::string, std::vector<ag::Variant>> keyContents;
  std::unordered_set<std::string> all_keys;
  auto device = common::getVariantDevice((*dict.begin()).second);
  // we get all the variants for all the query elements, sorted by key
  for (const auto& it : dict) {
    const std::string key = it.first;
    all_keys.insert(key);
    for (const auto& q : queries) {
      const ag::VariantDict& current_dict = q.getDict();
      auto current_it = current_dict.find(key);
      if (current_it == current_dict.end()) {
        LOG(FATAL) << "One of the queries did not contain expected key " << key;
      }
      keyContents[key].emplace_back((*current_it).second);
    }
  }

  // now we can batch, key by key
  for (const std::string& key : all_keys) {
    if (het_keys.count(key) > 0) {
      std::vector<long> nums;
      std::vector<torch::Tensor> all_vecs;
      for (auto& v : keyContents[key]) {
        nums.push_back(v.get().size(0));
        all_vecs.emplace_back(v.get());
      }
      res[key] = torch::cat(all_vecs, 0);
      if (!num_keys.at(key).empty()) {
        res[num_keys.at(key)] = fromVec(nums).to(device);
      }
    } else {
      // for other keys, we batch normally
      res[key] = common::makeBatchVariant(keyContents[key], padValue);
    }
  }

  return res;
}
