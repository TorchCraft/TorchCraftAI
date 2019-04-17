/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "common/autograd.h"
#include "cpid/optimizers.h"
#include "test.h"
#include <autogradpp/autograd.h>
#include <common/autograd/debug.h>
#include <common/autograd/models.h>
#include <common/autograd/operations.h>
#include <common/rand.h>
#include <common/serialization.h>
#include <glog/logging.h>
CASE("autograd/variant_ref/dict") {
  ag::Variant test = ag::VariantDict{{"key", torch::zeros({4, 5})}};
  EXPECT(test["key"].size(0) == 4);
  EXPECT(test["key"].size(1) == 5);
  EXPECT(test["key"].view(-1).size(0) == 20);
  test["key"].fill_(1);
  EXPECT(test["key"].sum().item<int32_t>() == 20);
}

class DummyTrainer {
 public:
  ag::Container model_ = ag::Linear(5, 1).make();
  ag::Optimizer optim_;

  template <class Archive>
  void save(Archive& ar) const {
    ar(CEREAL_NVP(*model_));
    ar(CEREAL_NVP(optim_));
  }

  template <class Archive>
  void load(Archive& ar) {
    ar(CEREAL_NVP(*model_));
    ar(CEREAL_NVP(optim_));
    optim_->add_parameters(model_->parameters());
    optim_->zero_grad();
  }
};

CASE("autograd/load_model") {
  {
    auto dummy = std::make_shared<DummyTrainer>();
    dummy->optim_ = cpid::selectOptimizer(dummy->model_);
    auto input = torch::randn({5});
    auto output = dummy->model_->forward(input)[0];
    output.backward();
    dummy->optim_->step();
    ag::save("test.bin", dummy);
    for (auto& var : dummy->model_->parameters()) {
      EXPECT(var.grad().defined() == true);
    }
  }
  {
    auto dummy = std::make_shared<DummyTrainer>();
    ag::load("test.bin", dummy);
    auto input = torch::randn({5});
    auto output = dummy->model_->forward(input)[0];
    output.backward();
    dummy->optim_->step();
    for (auto& var : dummy->model_->parameters()) {
      EXPECT(var.grad().defined() == true);
    }
  }
}

CASE("autograd/variant_ref/dict/singleton_list") {
  ag::Variant test =
      ag::VariantDict{{"key", ag::Variant{torch::zeros({4, 5})}}};
  EXPECT(test["key"].size(0) == 4);
  EXPECT(test["key"].size(1) == 5);
  EXPECT(test["key"].view(-1).size(0) == 20);
  test["key"].fill_(1);
  EXPECT(test["key"].sum().item<int32_t>() == 20);
}

CASE("autograd/variant_ref/list") {
  ag::Variant test = ag::Variant{torch::zeros({4, 5})};
  EXPECT(test[0].size(0) == 4);
  EXPECT(test[0].size(1) == 5);
  EXPECT(test[0].view(-1).size(0) == 20);
  test[0].fill_(1);
  EXPECT(test[0].sum().item<int32_t>() == 20);
}

CASE("autograd/variant/dict/insert") {
  ag::Variant test = ag::VariantDict{{"key", torch::zeros({4, 5})}};
  EXPECT(test.getDict().size() == 1u);
  test["key"] = torch::zeros({1, 2});
  EXPECT(test.getDict().size() == 1u);
  test["key2"] = torch::zeros({1, 2});
  EXPECT(test.getDict().size() == 2u);
  EXPECT(test["key3"].defined() == false);
}

CASE("autograd/variant/dict/const") {
  ag::Variant const test = ag::VariantDict{{"key", torch::zeros({4, 5})}};
  EXPECT(test.getDict().size() == 1u);
  // key2 doesn't exist, so we should not be able to create it
  EXPECT_THROWS(test["key2"]);
}

CASE("autograd/variant/list/oob") {
  ag::Variant test =
      ag::tensor_list{torch::zeros({4, 5}), torch::zeros({1, 2})};
  EXPECT_THROWS(test[2]);
  EXPECT_THROWS(test[10]);
}

CASE("autograd/yay_cuda_is_working") {
  torch::randn({100}, torch::kCUDA).sum();
}

CASE("autograd/variant/serialize") {
  auto serDeser = [&](ag::Variant varIn) -> ag::Variant {
    common::OMembuf ombuf;
    std::ostream os(&ombuf);
    {
      cereal::BinaryOutputArchive archive(os);
      EXPECT((archive(varIn), true));
    }
    os.flush();

    common::IMembuf imbuf(ombuf.data());
    std::istream is(&imbuf);
    ag::Variant varOut;
    cereal::BinaryInputArchive archive(is);
    EXPECT((archive(varOut), true));
    return varOut;
  };

  // Tensor
  {
    ag::Variant v1 = torch::arange(0, 10);
    auto v2 = serDeser(v1);
    EXPECT(v2.isTensor());
    EXPECT(v2.get().sum().item<float>() == v1.get().sum().item<float>());
  }
  // vector<Tensor>
  {
    ag::Variant v1 =
        std::vector<torch::Tensor>{torch::arange(0, 10), torch::arange(10, 20)};
    auto v2 = serDeser(v1);
    EXPECT(v2.isTensorList());
    EXPECT(
        v2.getTensorList()[0].sum().item<float>() ==
        v1.getTensorList()[0].sum().item<float>());
    EXPECT(
        v2.getTensorList()[1].sum().item<float>() ==
        v1.getTensorList()[1].sum().item<float>());
  }
  // string
  {
    ag::Variant v1 = std::string("foo");
    auto v2 = serDeser(v1);
    EXPECT(v2.isString());
    EXPECT(v1.getString() == v2.getString());
  }
  // float
  {
    ag::Variant v1 = 12.3f;
    auto v2 = serDeser(v1);
    EXPECT(v2.isFloat());
    EXPECT(v1.getFloat() == v2.getFloat());
  }
  // double
  {
    ag::Variant v1 = 12.3;
    auto v2 = serDeser(v1);
    EXPECT(v2.isDouble());
    EXPECT(v1.getDouble() == v2.getDouble());
  }
  // bool
  {
    ag::Variant v1 = true;
    auto v2 = serDeser(v1);
    EXPECT(v2.isBool());
    EXPECT(v1.getBool() == v2.getBool());
  }
  // int32_t
  {
    ag::Variant v1 = int32_t(123);
    auto v2 = serDeser(v1);
    EXPECT(v2.isInt32());
    EXPECT(v1.getInt32() == v2.getInt32());
  }
  // int64_t
  {
    ag::Variant v1 = int64_t(123);
    auto v2 = serDeser(v1);
    EXPECT(v2.isInt64());
    EXPECT(v1.getInt64() == v2.getInt64());
  }
  // vector<Variant>
  {
    ag::Variant v1 =
        std::vector<ag::Variant>{ag::Variant(torch::arange(0, 10)),
                                 ag::Variant(1.23f),
                                 ag::Variant(std::string("string")),
                                 ag::Variant(std::vector<ag::Variant>{
                                     ag::Variant(1.23), ag::Variant(true)})};
    auto v2 = serDeser(v1);
    EXPECT(v2.isList());
    EXPECT(v2.getList()[0].isTensor());
    EXPECT(v2.getList()[0].get().sum().item<int>() == 45);
    EXPECT(v2.getList()[1].isFloat());
    EXPECT(v2.getList()[1].getFloat() == 1.23f);
    EXPECT(v2.getList()[2].isString());
    EXPECT(v2.getList()[2].getString() == "string");
    EXPECT(v2.getList()[3].isList());
    EXPECT(v2.getList()[3].getList()[0].isDouble());
    EXPECT(v2.getList()[3].getList()[0].getDouble() == 1.23);
    EXPECT(v2.getList()[3].getList()[1].isBool());
    EXPECT(v2.getList()[3].getList()[1].getBool() == true);
  }
  // unordered_map<string, Variant>
  {
    ag::Variant v1 = ag::VariantDict{
        {"tensor", ag::Variant(torch::arange(0, 10))},
        {"float", ag::Variant(1.23f)},
        {"string", ag::Variant(std::string("string"))},
        {"list",
         ag::Variant(
             std::vector<ag::Variant>{ag::Variant(1.23), ag::Variant(true)})},
        {"dict",
         ag::Variant(ag::VariantDict{{"double", ag::Variant(1.23)},
                                     {"bool", ag::Variant(true)}})}};
    auto v2 = serDeser(v1);
    EXPECT(v2.isDict());
    EXPECT(v2.getDict()["tensor"].isTensor());
    EXPECT(v2.getDict()["tensor"].get().sum().item<int>() == 45);
    EXPECT(v2.getDict()["float"].isFloat());
    EXPECT(v2.getDict()["float"].getFloat() == 1.23f);
    EXPECT(v2.getDict()["string"].isString());
    EXPECT(v2.getDict()["string"].getString() == "string");
    EXPECT(v2.getDict()["list"].isList());
    EXPECT(v2.getDict()["list"].getList()[0].isDouble());
    EXPECT(v2.getDict()["list"].getList()[0].getDouble() == 1.23);
    EXPECT(v2.getDict()["list"].getList()[1].isBool());
    EXPECT(v2.getDict()["list"].getList()[1].getBool() == true);
    EXPECT(v2.getDict()["dict"].isDict());
    EXPECT(v2.getDict()["dict"].getDict()["double"].isDouble());
    EXPECT(v2.getDict()["dict"].getDict()["double"].getDouble() == 1.23);
    EXPECT(v2.getDict()["dict"].getDict()["bool"].isBool());
    EXPECT(v2.getDict()["dict"].getDict()["bool"].getBool() == true);
  }
}
// Input is (Q, K, V, mask), where mask contains the valid indices
// Q is (bsz, numQueries, queryDim)
// K is (bsz, numKeys, queryDim)
// V is (bsz, numKeys, valueDim)
// mask is (bsz, numQueries, numKeys)
// output is (bsz, numQueries, outDim)
//
CASE("autograd/mhattention") {
  for (auto rep = 0; rep < 10; rep++) {
    auto bsz = common::Rand::rand() % 5 + 2;
    int64_t qDim = common::Rand::rand() % 20 + 1;
    int64_t vDim = common::Rand::rand() % 20 + 1;
    int64_t hDim = common::Rand::rand() % 20 + 1;
    auto heads = common::Rand::rand() % 4 + 1;
    auto oDim = common::Rand::rand() % 20 + 1;
    auto module = common::MHAttention()
                      .queryDim(qDim)
                      .valueDim(vDim)
                      .hidDim(hDim)
                      .nHeads(heads)
                      .outDim(oDim)
                      .make();
    std::vector<torch::Tensor> outputs;
    std::vector<torch::Tensor> Qs;
    std::vector<torch::Tensor> Ks;
    std::vector<torch::Tensor> Vs;
    std::vector<torch::Tensor> masks;
    std::vector<int> numQueries;

    for (decltype(bsz) i = 0; i < bsz; i++) {
      int64_t nq = common::Rand::rand() % 10 + 5;
      int64_t nk = common::Rand::rand() % 10 + 5;

      Qs.emplace_back(torch::randn({nq, qDim}));
      Ks.emplace_back(torch::randn({nk, qDim}));
      Vs.emplace_back(torch::randn({nk, vDim}));
      masks.emplace_back(torch::ones({nq, nk}));
      outputs.emplace_back(module->forward({Qs.back().unsqueeze(0),
                                            Ks.back().unsqueeze(0),
                                            Vs.back().unsqueeze(0)})[0]);
      numQueries.emplace_back(nq);
    }
    auto Q = common::makeBatch(Qs, -100);
    auto K = common::makeBatch(Ks, -100);
    auto V = common::makeBatch(Vs, -100);
    auto mask = common::makeBatch(masks, 0);

    auto batch = module->forward({Q, K, V, mask})[0];
    for (decltype(bsz) i = 0; i < bsz; i++) {
      EXPECT(torch::allclose(
          batch[i].slice(0, 0, numQueries[i]), outputs[i].squeeze(0)));
    }
  }
}

CASE("autograd/cudamemory") {
#ifdef CUDA_FOUND
  EXPECT_NO_THROW(common::torchMemoryUsage());
#else // CUDA_FOUND
  EXPECT_THROWS(common::torchMemoryUsage());
#endif // CUDA_FOUND
}
