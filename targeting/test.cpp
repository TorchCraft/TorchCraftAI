#include <autogradpp/autograd.h>
#include <lest.hpp>
#include "featurize.h"
#include <memory>
#include "custombatcher.h"
#include "keys.h"
#include "parameters.h"
#include "model.h"
#include "common/autograd.h"

using namespace std;
using namespace cherrypi;


torch::Tensor fromVec(std::vector<long> v) {
  return torch::from_blob(v.data(),
                          {(long)v.size()},
                          torch::TensorOptions()
                          .device(torch::kCPU)
                          .dtype(torch::kLong)
                          .requires_grad(false)).clone();
}

bool check_vec(torch::Tensor a, std::vector<long> b) {
  torch::Tensor targ = fromVec(b);
  a = a.to(torch::kCPU);
  return torch::abs(a - targ).max().item<long>() == 0;
}

bool equals(torch::Tensor a, torch::Tensor b) {
  bool res = (a - b).abs().le(1e-7).all().item<uint8_t>();
  return res;
}
std::vector<ag::Variant> generateBatch(int nFrames,
                                       int kEnemyFeats,
                                       int kAllyFeats,
                                       int dimX,
                                       int dimY,
                                       int feats,
                                       std::vector<long>& num_allies,
                                       std::vector<long>& num_enemies,
                                       bool quad,
                                       int pairFeatures = 0,
                                       bool memory = false) {
  std::vector<ag::Variant> frames;
  frames.reserve(nFrames);

   std::mt19937 gen(42);
   std::uniform_int_distribution<> dis(1, 6);
   std::bernoulli_distribution coin(0.5);

   for (int i = 0; i < nFrames; ++i) {
     int nAllies = dis(gen);
     int nEnemies = dis(gen);
     num_allies.push_back(nAllies);
     num_enemies.push_back(nEnemies);

     torch::Tensor enemy_data = torch::normal(torch::zeros({nEnemies, kEnemyFeats})).to(torch::kCPU);
     torch::Tensor ally_data = torch::normal(torch::zeros({nAllies, kAllyFeats})).to(torch::kCPU);
     torch::Tensor enemy_pos =  torch::randint(FLAGS_map_dim ,{nEnemies, 2}).to(torch::kLong).to(torch::kCPU);
     torch::Tensor ally_pos = torch::randint(FLAGS_map_dim ,{nAllies, 2}).to(torch::kLong).to(torch::kCPU);

     int size = nEnemies * nAllies;
     if (quad) {
       size += nEnemies * nEnemies;
     }
     torch::Tensor sampling_hist = torch::normal(torch::zeros({size})).to(torch::kCPU);

     torch::Tensor state = at::normal(torch::zeros({feats, dimY, dimX})).to(torch::kCPU);

     frames.push_back(ag::VariantDict({
             {keys::kAllyData, ag::Variant(ally_data)},
             {keys::kAllyPos, ag::Variant(ally_pos)},
             {keys::kEnemyData, ag::Variant(enemy_data)},
             {keys::kEnemyPos, ag::Variant(enemy_pos)},
             {keys::kSamplingHist, ag::Variant(sampling_hist)},
             {keys::kState, ag::Variant(state)}}));
     if (pairFeatures > 0 ) {
       frames.back()[keys::kPairsData] = torch::normal(torch::zeros({nEnemies * nAllies, pairFeatures})).to(torch::kCPU);
     }
     if (memory) {
       torch::Tensor neighb = torch::zeros({nAllies, nAllies});
       auto acc = neighb.accessor<float,2>();
       for(int j = 0; j < nAllies; ++j){
         for(int k = 0; k < nAllies; ++k){
           acc[j][k] = coin(gen) ? 1. : 0.;
         }
       }
       frames.back()[keys::kMaskKey] = neighb.view({-1});
     }
   }
   if (pairFeatures > 0) {
     for (const auto& f : frames) {
       std::cout << "pairs "<<common::tensorInfo(f[keys::kPairsData])<<std::endl;
     }
   }
   return frames;
}

const lest::test specification[] =
{
    CASE( "Test of crossProduct_indices" )
    {
      auto A = torch::zeros({1}).fill_(3).to(torch::kLong);
      auto B = torch::zeros({1}).fill_(2).to(torch::kLong);
      auto res = crossProduct_indices(A,B, torch::kCPU);
      std::vector<long> targetA {0,0,1,1,2,2};
      std::vector<long> targetB {0,1,0,1,0,1};
      EXPECT(check_vec(res.first, targetA));
      EXPECT(check_vec(res.second, targetB));

      A = fromVec({3,2});
      B = fromVec({1,3});
      res = crossProduct_indices(A,B, torch::kCPU);
      std::vector<long> target2A {0,1,2,3,3,3,4,4,4};
      std::vector<long> target2B {0,0,0,1,2,3,1,2,3};
      EXPECT(check_vec(res.first, target2A));
      EXPECT(check_vec(res.second, target2B));

    },

    CASE("Custom batching")
    {
      auto model = TargetingModel().inFeatures(cherrypi::SimpleUnitFeaturizer::kNumChannels).model_type(ModelType::LP_DM).make();
      model->to(torch::kCUDA);
      auto batcher = std::make_shared<CustomBatcher>(model, 32);
      std::vector<long> num_allies, num_enemies;
      const int nFrames = 10;
      const int kEnemyFeats = 35;
      const int kAllyFeats = 36;
      const int dimX = 5;
      const int dimY = 7;
      const int feats = 3;
      std::vector<ag::Variant> frames = generateBatch(nFrames, kEnemyFeats, kAllyFeats, dimX, dimY, feats, num_allies, num_enemies, true);

      auto batch = batcher->makeBatch(frames);

      EXPECT(check_vec(batch[keys::kNumEnemies], num_enemies));
      EXPECT(check_vec(batch[keys::kNumAllies], num_allies));

      int indEnemy = 0;
      int indAlly = 0;
      int indSample = 0;
      for (int i = 0; i < nFrames; ++i) {
        EXPECT(equals(frames[i][keys::kState], batch[keys::kState][i]));
        for (int j = 0; j < num_allies[i] * num_enemies[i] + num_enemies[i] * num_enemies[i]; ++j) {
          EXPECT(equals(frames[i][keys::kSamplingHist][j], batch[keys::kSamplingHist][indSample]));
          indSample++;
        }
        for (int j = 0; j < num_allies[i]; ++j) {
          EXPECT(equals(frames[i][keys::kAllyData][j], batch[keys::kAllyData][indAlly]));
          EXPECT(equals(frames[i][keys::kAllyPos][j], batch[keys::kAllyPos][indAlly]));
          indAlly++;
        }
        for (int j = 0; j < num_enemies[i]; ++j) {
          EXPECT(equals(frames[i][keys::kEnemyData][j], batch[keys::kEnemyData][indEnemy]));
          EXPECT(equals(frames[i][keys::kEnemyPos][j], batch[keys::kEnemyPos][indEnemy]));
          indEnemy++;
        }
      }

    },

    CASE("Test of model LP_DM, no sampling hist")
    {
      FLAGS_correlated_steps = 1;
      Parameters::init();
      auto model = TargetingModel().inFeatures(cherrypi::SimpleUnitFeaturizer::kNumChannels).model_type(ModelType::LP_DM).zeroLastLayer(false).make();
      model->to(torch::kCUDA);
      auto batcher = std::make_shared<CustomBatcher>(model, 32);
      std::vector<long> num_allies, num_enemies;
      const int nFrames = 10;
      const int kEnemyFeats = cherrypi::SimpleUnitFeaturizer::kNumChannels;
      const int kAllyFeats = cherrypi::SimpleUnitFeaturizer::kNumChannels;
      const int dimX = FLAGS_map_dim;
      const int dimY = FLAGS_map_dim;
      const int feats = cherrypi::SimpleUnitFeaturizer::kNumChannels * 2;
      std::vector<ag::Variant> frames = generateBatch(nFrames, kEnemyFeats, kAllyFeats, dimX, dimY, feats, num_allies, num_enemies, false);

      auto batch = batcher->makeBatch(frames);
      batch = common::applyTransform(batch, [](torch::Tensor x){return x.to(torch::kCUDA);});

      auto forwarded = model->forward(batch);
      forwarded = common::applyTransform(forwarded, [](torch::Tensor x){return x.to(torch::kCPU);});
      model->to(torch::kCPU);

      std::vector<ag::Variant> unbatched = batcher->unBatch(forwarded, false, 0);
      torch::Tensor policy = forwarded[keys::kPiKey];
      torch::Tensor ppolicy = forwarded[keys::kPiPlayKey];
      EXPECT(equal(policy,ppolicy));

      // we check the features of all pairs of (ally, enemy)
      int currentStart = 0;
      for (int i = 0; i < nFrames; ++i ) {
        torch::Tensor cur_policy = policy.slice(0, currentStart, currentStart + num_allies[i] * num_enemies[i] );
        cur_policy = cur_policy.view({num_allies[i], num_enemies[i]});
        currentStart += num_allies[i] * num_enemies[i];
        auto pol = cur_policy.accessor<float, 2>();

        // check that the unbatched policy matches the one we just manually unbatched
        EXPECT(equal(cur_policy.view(-1), unbatched[i][keys::kPiKey].view(-1)));
        EXPECT(equal(cur_policy.view(-1), unbatched[i][keys::kPiPlayKey].view(-1)));

        for (int j = 0; j < num_allies[i]; ++j) {
          auto feat_ally = frames[i][keys::kAllyData][j].view({1, kAllyFeats});
          feat_ally = model->agentEmbed_->forward(feat_ally)[0];
          for (int k = 0; k < num_enemies[i]; ++k) {
            auto feat_enemy = frames[i][keys::kEnemyData][k].view({1, kEnemyFeats});
            feat_enemy = model->taskEmbed_->forward(feat_enemy)[0];
            auto concat = torch::cat({feat_ally, feat_enemy}, 1);
            float target = model->lpWeightsMLP_->forward({concat})[0].view({1}).item<float>();

            float error = target - pol[j][k];
            EXPECT(abs(error) < 1e-5);

          }
        }
      }
    },

    CASE("Test of model LP_DM, with sampling hist")
    {
      FLAGS_correlated_steps = 2;
      Parameters::init();
      auto model = TargetingModel().inFeatures(cherrypi::SimpleUnitFeaturizer::kNumChannels).model_type(ModelType::LP_DM).zeroLastLayer(false).make();
      model->to(torch::kCUDA);
      auto batcher = std::make_shared<CustomBatcher>(model, 32);
      std::vector<long> num_allies, num_enemies;
      const int nFrames = 10;
      const int kEnemyFeats = cherrypi::SimpleUnitFeaturizer::kNumChannels;
      const int kAllyFeats = cherrypi::SimpleUnitFeaturizer::kNumChannels;
      const int dimX = FLAGS_map_dim;
      const int dimY = FLAGS_map_dim;
      const int feats = cherrypi::SimpleUnitFeaturizer::kNumChannels * 2;
      std::vector<ag::Variant> frames = generateBatch(nFrames, kEnemyFeats, kAllyFeats, dimX, dimY, feats, num_allies, num_enemies, false);

      auto batch = batcher->makeBatch(frames);
      batch = common::applyTransform(batch, [](torch::Tensor x){return x.to(torch::kCUDA);});

      auto forwarded = model->forward(batch);
      forwarded = common::applyTransform(forwarded, [](torch::Tensor x){return x.to(torch::kCPU);});
      model->to(torch::kCPU);
      std::vector<ag::Variant> unbatched = batcher->unBatch(forwarded, false, 0);
      torch::Tensor policy = forwarded[keys::kPiKey];
      torch::Tensor ppolicy = forwarded[keys::kPiPlayKey];

      // we check the features of all pairs of (ally, enemy)
      int currentStart = 0;
      for (int i = 0; i < nFrames; ++i ) {
        torch::Tensor cur_policy = policy.slice(0, currentStart, currentStart + num_allies[i] * num_enemies[i] );
        cur_policy = cur_policy.view({num_allies[i], num_enemies[i]});
        torch::Tensor cur_ppolicy = ppolicy.slice(0, currentStart, currentStart + num_allies[i] * num_enemies[i] );
        cur_ppolicy = cur_ppolicy.view({num_allies[i], num_enemies[i]});
        currentStart += num_allies[i] * num_enemies[i];
        auto pol = cur_policy.accessor<float, 2>();
        auto ppol = cur_ppolicy.accessor<float, 2>();

        // check that the unbatched policy matches the one we just manually unbatched
        EXPECT(equal(cur_policy.view(-1), unbatched[i][keys::kPiKey].view(-1)));
        EXPECT(equal(cur_ppolicy.view(-1), unbatched[i][keys::kPiPlayKey].view(-1)));

        torch::Tensor sampl_hist_lin_tens = frames[i][keys::kSamplingHist]
          .view({num_allies[i], num_enemies[i]});

        auto sampl_hist_lin = sampl_hist_lin_tens.accessor<float, 2>();

        for (int j = 0; j < num_allies[i]; ++j) {
          auto feat_ally = frames[i][keys::kAllyData][j].view({1, kAllyFeats});
          feat_ally = model->agentEmbed_->forward(feat_ally)[0];
          for (int k = 0; k < num_enemies[i]; ++k) {
            auto feat_enemy = frames[i][keys::kEnemyData][k].view({1, kEnemyFeats});
            feat_enemy = model->taskEmbed_->forward(feat_enemy)[0];
            auto concat = torch::cat({feat_ally, feat_enemy}, 1);
            float target = model->lpWeightsMLP_->forward({concat})[0].view({1}).item<float>();

            float error = target - pol[j][k];
            EXPECT(abs(error) < 1e-5);

            target += sampl_hist_lin[j][k];
            float error2 = target - ppol[j][k];
            EXPECT(abs(error2) < 1e-5);

          }
        }
      }
    },

    CASE("Test of model LP_PEM, no sampling hist")
    {
      FLAGS_correlated_steps = 1;
      Parameters::init();
      auto model = TargetingModel().inFeatures(cherrypi::SimpleUnitFeaturizer::kNumChannels).model_type(ModelType::LP_PEM).zeroLastLayer(false).make();
      model->to(torch::kCUDA);
      auto batcher = std::make_shared<CustomBatcher>(model, 32);
      std::vector<long> num_allies, num_enemies;
      const int nFrames = 10;
      const int kEnemyFeats = cherrypi::SimpleUnitFeaturizer::kNumChannels;
      const int kAllyFeats = cherrypi::SimpleUnitFeaturizer::kNumChannels;
      const int dimX = FLAGS_map_dim;
      const int dimY = FLAGS_map_dim;
      const int feats = cherrypi::SimpleUnitFeaturizer::kNumChannels * 2;
      std::vector<ag::Variant> frames = generateBatch(nFrames, kEnemyFeats, kAllyFeats, dimX, dimY, feats, num_allies, num_enemies, false );

      auto batch = batcher->makeBatch(frames);
      batch = common::applyTransform(batch, [](torch::Tensor x){return x.to(torch::kCUDA);});

      auto forwarded = model->forward(batch);
      forwarded = common::applyTransform(forwarded, [](torch::Tensor x){return x.to(torch::kCPU);});
      std::vector<ag::Variant> unbatched = batcher->unBatch(forwarded, false, 0);
      torch::Tensor policy = forwarded[keys::kPiKey];
      torch::Tensor ppolicy = forwarded[keys::kPiPlayKey];
      EXPECT((policy.max() - policy.min()).item<float>() > 0.01);
      EXPECT(equal(policy,ppolicy));

      torch::Tensor pos_emb = model->policyTrunk_->forward({batch[keys::kState]})[0].to(torch::kCPU);
      common::assertSize("pos_emb", pos_emb, {nFrames, (int)FLAGS_conv_embed_size * 2, dimY, dimX});
      model->to(torch::kCPU);

      // we check the features of all pairs of (ally, enemy)
      int currentStart = 0;
      for (int i = 0; i < nFrames; ++i ) {
        torch::Tensor cur_policy = policy.slice(0, currentStart, currentStart + num_allies[i] * num_enemies[i] );
        cur_policy = cur_policy.view({num_allies[i], num_enemies[i]});
        currentStart += num_allies[i] * num_enemies[i];
        auto pol = cur_policy.accessor<float, 2>();

        // check that the unbatched policy matches the one we just manually unbatched
        EXPECT(equal(cur_policy.view(-1), unbatched[i][keys::kPiKey].view(-1)));
        EXPECT(equal(cur_policy.view(-1), unbatched[i][keys::kPiPlayKey].view(-1)));

        auto pos_ally_acc = frames[i][keys::kAllyPos].accessor<long, 2>();
        auto pos_enemy_acc = frames[i][keys::kEnemyPos].accessor<long, 2>();
        for (int j = 0; j < num_allies[i]; ++j) {
          auto feat_ally = frames[i][keys::kAllyData][j].view({1, kAllyFeats});
          auto conv_feat_ally = pos_emb[i]
            .select(2, pos_ally_acc[j][1])
            .select(1, pos_ally_acc[j][0])
            .view({1, 2 * (int)FLAGS_conv_embed_size})
            .slice(1, 0, FLAGS_conv_embed_size);

          feat_ally = torch::cat({feat_ally, conv_feat_ally}, 1);
          feat_ally = model->agentEmbed_->forward(feat_ally)[0];
          for (int k = 0; k < num_enemies[i]; ++k) {
            auto feat_enemy = frames[i][keys::kEnemyData][k].view({1, kEnemyFeats});
            auto conv_feat_enemy = pos_emb[i]
              .select(2, pos_enemy_acc[k][1])
              .select(1, pos_enemy_acc[k][0])
              .view({1, 2 *(int) FLAGS_conv_embed_size})
              .slice(1, (int)FLAGS_conv_embed_size, 2* (int)FLAGS_conv_embed_size);
            feat_enemy = torch::cat({feat_enemy, conv_feat_enemy}, 1);
            feat_enemy = model->taskEmbed_->forward(feat_enemy)[0];

            auto concat = torch::cat({feat_ally, feat_enemy}, 1);
            float target = model->lpWeightsMLP_->forward({concat})[0].view({1}).item<float>();

            float error = target - pol[j][k];
            EXPECT(abs(error) < 1e-5);

          }
        }
      }
    },

    CASE("Test of model Quad_DM, no sampling hist")
    {
      FLAGS_correlated_steps = 1;
      Parameters::init();
      auto model = TargetingModel().inFeatures(cherrypi::SimpleUnitFeaturizer::kNumChannels).model_type(ModelType::Quad_DM).zeroLastLayer(false).make();
      model->to(torch::kCUDA);
      auto batcher = std::make_shared<CustomBatcher>(model, 32);
      std::vector<long> num_allies, num_enemies;
      const int nFrames = 10;
      const int kEnemyFeats = cherrypi::SimpleUnitFeaturizer::kNumChannels;
      const int kAllyFeats = cherrypi::SimpleUnitFeaturizer::kNumChannels;
      const int dimX = FLAGS_map_dim;
      const int dimY = FLAGS_map_dim;
      const int feats = cherrypi::SimpleUnitFeaturizer::kNumChannels * 2;
      std::vector<ag::Variant> frames = generateBatch(nFrames, kEnemyFeats, kAllyFeats, dimX, dimY, feats, num_allies, num_enemies, true);

      auto batch = batcher->makeBatch(frames);
      batch = common::applyTransform(batch, [](torch::Tensor x){return x.to(torch::kCUDA);});

      auto forwarded = model->forward(batch);
      forwarded = common::applyTransform(forwarded, [](torch::Tensor x){return x.to(torch::kCPU);});
      model->to(torch::kCPU);
      std::vector<ag::Variant> unbatched = batcher->unBatch(forwarded, false, 0);
      torch::Tensor policy = forwarded[keys::kPiKey];
      EXPECT((policy.max() - policy.min()).item<float>() > 0.1);
      torch::Tensor ppolicy = forwarded[keys::kPiPlayKey];
      EXPECT(equal(policy,ppolicy));

      // we check the features of all pairs of (ally, enemy) and (enemy, enemy)
      int currentStart = 0;
      for (int i = 0; i < nFrames; ++i ) {
        torch::Tensor cur_policy = policy.slice(0, currentStart, currentStart + num_allies[i] * num_enemies[i] );
        cur_policy = cur_policy.view({num_allies[i], num_enemies[i]});
        currentStart += num_allies[i] * num_enemies[i];
        auto pol = cur_policy.accessor<float, 2>();

        torch::Tensor cur_policy_quad = policy.slice(0, currentStart, currentStart + num_enemies[i] * num_enemies[i] );
        cur_policy_quad = cur_policy_quad.view({num_enemies[i], num_enemies[i]});
        currentStart += num_enemies[i] * num_enemies[i];
        auto pol_quad = cur_policy_quad.accessor<float, 2>();

        // check that the unbatched policy matches the one we just manually unbatched
        EXPECT(equal(torch::cat({cur_policy.view(-1), cur_policy_quad.view(-1)},0), unbatched[i][keys::kPiKey].view(-1)));
        EXPECT(equal(torch::cat({cur_policy.view(-1), cur_policy_quad.view(-1)},0), unbatched[i][keys::kPiPlayKey].view(-1)));

        for (int j = 0; j < num_allies[i]; ++j) {
          auto feat_ally = frames[i][keys::kAllyData][j].view({1, kAllyFeats});
          feat_ally = model->agentEmbed_->forward(feat_ally)[0];
          for (int k = 0; k < num_enemies[i]; ++k) {
            auto feat_enemy = frames[i][keys::kEnemyData][k].view({1, kEnemyFeats});
            feat_enemy = model->taskEmbed_->forward(feat_enemy)[0];
            auto concat = torch::cat({feat_ally, feat_enemy}, 1);
            float target = model->lpWeightsMLP_->forward({concat})[0].view({1}).item<float>();
            float error = target - pol[j][k];
            // std::cout << target << " -- "<<pol[j][k]<<std::endl;
            EXPECT(abs(error) < 1e-5);
          }
        }
        // quad part
        for (int j = 0; j < num_enemies[i]; ++j) {
          auto feat_enemyA = frames[i][keys::kEnemyData][j].view({1, kEnemyFeats});
          feat_enemyA = model->taskEmbed_->forward(feat_enemyA)[0];
          for (int k = 0; k < num_enemies[i]; ++k) {
            auto feat_enemyB = frames[i][keys::kEnemyData][k].view({1, kEnemyFeats});
            feat_enemyB = model->taskEmbed_->forward(feat_enemyB)[0];
            auto concat = torch::cat({feat_enemyA, feat_enemyB}, 1);
            float target = model->quadWeightsMLP_->forward({concat})[0].view({1}).item<float>();
            float error = target - pol_quad[j][k];
            // std::cout << "quad "<< target << " -- "<<pol_quad[j][k]<<std::endl;
            EXPECT(abs(error) < 1e-5);
          }
        }
      }
    },

    CASE("Test of model QUAD_PEM, no sampling hist")
    {
      FLAGS_correlated_steps = 1;
      Parameters::init();
      auto model = TargetingModel().inFeatures(cherrypi::SimpleUnitFeaturizer::kNumChannels).model_type(ModelType::Quad_PEM).zeroLastLayer(false).make();
      model->to(torch::kCUDA);
      auto batcher = std::make_shared<CustomBatcher>(model, 32);
      std::vector<long> num_allies, num_enemies;
      const int nFrames = 10;
      const int kEnemyFeats = cherrypi::SimpleUnitFeaturizer::kNumChannels;
      const int kAllyFeats = cherrypi::SimpleUnitFeaturizer::kNumChannels;
      const int dimX = FLAGS_map_dim;
      const int dimY = FLAGS_map_dim;
      const int feats = cherrypi::SimpleUnitFeaturizer::kNumChannels * 2;
      std::vector<ag::Variant> frames = generateBatch(nFrames, kEnemyFeats, kAllyFeats, dimX, dimY, feats, num_allies, num_enemies, true);

      auto batch = batcher->makeBatch(frames);
      batch = common::applyTransform(batch, [](torch::Tensor x){return x.to(torch::kCUDA);});

      auto forwarded = model->forward(batch);
      forwarded = common::applyTransform(forwarded, [](torch::Tensor x){return x.to(torch::kCPU);});
      std::vector<ag::Variant> unbatched = batcher->unBatch(forwarded, false, 0);
      torch::Tensor policy = forwarded[keys::kPiKey];
      torch::Tensor ppolicy = forwarded[keys::kPiPlayKey];
      EXPECT(equal(policy,ppolicy));

      torch::Tensor pos_emb = model->policyTrunk_->forward({batch[keys::kState]})[0].to(torch::kCPU);
      common::assertSize("pos_emb", pos_emb, {nFrames, (int)FLAGS_conv_embed_size * 2, dimY, dimX});
      model->to(torch::kCPU);

      // we check the features of all pairs of (ally, enemy)
      int currentStart = 0;
      for (int i = 0; i < nFrames; ++i ) {
        torch::Tensor cur_policy = policy.slice(0, currentStart, currentStart + num_allies[i] * num_enemies[i] );
        cur_policy = cur_policy.view({num_allies[i], num_enemies[i]});
        currentStart += num_allies[i] * num_enemies[i];
        auto pol = cur_policy.accessor<float, 2>();

        torch::Tensor cur_policy_quad = policy.slice(0, currentStart, currentStart + num_enemies[i] * num_enemies[i] );
        cur_policy_quad = cur_policy_quad.view({num_enemies[i], num_enemies[i]});
        currentStart += num_enemies[i] * num_enemies[i];
        auto pol_quad = cur_policy_quad.accessor<float, 2>();

        // check that the unbatched policy matches the one we just manually unbatched
        EXPECT(equal(torch::cat({cur_policy.view(-1), cur_policy_quad.view(-1)},0), unbatched[i][keys::kPiKey].view(-1)));
        EXPECT(equal(torch::cat({cur_policy.view(-1), cur_policy_quad.view(-1)},0), unbatched[i][keys::kPiPlayKey].view(-1)));


        auto pos_ally_acc = frames[i][keys::kAllyPos].accessor<long, 2>();
        auto pos_enemy_acc = frames[i][keys::kEnemyPos].accessor<long, 2>();
        for (int j = 0; j < num_allies[i]; ++j) {
          auto feat_ally = frames[i][keys::kAllyData][j].view({1, kAllyFeats});
          auto conv_feat_ally = pos_emb[i]
            .select(2, pos_ally_acc[j][1])
            .select(1, pos_ally_acc[j][0])
            .view({1, 2 * (int)FLAGS_conv_embed_size})
            .slice(1, 0, FLAGS_conv_embed_size);

          feat_ally = torch::cat({feat_ally, conv_feat_ally}, 1);
          feat_ally = model->agentEmbed_->forward(feat_ally)[0];
          for (int k = 0; k < num_enemies[i]; ++k) {
            auto feat_enemy = frames[i][keys::kEnemyData][k].view({1, kEnemyFeats});
            auto conv_feat_enemy = pos_emb[i]
              .select(2, pos_enemy_acc[k][1])
              .select(1, pos_enemy_acc[k][0])
              .view({1, 2 *(int) FLAGS_conv_embed_size})
              .slice(1, (int)FLAGS_conv_embed_size, 2* (int)FLAGS_conv_embed_size);
            feat_enemy = torch::cat({feat_enemy, conv_feat_enemy}, 1);
            feat_enemy = model->taskEmbed_->forward(feat_enemy)[0];

            auto concat = torch::cat({feat_ally, feat_enemy}, 1);
            float target = model->lpWeightsMLP_->forward({concat})[0].view({1}).item<float>();

            float error = target - pol[j][k];
            // std::cout << target << " --- "<<pol[j][k]<<std::endl;
            EXPECT(abs(error) < 1e-5);

          }
        }

        // quad part
        for (int j = 0; j < num_enemies[i]; ++j) {
          auto feat_enemy = frames[i][keys::kEnemyData][j].view({1, kEnemyFeats});
          auto conv_feat_enemy = pos_emb[i]
            .select(2, pos_enemy_acc[j][1])
            .select(1, pos_enemy_acc[j][0])
            .view({1, 2 * (int)FLAGS_conv_embed_size})
            .slice(1, (int)FLAGS_conv_embed_size, 2* (int)FLAGS_conv_embed_size);

          feat_enemy = torch::cat({feat_enemy, conv_feat_enemy}, 1);
          feat_enemy = model->taskEmbed_->forward(feat_enemy)[0];
          for (int k = 0; k < num_enemies[i]; ++k) {
            auto feat_enemy2 = frames[i][keys::kEnemyData][k].view({1, kEnemyFeats});
            auto conv_feat_enemy2 = pos_emb[i]
              .select(2, pos_enemy_acc[k][1])
              .select(1, pos_enemy_acc[k][0])
              .view({1, 2 *(int) FLAGS_conv_embed_size})
              .slice(1, (int)FLAGS_conv_embed_size, 2* (int)FLAGS_conv_embed_size);
            feat_enemy2 = torch::cat({feat_enemy2, conv_feat_enemy2}, 1);
            feat_enemy2 = model->taskEmbed_->forward(feat_enemy2)[0];

            auto concat = torch::cat({feat_enemy, feat_enemy2}, 1);
            float target = model->quadWeightsMLP_->forward({concat})[0].view({1}).item<float>();

            float error = target - pol_quad[j][k];
            // std::cout << target << " qqq "<<pol_quad[j][k]<<std::endl;
            EXPECT(abs(error) < 1e-5);

          }
        }
      }
    },

    CASE("Test of model QUAD_PEM, with sampling hist")
    {
      FLAGS_correlated_steps = 2;
      Parameters::init();
      auto model = TargetingModel().inFeatures(cherrypi::SimpleUnitFeaturizer::kNumChannels).model_type(ModelType::Quad_PEM).zeroLastLayer(false).make();
      model->to(torch::kCUDA);
      auto batcher = std::make_shared<CustomBatcher>(model, 32);
      std::vector<long> num_allies, num_enemies;
      const int nFrames = 10;
      const int kEnemyFeats = cherrypi::SimpleUnitFeaturizer::kNumChannels;
      const int kAllyFeats = cherrypi::SimpleUnitFeaturizer::kNumChannels;
      const int dimX = FLAGS_map_dim;
      const int dimY = FLAGS_map_dim;
      const int feats = cherrypi::SimpleUnitFeaturizer::kNumChannels * 2;
      std::vector<ag::Variant> frames = generateBatch(nFrames, kEnemyFeats, kAllyFeats, dimX, dimY, feats, num_allies, num_enemies, true);

      auto batch = batcher->makeBatch(frames);
      batch = common::applyTransform(batch, [](torch::Tensor x){return x.to(torch::kCUDA);});

      auto forwarded = model->forward(batch);
      forwarded = common::applyTransform(forwarded, [](torch::Tensor x){return x.to(torch::kCPU);});
      std::vector<ag::Variant> unbatched = batcher->unBatch(forwarded, false, 0);
      torch::Tensor policy = forwarded[keys::kPiKey];
      torch::Tensor ppolicy = forwarded[keys::kPiPlayKey];
      EXPECT((policy.max() - policy.min()).item<float>() > 0.1);

      torch::Tensor pos_emb = model->policyTrunk_->forward({batch[keys::kState]})[0].to(torch::kCPU);
      common::assertSize("pos_emb", pos_emb, {nFrames, (int)FLAGS_conv_embed_size * 2, dimY, dimX});
      model->to(torch::kCPU);

      // we check the features of all pairs of (ally, enemy)
      int currentStart = 0;
      for (int i = 0; i < nFrames; ++i ) {
        torch::Tensor cur_policy = policy.slice(0, currentStart, currentStart + num_allies[i] * num_enemies[i] );
        cur_policy = cur_policy.view({num_allies[i], num_enemies[i]});
        torch::Tensor cur_ppolicy = ppolicy.slice(0, currentStart, currentStart + num_allies[i] * num_enemies[i] );
        cur_ppolicy = cur_ppolicy.view({num_allies[i], num_enemies[i]});
        currentStart += num_allies[i] * num_enemies[i];
        auto pol = cur_policy.accessor<float, 2>();
        auto ppol = cur_ppolicy.accessor<float, 2>();

        torch::Tensor cur_policy_quad = policy.slice(0, currentStart, currentStart + num_enemies[i] * num_enemies[i] );
        cur_policy_quad = cur_policy_quad.view({num_enemies[i], num_enemies[i]});
        auto pol_quad = cur_policy_quad.accessor<float, 2>();
        torch::Tensor cur_ppolicy_quad = ppolicy.slice(0, currentStart, currentStart + num_enemies[i] * num_enemies[i] );
        cur_ppolicy_quad = cur_ppolicy_quad.view({num_enemies[i], num_enemies[i]});
        currentStart += num_enemies[i] * num_enemies[i];
        auto ppol_quad = cur_ppolicy_quad.accessor<float, 2>();

        auto pos_ally_acc = frames[i][keys::kAllyPos].accessor<long, 2>();
        auto pos_enemy_acc = frames[i][keys::kEnemyPos].accessor<long, 2>();

        // check that the unbatched policy matches the one we just manually unbatched
        EXPECT(equal(torch::cat({cur_policy.view(-1), cur_policy_quad.view(-1)},0), unbatched[i][keys::kPiKey].view(-1)));
        EXPECT(equal(torch::cat({cur_ppolicy.view(-1), cur_ppolicy_quad.view(-1)},0), unbatched[i][keys::kPiPlayKey].view(-1)));

        torch::Tensor sampl_hist_lin_tens = frames[i][keys::kSamplingHist].view({-1})
          .slice(0,0, num_enemies[i] * num_allies[i])
          .view({num_allies[i], num_enemies[i]});
        auto sampl_hist_lin = sampl_hist_lin_tens.accessor<float, 2>();

        torch::Tensor sampl_hist_quad_tens = frames[i][keys::kSamplingHist].view({-1})
          .slice(0, num_enemies[i] * num_allies[i], num_enemies[i] * num_allies[i] + num_enemies[i] * num_enemies[i])
          .view({num_enemies[i], num_enemies[i]});
        auto sampl_hist_quad = sampl_hist_quad_tens.accessor<float, 2>();

        for (int j = 0; j < num_allies[i]; ++j) {
          auto feat_ally = frames[i][keys::kAllyData][j].view({1, kAllyFeats});
          auto conv_feat_ally = pos_emb[i]
            .select(2, pos_ally_acc[j][1])
            .select(1, pos_ally_acc[j][0])
            .view({1, 2 * (int)FLAGS_conv_embed_size})
            .slice(1, 0, FLAGS_conv_embed_size);

          feat_ally = torch::cat({feat_ally, conv_feat_ally}, 1);
          feat_ally = model->agentEmbed_->forward(feat_ally)[0];
          for (int k = 0; k < num_enemies[i]; ++k) {
            auto feat_enemy = frames[i][keys::kEnemyData][k].view({1, kEnemyFeats});
            auto conv_feat_enemy = pos_emb[i]
              .select(2, pos_enemy_acc[k][1])
              .select(1, pos_enemy_acc[k][0])
              .view({1, 2 *(int) FLAGS_conv_embed_size})
              .slice(1, (int)FLAGS_conv_embed_size, 2* (int)FLAGS_conv_embed_size);
            feat_enemy = torch::cat({feat_enemy, conv_feat_enemy}, 1);
            feat_enemy = model->taskEmbed_->forward(feat_enemy)[0];

            auto concat = torch::cat({feat_ally, feat_enemy}, 1);

            // we check that the policy is the correct one, and the playPolicy incorporates the sampling hist
            float target = model->lpWeightsMLP_->forward({concat})[0].view({1}).item<float>();

            float error = target - pol[j][k];
             // std::cout << target << " -1- "<<pol[j][k]<<std::endl;
            EXPECT(abs(error) < 1e-5);

            target += sampl_hist_lin[j][k];
            float error2 = target - ppol[j][k];
            // std::cout << target << " -2- "<<ppol[j][k]<<std::endl;
            EXPECT(abs(error2) < 1e-5);

          }
        }

        // quad part
        for (int j = 0; j < num_enemies[i]; ++j) {
          auto feat_enemy = frames[i][keys::kEnemyData][j].view({1, kEnemyFeats});
          auto conv_feat_enemy = pos_emb[i]
            .select(2, pos_enemy_acc[j][1])
            .select(1, pos_enemy_acc[j][0])
            .view({1, 2 * (int)FLAGS_conv_embed_size})
            .slice(1, (int)FLAGS_conv_embed_size, 2* (int)FLAGS_conv_embed_size);

          feat_enemy = torch::cat({feat_enemy, conv_feat_enemy}, 1);
          feat_enemy = model->taskEmbed_->forward(feat_enemy)[0];
          for (int k = 0; k < num_enemies[i]; ++k) {
            auto feat_enemy2 = frames[i][keys::kEnemyData][k].view({1, kEnemyFeats});
            auto conv_feat_enemy2 = pos_emb[i]
              .select(2, pos_enemy_acc[k][1])
              .select(1, pos_enemy_acc[k][0])
              .view({1, 2 *(int) FLAGS_conv_embed_size})
              .slice(1, (int)FLAGS_conv_embed_size, 2* (int)FLAGS_conv_embed_size);
            feat_enemy2 = torch::cat({feat_enemy2, conv_feat_enemy2}, 1);
            feat_enemy2 = model->taskEmbed_->forward(feat_enemy2)[0];

            auto concat = torch::cat({feat_enemy, feat_enemy2}, 1);
            float target = model->quadWeightsMLP_->forward({concat})[0].view({1}).item<float>();
            float error = target - pol_quad[j][k];
             // std::cout << target << " q1q "<<pol_quad[j][k]<<std::endl;
            EXPECT(abs(error) < 1e-5);

            target += sampl_hist_quad[j][k];
            float error2 = target - ppol_quad[j][k];
            // std::cout << target << " q2q "<<ppol_quad[j][k]<<std::endl;
            EXPECT(abs(error2) < 1e-5);

          }
        }
      }
    },

    CASE("Test of model QUAD_PEM, with sampling hist and pairwise features")
    {
      FLAGS_correlated_steps = 2;
      Parameters::init();
      const int num_pair_feats = 2;
      auto model = TargetingModel().inFeatures(cherrypi::SimpleUnitFeaturizer::kNumChannels).model_type(ModelType::Quad_PEM).zeroLastLayer(false).inPairFeatures(num_pair_feats).make();
      model->to(torch::kCUDA);
      auto batcher = std::make_shared<CustomBatcher>(model, 32);
      std::vector<long> num_allies, num_enemies;
      const int nFrames = 10;
      const int kEnemyFeats = cherrypi::SimpleUnitFeaturizer::kNumChannels;
      const int kAllyFeats = cherrypi::SimpleUnitFeaturizer::kNumChannels;
      const int dimX = FLAGS_map_dim;
      const int dimY = FLAGS_map_dim;
      const int feats = cherrypi::SimpleUnitFeaturizer::kNumChannels * 2;
      std::vector<ag::Variant> frames = generateBatch(nFrames, kEnemyFeats, kAllyFeats, dimX, dimY, feats, num_allies, num_enemies, true, num_pair_feats);

      auto batch = batcher->makeBatch(frames);
      batch = common::applyTransform(batch, [](torch::Tensor x){return x.to(torch::kCUDA);});

      auto forwarded = model->forward(batch);
      forwarded = common::applyTransform(forwarded, [](torch::Tensor x){return x.to(torch::kCPU);});
      std::vector<ag::Variant> unbatched = batcher->unBatch(forwarded, false, 0);
      torch::Tensor policy = forwarded[keys::kPiKey];
      torch::Tensor ppolicy = forwarded[keys::kPiPlayKey];
      EXPECT((policy.max() - policy.min()).item<float>() > 0.1);

      torch::Tensor pos_emb = model->policyTrunk_->forward({batch[keys::kState]})[0].to(torch::kCPU);
      common::assertSize("pos_emb", pos_emb, {nFrames, (int)FLAGS_conv_embed_size * 2, dimY, dimX});
      model->to(torch::kCPU);

      // we check the features of all pairs of (ally, enemy)
      int currentStart = 0;
      for (int i = 0; i < nFrames; ++i ) {
        torch::Tensor cur_policy = policy.slice(0, currentStart, currentStart + num_allies[i] * num_enemies[i] );
        cur_policy = cur_policy.view({num_allies[i], num_enemies[i]});
        torch::Tensor cur_ppolicy = ppolicy.slice(0, currentStart, currentStart + num_allies[i] * num_enemies[i] );
        cur_ppolicy = cur_ppolicy.view({num_allies[i], num_enemies[i]});
        currentStart += num_allies[i] * num_enemies[i];
        auto pol = cur_policy.accessor<float, 2>();
        auto ppol = cur_ppolicy.accessor<float, 2>();

        torch::Tensor cur_policy_quad = policy.slice(0, currentStart, currentStart + num_enemies[i] * num_enemies[i] );
        cur_policy_quad = cur_policy_quad.view({num_enemies[i], num_enemies[i]});
        auto pol_quad = cur_policy_quad.accessor<float, 2>();
        torch::Tensor cur_ppolicy_quad = ppolicy.slice(0, currentStart, currentStart + num_enemies[i] * num_enemies[i] );
        cur_ppolicy_quad = cur_ppolicy_quad.view({num_enemies[i], num_enemies[i]});
        currentStart += num_enemies[i] * num_enemies[i];
        auto ppol_quad = cur_ppolicy_quad.accessor<float, 2>();

        auto pos_ally_acc = frames[i][keys::kAllyPos].accessor<long, 2>();
        auto pos_enemy_acc = frames[i][keys::kEnemyPos].accessor<long, 2>();

        // check that the unbatched policy matches the one we just manually unbatched
        EXPECT(equal(torch::cat({cur_policy.view(-1), cur_policy_quad.view(-1)},0), unbatched[i][keys::kPiKey].view(-1)));
        EXPECT(equal(torch::cat({cur_ppolicy.view(-1), cur_ppolicy_quad.view(-1)},0), unbatched[i][keys::kPiPlayKey].view(-1)));

        torch::Tensor sampl_hist_lin_tens = frames[i][keys::kSamplingHist].view({-1})
          .slice(0,0, num_enemies[i] * num_allies[i])
          .view({num_allies[i], num_enemies[i]});
        auto sampl_hist_lin = sampl_hist_lin_tens.accessor<float, 2>();

        torch::Tensor sampl_hist_quad_tens = frames[i][keys::kSamplingHist].view({-1})
          .slice(0, num_enemies[i] * num_allies[i], num_enemies[i] * num_allies[i] + num_enemies[i] * num_enemies[i])
          .view({num_enemies[i], num_enemies[i]});
        auto sampl_hist_quad = sampl_hist_quad_tens.accessor<float, 2>();

        auto pairwise_feats = frames[i][keys::kPairsData].view({num_allies[i], num_enemies[i], num_pair_feats});

        for (int j = 0; j < num_allies[i]; ++j) {
          auto feat_ally = frames[i][keys::kAllyData][j].view({1, kAllyFeats});
          auto conv_feat_ally = pos_emb[i]
            .select(2, pos_ally_acc[j][1])
            .select(1, pos_ally_acc[j][0])
            .view({1, 2 * (int)FLAGS_conv_embed_size})
            .slice(1, 0, FLAGS_conv_embed_size);

          feat_ally = torch::cat({feat_ally, conv_feat_ally}, 1);
          feat_ally = model->agentEmbed_->forward(feat_ally)[0];
          for (int k = 0; k < num_enemies[i]; ++k) {
            auto feat_enemy = frames[i][keys::kEnemyData][k].view({1, kEnemyFeats});
            auto conv_feat_enemy = pos_emb[i]
              .select(2, pos_enemy_acc[k][1])
              .select(1, pos_enemy_acc[k][0])
              .view({1, 2 *(int) FLAGS_conv_embed_size})
              .slice(1, (int)FLAGS_conv_embed_size, 2* (int)FLAGS_conv_embed_size);
            feat_enemy = torch::cat({feat_enemy, conv_feat_enemy}, 1);
            feat_enemy = model->taskEmbed_->forward(feat_enemy)[0];

            auto concat = torch::cat({feat_ally, feat_enemy}, 1);

            auto cur_pair_feats = pairwise_feats[j][k].view({1, num_pair_feats});
            concat = torch::cat({concat, cur_pair_feats}, 1);

            // we check that the policy is the correct one, and the playPolicy incorporates the sampling hist
            float target = model->lpWeightsMLP_->forward({concat})[0].view({1}).item<float>();

            float error = target - pol[j][k];
             // std::cout << target << " -1- "<<pol[j][k]<<std::endl;
            EXPECT(abs(error) < 1e-5);

            target += sampl_hist_lin[j][k];
            float error2 = target - ppol[j][k];
            // std::cout << target << " -2- "<<ppol[j][k]<<std::endl;
            EXPECT(abs(error2) < 1e-5);

          }
        }

        // quad part
        for (int j = 0; j < num_enemies[i]; ++j) {
          auto feat_enemy = frames[i][keys::kEnemyData][j].view({1, kEnemyFeats});
          auto conv_feat_enemy = pos_emb[i]
            .select(2, pos_enemy_acc[j][1])
            .select(1, pos_enemy_acc[j][0])
            .view({1, 2 * (int)FLAGS_conv_embed_size})
            .slice(1, (int)FLAGS_conv_embed_size, 2* (int)FLAGS_conv_embed_size);

          feat_enemy = torch::cat({feat_enemy, conv_feat_enemy}, 1);
          feat_enemy = model->taskEmbed_->forward(feat_enemy)[0];
          for (int k = 0; k < num_enemies[i]; ++k) {
            auto feat_enemy2 = frames[i][keys::kEnemyData][k].view({1, kEnemyFeats});
            auto conv_feat_enemy2 = pos_emb[i]
              .select(2, pos_enemy_acc[k][1])
              .select(1, pos_enemy_acc[k][0])
              .view({1, 2 *(int) FLAGS_conv_embed_size})
              .slice(1, (int)FLAGS_conv_embed_size, 2* (int)FLAGS_conv_embed_size);
            feat_enemy2 = torch::cat({feat_enemy2, conv_feat_enemy2}, 1);
            feat_enemy2 = model->taskEmbed_->forward(feat_enemy2)[0];

            auto concat = torch::cat({feat_enemy, feat_enemy2}, 1);
            float target = model->quadWeightsMLP_->forward({concat})[0].view({1}).item<float>();
            float error = target - pol_quad[j][k];
             // std::cout << target << " q1q "<<pol_quad[j][k]<<std::endl;
            EXPECT(abs(error) < 1e-5);

            target += sampl_hist_quad[j][k];
            float error2 = target - ppol_quad[j][k];
            // std::cout << target << " q2q "<<ppol_quad[j][k]<<std::endl;
            EXPECT(abs(error2) < 1e-5);

          }
        }
      }
    },

    CASE("Test of model QUAD_SPEM, with sampling hist and pairwise features")
    {
      FLAGS_correlated_steps = 2;
      Parameters::init();
      const int num_pair_feats = 2;
      auto model = TargetingModel().inFeatures(cherrypi::SimpleUnitFeaturizer::kNumChannels).model_type(ModelType::Quad_SPEM).zeroLastLayer(false).inPairFeatures(num_pair_feats).make();
      model->to(torch::kCUDA);
      auto batcher = std::make_shared<CustomBatcher>(model, 32);
      std::vector<long> num_allies, num_enemies;
      const int nFrames = 10;
      const int kEnemyFeats = cherrypi::SimpleUnitFeaturizer::kNumChannels;
      const int kAllyFeats = cherrypi::SimpleUnitFeaturizer::kNumChannels;
      const int dimX = FLAGS_map_dim;
      const int dimY = FLAGS_map_dim;
      const int feats = cherrypi::SimpleUnitFeaturizer::kNumChannels * 2;
      std::vector<ag::Variant> frames = generateBatch(nFrames, kEnemyFeats, kAllyFeats, dimX, dimY, feats, num_allies, num_enemies, true, num_pair_feats);

      auto batch = batcher->makeBatch(frames);
      batch = common::applyTransform(batch, [](torch::Tensor x){return x.to(torch::kCUDA);});

      auto forwarded = model->forward(batch);
      forwarded = common::applyTransform(forwarded, [](torch::Tensor x){return x.to(torch::kCPU);});
      std::vector<ag::Variant> unbatched = batcher->unBatch(forwarded, false, 0);
      torch::Tensor policy = forwarded[keys::kPiKey];
      torch::Tensor ppolicy = forwarded[keys::kPiPlayKey];
      EXPECT((policy.max() - policy.min()).item<float>() > 0.1);

      torch::Tensor pos_emb = model->policyTrunk_->forward({batch[keys::kState]})[0].to(torch::kCPU);
      common::assertSize("pos_emb", pos_emb, {nFrames, (int)FLAGS_conv_embed_size * 2, dimY, dimX});
      model->to(torch::kCPU);

      // we check the features of all pairs of (ally, enemy)
      int currentStart = 0;
      for (int i = 0; i < nFrames; ++i ) {
        torch::Tensor cur_policy = policy.slice(0, currentStart, currentStart + num_allies[i] * num_enemies[i] );
        cur_policy = cur_policy.view({num_allies[i], num_enemies[i]});
        torch::Tensor cur_ppolicy = ppolicy.slice(0, currentStart, currentStart + num_allies[i] * num_enemies[i] );
        cur_ppolicy = cur_ppolicy.view({num_allies[i], num_enemies[i]});
        currentStart += num_allies[i] * num_enemies[i];
        auto pol = cur_policy.accessor<float, 2>();
        auto ppol = cur_ppolicy.accessor<float, 2>();

        torch::Tensor cur_policy_quad = policy.slice(0, currentStart, currentStart + num_enemies[i] * num_enemies[i] );
        cur_policy_quad = cur_policy_quad.view({num_enemies[i], num_enemies[i]});
        auto pol_quad = cur_policy_quad.accessor<float, 2>();
        torch::Tensor cur_ppolicy_quad = ppolicy.slice(0, currentStart, currentStart + num_enemies[i] * num_enemies[i] );
        cur_ppolicy_quad = cur_ppolicy_quad.view({num_enemies[i], num_enemies[i]});
        currentStart += num_enemies[i] * num_enemies[i];
        auto ppol_quad = cur_ppolicy_quad.accessor<float, 2>();

        auto pos_ally_acc = frames[i][keys::kAllyPos].accessor<long, 2>();
        auto pos_enemy_acc = frames[i][keys::kEnemyPos].accessor<long, 2>();

        // check that the unbatched policy matches the one we just manually unbatched
        EXPECT(equal(torch::cat({cur_policy.view(-1), cur_policy_quad.view(-1)},0), unbatched[i][keys::kPiKey].view(-1)));
        EXPECT(equal(torch::cat({cur_ppolicy.view(-1), cur_ppolicy_quad.view(-1)},0), unbatched[i][keys::kPiPlayKey].view(-1)));

        torch::Tensor sampl_hist_lin_tens = frames[i][keys::kSamplingHist].view({-1})
          .slice(0,0, num_enemies[i] * num_allies[i])
          .view({num_allies[i], num_enemies[i]});
        auto sampl_hist_lin = sampl_hist_lin_tens.accessor<float, 2>();

        torch::Tensor sampl_hist_quad_tens = frames[i][keys::kSamplingHist].view({-1})
          .slice(0, num_enemies[i] * num_allies[i], num_enemies[i] * num_allies[i] + num_enemies[i] * num_enemies[i])
          .view({num_enemies[i], num_enemies[i]});
        auto sampl_hist_quad = sampl_hist_quad_tens.accessor<float, 2>();

        auto pairwise_feats = frames[i][keys::kPairsData].view({num_allies[i], num_enemies[i], num_pair_feats});

        for (int j = 0; j < num_allies[i]; ++j) {
          auto feat_ally = frames[i][keys::kAllyData][j].view({1, kAllyFeats});
          auto conv_feat_ally = pos_emb[i]
            .select(2, pos_ally_acc[j][1])
            .select(1, pos_ally_acc[j][0])
            .view({1, 2 * (int)FLAGS_conv_embed_size})
            .slice(1, 0, FLAGS_conv_embed_size);

          feat_ally = torch::cat({feat_ally, conv_feat_ally}, 1);
          feat_ally = model->agentEmbed_->forward(feat_ally)[0];
          for (int k = 0; k < num_enemies[i]; ++k) {
            auto feat_enemy = frames[i][keys::kEnemyData][k].view({1, kEnemyFeats});
            auto conv_feat_enemy = pos_emb[i]
              .select(2, pos_enemy_acc[k][1])
              .select(1, pos_enemy_acc[k][0])
              .view({1, 2 *(int) FLAGS_conv_embed_size})
              .slice(1, (int)FLAGS_conv_embed_size, 2* (int)FLAGS_conv_embed_size);
            feat_enemy = torch::cat({feat_enemy, conv_feat_enemy}, 1);
            feat_enemy = model->taskEmbed_->forward(feat_enemy)[0];

            auto concat = torch::cat({feat_ally, feat_enemy}, 1);

            auto cur_pair_feats = pairwise_feats[j][k].view({1, num_pair_feats});
            concat = torch::cat({concat, cur_pair_feats}, 1);

            // we check that the policy is the correct one, and the playPolicy incorporates the sampling hist
            float target = model->lpWeightsMLP_->forward({concat})[0].view({1}).item<float>();

            float error = target - pol[j][k];
             // std::cout << target << " -1- "<<pol[j][k]<<std::endl;
            EXPECT(abs(error) < 1e-5);

            target += sampl_hist_lin[j][k];
            float error2 = target - ppol[j][k];
            // std::cout << target << " -2- "<<ppol[j][k]<<std::endl;
            EXPECT(abs(error2) < 1e-5);

          }
        }

        // quad part
        for (int j = 0; j < num_enemies[i]; ++j) {
          auto feat_enemy = frames[i][keys::kEnemyData][j].view({1, kEnemyFeats});
          auto conv_feat_enemy = pos_emb[i]
            .select(2, pos_enemy_acc[j][1])
            .select(1, pos_enemy_acc[j][0])
            .view({1, 2 * (int)FLAGS_conv_embed_size})
            .slice(1, (int)FLAGS_conv_embed_size, 2* (int)FLAGS_conv_embed_size);

          feat_enemy = torch::cat({feat_enemy, conv_feat_enemy}, 1);
          feat_enemy = model->taskEmbed_->forward(feat_enemy)[0];
          for (int k = 0; k < num_enemies[i]; ++k) {
            auto feat_enemy2 = frames[i][keys::kEnemyData][k].view({1, kEnemyFeats});
            auto conv_feat_enemy2 = pos_emb[i]
              .select(2, pos_enemy_acc[k][1])
              .select(1, pos_enemy_acc[k][0])
              .view({1, 2 *(int) FLAGS_conv_embed_size})
              .slice(1, (int)FLAGS_conv_embed_size, 2* (int)FLAGS_conv_embed_size);
            feat_enemy2 = torch::cat({feat_enemy2, conv_feat_enemy2}, 1);
            feat_enemy2 = model->taskEmbed_->forward(feat_enemy2)[0];

            auto concat = torch::cat({feat_enemy, feat_enemy2}, 1);
            float target = model->quadWeightsMLP_->forward({concat})[0].view({1}).item<float>();
            float error = target - pol_quad[j][k];
             // std::cout << target << " q1q "<<pol_quad[j][k]<<std::endl;
            EXPECT(abs(error) < 1e-5);

            target += sampl_hist_quad[j][k];
            float error2 = target - ppol_quad[j][k];
            // std::cout << target << " q2q "<<ppol_quad[j][k]<<std::endl;
            EXPECT(abs(error2) < 1e-5);

          }
        }
      }
    },

    CASE("Test of model ARGMAX_DM_MEMORY, with sampling hist and pairwise features")
    {
      if(FLAGS_use_embeddings){
      FLAGS_correlated_steps = 2;
      Parameters::init();
      const int num_pair_feats = 2;
      auto model = TargetingModel().inFeatures(cherrypi::SimpleUnitFeaturizer::kNumChannels).model_type(ModelType::Argmax_PEM).zeroLastLayer(false).inPairFeatures(num_pair_feats).memoryModel(true).make();
      model->to(torch::kCUDA);
      auto batcher = std::make_shared<CustomBatcher>(model, 32);
      std::vector<long> num_allies, num_enemies;
      const int nFrames = 10;
      const int kEnemyFeats = cherrypi::SimpleUnitFeaturizer::kNumChannels;
      const int kAllyFeats = cherrypi::SimpleUnitFeaturizer::kNumChannels;
      const int dimX = FLAGS_map_dim;
      const int dimY = FLAGS_map_dim;
      const int feats = cherrypi::SimpleUnitFeaturizer::kNumChannels * 2;
      std::vector<ag::Variant> frames = generateBatch(nFrames, kEnemyFeats, kAllyFeats, dimX, dimY, feats, num_allies, num_enemies, false, num_pair_feats, true);

      auto batch = batcher->makeBatch(frames);
      batch = common::applyTransform(batch, [](torch::Tensor x){return x.to(torch::kCUDA);});

      auto forwarded = model->forward(batch);
      forwarded = common::applyTransform(forwarded, [](torch::Tensor x){return x.to(torch::kCPU);});
      std::vector<ag::Variant> unbatched = batcher->unBatch(forwarded, false, 0);
      torch::Tensor policy = forwarded[keys::kPiKey];
      torch::Tensor ppolicy = forwarded[keys::kPiPlayKey];

      torch::Tensor pos_emb = model->policyTrunk_->forward({batch[keys::kState]})[0].to(torch::kCPU);
      common::assertSize("pos_emb", pos_emb, {nFrames, (int)FLAGS_conv_embed_size * 2, dimY, dimX});
      model->to(torch::kCPU);

      // we check the features of all pairs of (ally, enemy)
      int currentStart = 0;
      for (int i = 0; i < nFrames; ++i ) {
        torch::Tensor cur_policy = policy.slice(0, currentStart, currentStart + num_allies[i] * num_enemies[i] );
        cur_policy = cur_policy.view({num_allies[i], num_enemies[i]});
        torch::Tensor cur_ppolicy = ppolicy.slice(0, currentStart, currentStart + num_allies[i] * num_enemies[i] );
        cur_ppolicy = cur_ppolicy.view({num_allies[i], num_enemies[i]});
        currentStart += num_allies[i] * num_enemies[i];
        auto pol = cur_policy.accessor<float, 2>();
        auto ppol = cur_ppolicy.accessor<float, 2>();


        auto pos_ally_acc = frames[i][keys::kAllyPos].accessor<long, 2>();
        auto pos_enemy_acc = frames[i][keys::kEnemyPos].accessor<long, 2>();

        // check that the unbatched policy matches the one we just manually unbatched
        EXPECT(equal(cur_policy.view(-1), unbatched[i][keys::kPiKey].view(-1)));
        EXPECT(equal(cur_ppolicy.view(-1), unbatched[i][keys::kPiPlayKey].view(-1)));

        torch::Tensor sampl_hist_lin_tens = frames[i][keys::kSamplingHist].view({-1})
          .slice(0,0, num_enemies[i] * num_allies[i])
          .view({num_allies[i], num_enemies[i]});
        auto sampl_hist_lin = sampl_hist_lin_tens.accessor<float, 2>();

        auto pairwise_feats = frames[i][keys::kPairsData].view({num_allies[i], num_enemies[i], num_pair_feats});

        /*
        for (int j = 0; j < num_allies[i]; ++j) {
          auto feat_ally = frames[i][keys::kAllyData][j].view({1, kAllyFeats});
          auto conv_feat_ally = pos_emb[i]
            .select(2, pos_ally_acc[j][1])
            .select(1, pos_ally_acc[j][0])
            .view({1, 2 * (int)FLAGS_conv_embed_size})
            .slice(1, 0, FLAGS_conv_embed_size);

          feat_ally = torch::cat({feat_ally, conv_feat_ally}, 1);
          feat_ally = model->agentEmbed_->forward(feat_ally)[0];
          for (int k = 0; k < num_enemies[i]; ++k) {
            auto feat_enemy = frames[i][keys::kEnemyData][k].view({1, kEnemyFeats});
            auto conv_feat_enemy = pos_emb[i]
              .select(2, pos_enemy_acc[k][1])
              .select(1, pos_enemy_acc[k][0])
              .view({1, 2 *(int) FLAGS_conv_embed_size})
              .slice(1, (int)FLAGS_conv_embed_size, 2* (int)FLAGS_conv_embed_size);
            feat_enemy = torch::cat({feat_enemy, conv_feat_enemy}, 1);
            feat_enemy = model->taskEmbed_->forward(feat_enemy)[0];

            auto concat = torch::cat({feat_ally, feat_enemy}, 1);

            auto cur_pair_feats = pairwise_feats[j][k].view({1, num_pair_feats});
            concat = torch::cat({concat, cur_pair_feats}, 1);

            // we check that the policy is the correct one, and the playPolicy incorporates the sampling hist
            float target = model->lpWeightsMLP_->forward({concat})[0].view({1}).item<float>();

            float error = target - pol[j][k];
             // std::cout << target << " -1- "<<pol[j][k]<<std::endl;
            EXPECT(abs(error) < 1e-5);

            target += sampl_hist_lin[j][k];
            float error2 = target - ppol[j][k];
            // std::cout << target << " -2- "<<ppol[j][k]<<std::endl;
            EXPECT(abs(error2) < 1e-5);

          }
          }*/

      }
      }
    },

};

int main()
{
  torch::manual_seed(43);
  lest::run( specification /*, argc, argv, std::cout */  );
  FLAGS_use_embeddings = true;
  return lest::run( specification /*, argc, argv, std::cout */  );
}
