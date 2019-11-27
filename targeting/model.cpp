/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "model.h"
#include "common/autograd/debug.h"
#include "common/autograd/models.h"
#include "cpid/metrics.h"
#include "featurize.h"
#include "flags.h"
#include "keys.h"
#include "parameters.h"
#include "transformer.h"
#include <cmath>

using namespace cpid;
std::pair<torch::Tensor, torch::Tensor> crossProduct_indices(
    torch::Tensor indA,
    torch::Tensor indB,
    const c10::Device& device) {
  indA = indA.to(torch::kCPU);
  indB = indB.to(torch::kCPU);
  if (indA.size(0) != indB.size(0)) {
    LOG(FATAL) << "Mismatched sizes";
  }
  common::assertSize("indA", indA, {indA.size(0)});
  common::assertSize("indB", indB, {indB.size(0)});

  auto accA = indA.accessor<long, 1>();
  auto accB = indB.accessor<long, 1>();

  const int nbBatchItems = indA.size(0);
  std::vector<long> resultA, resultB;

  int offsetA = 0;
  int offsetB = 0;
  for (int i = 0; i < nbBatchItems; ++i) {
    int nbA = accA[i];
    int nbB = accB[i];
    for (int j = 0; j < nbA; ++j) {
      for (int k = 0; k < nbB; ++k) {
        resultA.push_back(j + offsetA);
        resultB.push_back(k + offsetB);
      }
    }
    offsetA += nbA;
    offsetB += nbB;
  }
  torch::Tensor resA = torch::from_blob(
                           resultA.data(),
                           {(int)resultA.size()},
                           torch::TensorOptions()
                               .device(torch::kCPU)
                               .dtype(torch::kLong)
                               .requires_grad(false))
                           .clone()
                           .to(device);

  torch::Tensor resB = torch::from_blob(
                           resultB.data(),
                           {(int)resultB.size()},
                           torch::TensorOptions()
                               .device(torch::kCPU)
                               .dtype(torch::kLong)
                               .requires_grad(false))
                           .clone()
                           .to(device);

  return {std::move(resA), std::move(resB)};
}

namespace {
// this function takes a vector [v1, v2, ... vn], and outputs [0,0,0,...n,n],
// where there are v1 zeros, v2 ones and so on
torch::Tensor generalized_range(
    const torch::Tensor& v,
    const c10::Device& device) {
  std::vector<long> indices;
  indices.reserve(v.sum().item<long>());
  auto acc = v.accessor<long, 1>();
  const int size = v.size(0);
  for (int i = 0; i < size; ++i) {
    const int N = acc[i];
    for (int j = 0; j < N; ++j) {
      indices.push_back(i);
    }
  }
  auto ind = torch::from_blob(
                 indices.data(),
                 {(long)indices.size()},
                 torch::TensorOptions()
                     .device(torch::kCPU)
                     .dtype(torch::kLong)
                     .requires_grad(false))
                 .clone()
                 .to(device);
  return ind;
};

} // namespace

ag::Variant Identity::forward(ag::Variant x) {
  return x;
}
void Identity::reset() {
  return;
}

namespace cherrypi {

void TargetingModel::reset() {
  int kFeats = inFeatures_;

  auto nConvInp = 2 * kFeats;
  auto enc = common::EncoderDecoder()
                 .inShape({nConvInp, FLAGS_map_dim, FLAGS_map_dim})
                 .intermSize(FLAGS_conv_embed_size)
                 .nOutFeats(FLAGS_conv_embed_size)
                 .kernelSize(3)
                 .stride(2)
                 .batchNorm(true)
                 .residual(true)
                 .bottleNeck(false)
                 .nInnerLayers(2)
                 .numBlocks(5)
                 .make();

  enc->eval();
  auto dummy = torch::zeros({1, nConvInp, FLAGS_map_dim, FLAGS_map_dim});
  torch::Tensor out = enc->forward({dummy})[0];
  int outSize = out.size(1) * out.size(2) * out.size(3);
  enc->train();

  valueTrunk_ = add(enc, "valueTrunk");
  valueHead_ =
      add(common::MLP()
              .nIn(outSize)
              .nHid(FLAGS_conv_embed_size / 2)
              .nOut(1)
              .nLayers(2)
              .make(),
          "valueHead_");

  if (isModelSpatial(model_type_)) {
    common::EncoderDecoder enc2 =
        common::EncoderDecoder()
            .inShape({nConvInp, FLAGS_map_dim, FLAGS_map_dim})
            .intermSize(FLAGS_conv_embed_size)
            .nOutFeats(2 * FLAGS_conv_embed_size)
            .kernelSize(3)
            .stride(1)
            .batchNorm(true)
            .residual(true)
            .bottleNeck(false)
            .nInnerLayers(2)
            .numBlocks(5);
    policyTrunk_ = add(enc2.make(), "policyTrunk_");
  }

  int inputSize = kFeats;
  if (isModelSpatial(model_type_)) {
    inputSize += FLAGS_conv_embed_size;
  }
  if (FLAGS_use_embeddings) {
    int outS = FLAGS_linear_embed_size;
    int taskOut = FLAGS_linear_embed_size;
    taskEmbed_ =
        add(ag::Sequential()
                .append(common::MLP()
                            .nIn(inputSize)
                            .nOut(taskOut)
                            .nLayers(2)
                            .nHid(outS)
                            .zeroLastLayer(false)
                            .make())
                .append(LayerNorm().size(taskOut).make())
                .make(),
            "taskEmbed_");
    taskEmbedSize_ = outS;

    agentEmbed_ =
        add(ag::Sequential()
                .append(common::MLP()
                            .nIn(inputSize)
                            .nOut(FLAGS_linear_embed_size)
                            .nLayers(2)
                            .nHid(FLAGS_linear_embed_size)
                            .zeroLastLayer(false)
                            .make())
                .append(LayerNorm().size(FLAGS_linear_embed_size).make())
                .make(),
            "agentEmbed_");

    agentEmbedSize_ = FLAGS_linear_embed_size;

  } else {
    taskEmbed_ = add(Identity().make(), "taskEmbed_");
    agentEmbed_ = add(Identity().make(), "agentEmbed_");
    taskEmbedSize_ = inputSize;
    agentEmbedSize_ = inputSize;
  }

  int lpMLPInputSize = agentEmbedSize_ + taskEmbedSize_ + inPairFeatures_;

  lpWeightsMLP_ =
      add(common::MLP()
              .nIn(lpMLPInputSize)
              .nOut(1)
              .nLayers(3)
              .nHid(FLAGS_linear_embed_size)
              .zeroLastLayer(zeroLastLayer_)
              .make(),
          "lpWeightMLP_");
  if (isModelQuad(model_type_)) {
    quadWeightsMLP_ =
        add(common::MLP()
                .nIn(2 * taskEmbedSize_)
                .nOut(1)
                .nHid(FLAGS_linear_embed_size)
                .nLayers(3)
                .zeroLastLayer(zeroLastLayer_)
                .make(),
            "quadWeightsMLP_");
  }
}

ag::Variant TargetingModel::forward(ag::Variant inp) {
  auto kFeats = inFeatures_;

  torch::Tensor state = inp[keys::kState];
  auto device = state.options().device();
  const int bs = state.size(0);
  common::assertSize(
      "state", state, {bs, 2 * kFeats, FLAGS_map_dim, FLAGS_map_dim});

  auto num_allies = inp[keys::kNumAllies].to(torch::kCPU);
  auto num_enemies = inp[keys::kNumEnemies].to(torch::kCPU);
  common::assertSize("num_allies", num_allies, {bs});
  common::assertSize("num_enemies", num_enemies, {bs});

  // First, compute the value function
  torch::Tensor valueEmb = valueTrunk_->forward({state})[0].view({bs, -1});
  torch::Tensor value = valueHead_->forward({valueEmb})[0];

  // Then, compute the policy
  torch::Tensor ally_feat = inp[keys::kAllyData];
  // torch::Tensor ally_pos =
  // torch::autograd::make_variable(inp[keys::kAllyPos]);
  torch::Tensor ally_pos = inp[keys::kAllyPos].to(torch::kLong);

  const int tot_num_allies = num_allies.sum().item<long>();

  // LOG(INFO) << "ally_feat" << ally_feat;
  common::assertSize("ally_feat", ally_feat, {tot_num_allies, kFeats});
  common::assertSize("ally_pos", ally_pos, {tot_num_allies, 2});

  torch::Tensor enemy_feat = inp[keys::kEnemyData];
  torch::Tensor enemy_pos = inp[keys::kEnemyPos].to(torch::kLong);

  const int tot_num_enemies = inp[keys::kNumEnemies].sum().item<long>();

  // LOG(INFO) << "enemy_feat" << enemy_feat;
  common::assertSize("enemy_feat", enemy_feat, {tot_num_enemies, kFeats});
  common::assertSize("enemy_pos", enemy_pos, {tot_num_enemies, 2});

  int tot_num_pairs;
  torch::Tensor pairs_feat;
  if (inPairFeatures_ > 0) {
    tot_num_pairs = (num_allies * num_enemies).sum().item<long>();
    pairs_feat = inp[keys::kPairsData];
    common::assertSize(
        "pairs_feat", pairs_feat, {tot_num_pairs, inPairFeatures_});
  }

  int totalFeats = kFeats;
  if (isModelSpatial(model_type_)) {
    totalFeats += FLAGS_conv_embed_size;

    int cFeat = FLAGS_conv_embed_size;
    torch::Tensor pos_embedding =
        policyTrunk_->forward({state})[0]
            .view({bs, 2 * cFeat, FLAGS_map_dim, FLAGS_map_dim})
            .transpose(0, 1)
            .contiguous()
            .view({2 * cFeat, -1});

    // we are going to index_select the embeddings of the allies/enemies from
    // the pos_embedding tensor

    torch::Tensor batch_ally = generalized_range(num_allies, device);
    torch::Tensor indices_ally =
        ally_pos.select(1, 0).view({tot_num_allies}) * (int)FLAGS_map_dim +
        ally_pos.select(1, 1).view({tot_num_allies}) +
        (int)FLAGS_map_dim * (int)FLAGS_map_dim *
            batch_ally.view({tot_num_allies});

    torch::Tensor conv_feat_ally = pos_embedding.index_select(1, indices_ally)
                                       .transpose(0, 1)
                                       .slice(1, 0, cFeat);

    ally_feat = torch::cat({ally_feat, conv_feat_ally}, 1);

    torch::Tensor batch_enemy = generalized_range(num_enemies, device);
    torch::Tensor indices_enemy = (int)FLAGS_map_dim * (int)FLAGS_map_dim *
            batch_enemy.view({tot_num_enemies}) +
        enemy_pos.select(1, 0).view({tot_num_enemies}) * (int)FLAGS_map_dim +
        enemy_pos.select(1, 1).view({tot_num_enemies});

    torch::Tensor conv_feat_enemy = pos_embedding.index_select(1, indices_enemy)
                                        .transpose(0, 1)
                                        .slice(1, cFeat, 2 * cFeat);

    enemy_feat = torch::cat({enemy_feat, conv_feat_enemy}, 1);
  }
  common::assertSize("ally_feat", ally_feat, {tot_num_allies, totalFeats});
  common::assertSize("enemy_feat", enemy_feat, {tot_num_enemies, totalFeats});

  // embed the allies and enemies
  ally_feat = agentEmbed_->forward(ally_feat)[0];
  enemy_feat = taskEmbed_->forward(enemy_feat)[0];

  const long expected_pairs = (num_allies * num_enemies).sum().item<long>();

  // We want to compute the cross features for all the pairs (ally, enemy)
  // that belong to the same batch.

  torch::Tensor allies_ind, enemies_ind;
  std::tie(allies_ind, enemies_ind) =
      crossProduct_indices(num_allies, num_enemies, device);

  torch::Tensor all_feats;

  // We want to compute the cross features for all the pairs (ally, enemy)
  // that belong to the same batch.

  auto ally_feat_expand = ally_feat.index_select(0, allies_ind);
  auto enemy_feat_expand = enemy_feat.index_select(0, enemies_ind);

  common::assertSize(
      "ally_feat_expand", ally_feat_expand, {expected_pairs, agentEmbedSize_});

  common::assertSize(
      "enemy_feat_expand", enemy_feat_expand, {expected_pairs, taskEmbedSize_});
  // we concat the features of each pair
  all_feats = torch::cat({ally_feat_expand, enemy_feat_expand}, 1);

  if (inPairFeatures_ > 0) {
    all_feats = torch::cat({all_feats, pairs_feat}, 1);
  }

  common::assertSize(
      "all_feats",
      all_feats,
      {expected_pairs, agentEmbedSize_ + taskEmbedSize_ + inPairFeatures_});

  // Run it through the mlp
  auto policy = lpWeightsMLP_->forward({all_feats})[0].view({expected_pairs});
  // LOG(INFO) << "all_feats forwarded" << policy;

  auto diagnosis = [&]() {
    if (torch::autograd::GradMode::is_enabled()) {
      std::vector<std::pair<std::string, ag::Container>> layers{
          {"policyTrunk", policyTrunk_},
          {"valueTrunk", valueTrunk_},
          {"valueHead", valueHead_},
          {"lpWeightsMLP", lpWeightsMLP_},
          {"quadWeightsMLP", quadWeightsMLP_},
          {"agentEmbed", agentEmbed_},
          {"taskEmbed", taskEmbed_}};

      for (const auto& l : layers) {
        if (l.second) {
          torch::Tensor sum = torch::zeros({1}).sum();
          int tot_size = 0;
          for (auto& p : l.second->parameters()) {
            sum = sum + p.detach().abs().sum();
            tot_size += p.view(-1).size(0);
          }
          if (!std::isfinite(sum.item<float>())) {
            LOG(ERROR) << "Layer " << l.first << " has diverged";
            throw std::runtime_error(
                "checkTensor: tensor has a NaN or infinity!");
          }
          if (tot_size > 0) {
            float mean = sum.item<float>() / tot_size;
            if (metrics_) {
              metrics_->pushEvent("mean_" + l.first, mean);
            } else {
              LOG(ERROR) << "no metrics";
            }
          }
        }
      }
    }
  };

  if (isModelQuad(model_type_)) {
    torch::Tensor enemiesA_ind, enemiesB_ind;
    std::tie(enemiesA_ind, enemiesB_ind) =
        crossProduct_indices(num_enemies, num_enemies, device);

    auto enemyA_feat_expand = enemy_feat.index_select(0, enemiesA_ind);
    auto enemyB_feat_expand = enemy_feat.index_select(0, enemiesB_ind);

    long expected_enemy_pairs = (num_enemies * num_enemies).sum().item<long>();
    common::assertSize(
        "enemyA_feat_expand",
        enemyA_feat_expand,
        {expected_enemy_pairs, taskEmbedSize_});
    common::assertSize(
        "enemyB_feat_expand",
        enemyB_feat_expand,
        {expected_enemy_pairs, taskEmbedSize_});

    // we concat the features of each pair
    torch::Tensor all_feats_quad =
        torch::cat({enemyA_feat_expand, enemyB_feat_expand}, 1);

    common::assertSize(
        "all_feats_quad",
        all_feats_quad,
        {expected_enemy_pairs, 2 * taskEmbedSize_});

    // Run it through the mlp
    auto policyQuad = quadWeightsMLP_->forward({all_feats_quad})[0].view(
        {expected_enemy_pairs});

    // now we need to merge together the linear part and the quadratic part of
    // the policy we can't simply cat, because we need to preserve the batch
    // order. the trick is to cat first, and then rearrange using an
    // index_select, so that the linear and quadratic part of each batch item
    // are brought back together.

    auto concat_policy = torch::cat({policy, policyQuad})
                             .view({expected_pairs + expected_enemy_pairs});
    std::vector<long> indices;
    indices.reserve(expected_pairs + expected_enemy_pairs);
    auto num_allies_acc = num_allies.accessor<long, 1>();
    auto num_enemies_acc = num_enemies.accessor<long, 1>();
    int offset_lin = 0;
    int offset_quad = expected_pairs;
    for (int i = 0; i < bs; ++i) {
      const int nA = num_allies_acc[i];
      const int nE = num_enemies_acc[i];
      for (int j = 0; j < nA * nE; ++j) {
        indices.push_back(offset_lin++);
      }
      for (int j = 0; j < nE * nE; ++j) {
        indices.push_back(offset_quad++);
      }
    }
    auto ind = torch::from_blob(
                   indices.data(),
                   {(long)indices.size()},
                   torch::TensorOptions()
                       .device(torch::kCPU)
                       .dtype(torch::kLong)
                       .requires_grad(false))
                   .to(device);

    policy = concat_policy.index_select(0, ind);
    common::assertSize(
        "policy", policy, {expected_pairs + expected_enemy_pairs});
  }
  diagnosis();
  if (!std::isfinite(policy.sum().item<float>())) {
    LOG(ERROR) << "Policy has diverged!";
    throw std::runtime_error("checkTensor: tensor has a NaN or infinity!");
  }

  // LOG(INFO) << "sigma " << sigma;

  auto real_policy = policy;
  if (Parameters::get_int("correlated_steps") > 1) {
    policy = policy + inp[keys::kSamplingHist].view_as(policy);
  }
  // LOG(INFO) << "Policy2 " << policy;

  /*
  auto old_policy = torch::zeros_like(policy).fill_(-42);
  old_policy =
      maskTot * inp["old_policy"].view_as(policy) + (1 - maskTot) *
  old_policy;

  policy = (1. - FLAGS_policy_momentum) * policy +
      FLAGS_policy_momentum * old_policy.view_as(policy);

  policy = policy.view({bs, -1});
  */

  torch::Tensor sigma = at::ones_like(policy) * Parameters::get_float("sigma");
  // sigma = sigma.view({bs, -1});

  torch::Tensor pol_size = num_allies * num_enemies;
  if (isModelQuad(model_type_)) {
    pol_size = pol_size + num_enemies * num_enemies;
  }

  auto dict = ag::VariantDict{{keys::kValueKey, value},
                              {keys::kPiKey, real_policy},
                              {keys::kSigmaKey, sigma},
                              {keys::kPiPlayKey, policy},
                              {keys::kPolSize, pol_size}};

  return dict;
}

} // namespace cherrypi
