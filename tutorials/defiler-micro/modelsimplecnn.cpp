/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 */

#include "modelsimplecnn.h"
#include "common/autograd.h"
#include "features/features.h"
#include "features/unitsfeatures.h"
#include "fmt/ostream.h"
#include "model.h"
#include "prettyprint/prettyprint.hpp"
#include "utils.h"
#include <assert.h>
#include <autogradpp/autograd.h>
#include <math.h>

// Just for logging
#include "cpid/distributed.h"

DEFINE_uint64(
    simplecnn_layers_unit,
    2,
    "SimpleCNN: Number of layers in unit embedding MLP");
DEFINE_uint64(
    simplecnn_layers_conv,
    3,
    "SimpleCNN: Number of 3x3 convolutional layers");
DEFINE_uint64(
    simplecnn_layers_head,
    2,
    "SimpleCNN: Number of layers in head MLPs");
DEFINE_uint64(
    simplecnn_channels_unit,
    32,
    "SimpleCNN: Number of unit embedding channels");
DEFINE_uint64(
    simplecnn_channels_unit_hidden,
    32,
    "SimpleCNN: Number of hidden channels in unit embedding MLPs");
DEFINE_uint64(
    simplecnn_channels_conv,
    32,
    "SimpleCNN: Number of 3x3 output channels");
DEFINE_uint64(
    simplecnn_channels_head_hidden,
    32,
    "SimpleCNN: Number of hidden channels in head MLPs");
DEFINE_bool(
    simplecnn_sample_attack,
    false,
    "SimpleCNN: Softmax-sample attack targets");
DEFINE_bool(
    simplecnn_sample_move,
    true,
    "SimpleCNN: Softmax-sample move targets");
DEFINE_bool(
    simplecnn_sample_action,
    true,
    "SimpleCNN: Softmax-sample action choice");

namespace {
auto constexpr kBoundingBox = 21;
// This is for debuggin, enable with the debugging statements later
// bool kPrinted = false;
enum SimpleCNNAction { Attack = 0, Move = 1, MAX = 2 };

int64_t static constexpr mapHeight = microbattles::kMapHeight;
int64_t static constexpr mapWidth = microbattles::kMapWidth;
int64_t static constexpr featuresMap =
    microbattles::MicroFeaturizer::kMapFeatures; // Plus X, Y
int64_t static featuresUnit = microbattles::MicroFeaturizer::kNumUnitChannels;

static constexpr microbattles::BoundingBox<kBoundingBox> bounds{};
} // namespace

namespace microbattles {

using common::MLP;

int64_t scale(int64_t start, int64_t end, double layers, double layer) {
  if (layer == 0)
    return start;
  if (layer >= layers)
    return end;
  auto ratio = layer / layers;
  return start * (1.0 - ratio) + end * ratio;
}

bool shouldLog() {
  return rand() % 1000 == 0 && cpid::distributed::globalContext()->rank == 0;
}

#define quickLog(name, value)         \
  if (shouldLog()) {                  \
    VLOG(1) << name << ": " << value; \
  }
#define QLOG(x) quickLog(#x, x)
std::string tensorDimensions(torch::Tensor tensor) {
  std::string output("");
  for (auto i = 0U; i < tensor.sizes().size(); ++i) {
    output += std::to_string(tensor.sizes()[i]);
    if (i < tensor.sizes().size() - 1) {
      output += " x ";
    }
  }
  return output;
}

void logTensor(std::string name, torch::Tensor tensor) {
  if (shouldLog()) {
    VLOG(1) << name << ": " << tensorDimensions(tensor);
  }
}

void printTensor(std::string name, torch::Tensor tensor) {
  if (shouldLog()) {
    VLOG(3) << name;
    VLOG(3) << tensor;
  }
}

void logWeights(std::string name, ag::Container container) {
  auto summary = common::WeightSummary(*container);
  VLOG(2) << name << ": " << summary.toString();
}

void SimpleCNNModel::reset() {
  QLOG(featuresMap);
  QLOG(featuresUnit);

  // Embed unit features
  //
  // In: Units * (Cunit + Cmap)
  // Out: Units * Cembedded; Cembedded = FLAGS_simplecnn_channels_unit
  units_ =
      add(MLP()
              .nIn(featuresUnit)
              .nHid(FLAGS_simplecnn_channels_unit_hidden)
              .nOut(FLAGS_simplecnn_channels_unit)
              .nLayers(FLAGS_simplecnn_layers_unit)
              .make(),
          "units_");

  // Do 3x3 spatial convolution on (unit+map embedding)
  //
  // In: (Cembedded + Cembedded + Cmap) * Y * X;
  // Cembedded = FLAGS_simplecnn_channels_unit
  //
  // Out: Cconv * Y * X
  // Cconv = FLAGS_simplecnn_channels_conv * Y * X
  auto convChannelsIn = 2 * FLAGS_simplecnn_channels_unit + featuresMap;
  auto convChannelsOut = FLAGS_simplecnn_channels_conv;
  auto convChannelsTotal = 0;
  auto convLayers = FLAGS_simplecnn_layers_conv;
  convLayers_ = std::vector<ag::Container>();
  for (auto layer = 0U; layer < convLayers; ++layer) {
    auto channelsIn = scale(convChannelsIn, convChannelsOut, convLayers, layer);
    auto channelsOut =
        scale(convChannelsIn, convChannelsOut, convLayers, layer + 1);
    convChannelsTotal += channelsOut;
    quickLog("3x3 channelsIn", channelsIn);
    quickLog("3x3 channelsOut", channelsOut);
    quickLog("3x3 channelsTotal", convChannelsTotal);
    auto conv = ag::Conv2d(channelsIn, channelsOut, 3).padding(1).make();
    add(conv, "conv_" + std::to_string(layer));
    convLayers_.push_back(conv);
  }

  // "Attack" command head
  // Provides a value for each friendly unit attacking each enemy unit
  //
  // In: Units * (Cembedded (us)+ Cembedded (them) + Cconv (here) + Cconv
  // (there)
  // Out: Units * Units
  auto attackChannelsIn =
      2 * (FLAGS_simplecnn_channels_unit + convChannelsTotal);
  QLOG(attackChannelsIn);
  attacks_ =
      add(MLP()
              .nIn(attackChannelsIn)
              .nHid(FLAGS_simplecnn_channels_head_hidden)
              .nOut(1)
              .nLayers(FLAGS_simplecnn_layers_head)
              .zeroLastLayer(true)
              .make(),
          "attacks_");

  // "Move" command head
  // Provides a value for each friendly unit moving to possible positions
  //
  // In:  Units * (Cembedded + Cconv (here) + Cconv (there)
  // Out: Units * CmoveIndex; CMmveIndex = bounds.kOffset ^ 2
  auto movesChannelsIn = convChannelsTotal;
  QLOG(movesChannelsIn);
  moves_ =
      add(CONV2D()
              .nIn(movesChannelsIn)
              .nHid(FLAGS_simplecnn_channels_head_hidden)
              .nOut(1)
              .nLayers(FLAGS_simplecnn_layers_head)
              .zeroLastLayer(true)
              .make(),
          "moves_");

  // Action type head
  // Chooses between the commands like Attack and Move
  //
  // In: Units * (Cembedded + Cconv (here) + Cactions); Cactions = [Best attack
  // value, best move value]
  // Out: Units (index of action selected)
  auto actions = ag::Sequential();
  auto actionsChannelsIn =
      FLAGS_simplecnn_channels_unit + convChannelsTotal + SimpleCNNAction::MAX;
  QLOG(actionsChannelsIn);
  actions_ =
      add(MLP()
              .nIn(actionsChannelsIn)
              .nHid(FLAGS_simplecnn_channels_head_hidden)
              .nOut(SimpleCNNAction::MAX)
              .nLayers(FLAGS_simplecnn_layers_head)
              .zeroLastLayer(true)
              .make(),
          "actions_");

  if (rand() % 10 == 0 && cpid::distributed::globalContext()->rank == 0) {
#define LOGWEIGHTS(t) logWeights(#t, t)
    for (auto i = 0U; i < convLayers_.size(); ++i) {
      LOGWEIGHTS(convLayers_[i]);
    }
    LOGWEIGHTS(units_);
    LOGWEIGHTS(attacks_);
    LOGWEIGHTS(moves_);
    LOGWEIGHTS(actions_);
  }
}

ag::Variant SimpleCNNModel::forward(ag::Variant input) {
#define LOGGG(tensor) logTensor(#tensor, tensor)
#define PRINTTT(tensor) printTensor(#tensor, tensor)

  // auto coordinates = torch.arange(0,
  // mapWidth).repeat({mapHeight}).reshape({mapWidth, mapHeight});

  ag::tensor_list& inp = input.getTensorList();

  auto mapFeatures2D = inp[0].unsqueeze(0);
  auto positionsFriendly2D = inp[1];
  auto featuresFriendly = inp[2];
  auto positionsEnemy2D = inp[3];
  auto featuresEnemy = inp[4];
  auto countFriendly = positionsFriendly2D.sizes()[0];
  auto countEnemy = positionsEnemy2D.sizes()[0];

  LOGGG(mapFeatures2D);
  LOGGG(positionsFriendly2D);
  LOGGG(featuresFriendly);
  LOGGG(positionsEnemy2D);
  LOGGG(featuresEnemy);

  auto embeddedFriendly = units_->forward({featuresFriendly})[0];
  auto embeddedEnemy = units_->forward({featuresEnemy})[0];
  auto scatter = [](decltype(positionsFriendly2D) positions,
                    decltype(embeddedFriendly) embedding) {
    return common::scatterSum2d(
        positions.unsqueeze(0), embedding.unsqueeze(0), {mapHeight, mapWidth});
  };
  auto scatteredFriendly2D = scatter(positionsFriendly2D, embeddedFriendly);
  auto scatteredEnemy2D = scatter(positionsEnemy2D, embeddedEnemy);
  LOGGG(scatteredFriendly2D);
  LOGGG(scatteredEnemy2D);

  // Convolve each layer
  // Upsample and concatenate each layer's output
  std::vector<torch::Tensor> layerOutputUpsampled;
  auto convInput =
      at::cat({scatteredFriendly2D, scatteredEnemy2D, mapFeatures2D}, 1);
  for (auto layer = 0U; layer < convLayers_.size(); ++layer) {
    auto convOutput = convLayers_[layer]->forward({convInput})[0];
    convOutput = torch::max_pool2d(convOutput, 2, 2).relu();
    layerOutputUpsampled.push_back(
        at::upsample_bilinear2d(convOutput, {mapHeight, mapWidth}, false));
    convInput = convOutput;
    LOGGG(convOutput);
  }
  auto convOutput2D = at::cat(layerOutputUpsampled, 1);
  LOGGG(convOutput2D);
  auto convOutput1D = convOutput2D.view({convOutput2D.sizes()[1], -1});
  LOGGG(convOutput1D);

  // Flatten 2D orientation to 1D so we can index: I = X + Y * Width
  auto flattenPositions = [](torch::Tensor positions) {
    return at::add(positions.select(1, 1), positions.select(1, 0), mapWidth);
  };
  auto positionsFriendly1D =
      flattenPositions(positionsFriendly2D).toType(at::kLong);
  auto positionsEnemy1D = flattenPositions(positionsEnemy2D).toType(at::kLong);
  LOGGG(positionsFriendly1D);
  LOGGG(positionsEnemy1D);

  auto convOutputFriendly = convOutput1D.index_select(1, positionsFriendly1D);
  auto convOutputEnemy = convOutput1D.index_select(1, positionsEnemy1D);
  LOGGG(convOutputFriendly);
  LOGGG(convOutputEnemy);

  auto outputFriendly =
      at::cat({embeddedFriendly, convOutputFriendly.transpose(1, 0)}, 1);
  auto outputEnemy =
      at::cat({embeddedEnemy, convOutputEnemy.transpose(1, 0)}, 1);
  LOGGG(outputFriendly);
  LOGGG(outputEnemy);

  auto attackInput = at::cat(
      {outputFriendly.unsqueeze(1).expand({-1, countEnemy, -1}),
       outputEnemy.unsqueeze(0).expand({countFriendly, -1, -1})},
      2);
  LOGGG(attackInput);
  auto attackInputView = attackInput.view({countFriendly * countEnemy, -1});
  LOGGG(attackInputView);
  auto attackValues1D = attacks_->forward({attackInputView})[0];
  LOGGG(attackValues1D);
  auto attackValues = attackValues1D.view({countFriendly, countEnemy});
  LOGGG(attackValues);
  auto attackSelected = FLAGS_simplecnn_sample_attack
      ? at::multinomial(at::softmax(attackValues, 1), 1).squeeze(1)
      : std::get<1>(attackValues.max(1, false));
  LOGGG(attackSelected);
  auto attackSelectedValue =
      attackValues.gather(1, attackSelected.unsqueeze(1));
  LOGGG(attackSelectedValue);

  auto convPadded = common::padNd(
      convOutput2D.squeeze(0),
      {0, 0, bounds.kOffset, bounds.kOffset, bounds.kOffset, bounds.kOffset});
  LOGGG(convPadded);
  auto positionsFriendlyCPU = positionsFriendly2D.to(at::kCPU);
  LOGGG(positionsFriendlyCPU);
  auto positionsEnemyCPU = positionsEnemy2D.to(at::kCPU);
  LOGGG(positionsEnemyCPU);
  // Taken from PFModel
  // Get the movement planes, U x H x W x N_p
  auto moveEmbedding = [&]() {
    auto acc = positionsFriendlyCPU.accessor<int64_t, 2>();
    std::vector<at::Tensor> slices;
    slices.reserve(positionsFriendlyCPU.size(0));
    for (auto i = 0; i < acc.size(0); i++) {
      auto y = acc[i][0];
      auto x = acc[i][1];
      slices.push_back(convPadded.slice(1, y, y + 2 * bounds.kOffset + 1)
                           .slice(2, x, x + 2 * bounds.kOffset + 1));
    }
    return at::stack(slices, 0).to(FLAGS_gpu ? at::kCUDA : at::kCPU);
  }();
  LOGGG(moveEmbedding);
  if (moveEmbedding.sizes().size() < 4) {
    throw std::runtime_error(
        std::string("moveEmbedding should have been 4 dimensions but was ") +
        tensorDimensions(moveEmbedding));
  }
  auto moveValues = moves_->forward({moveEmbedding})[0].squeeze(1);
  LOGGG(moveValues);
  auto moveValuesView = moveValues.view({moveValues.size(0), -1});

  // Add noise so in the case of all-zeroes we're at least sampling
  //
  auto moveValuesViewNoised = moveValuesView +
      torch::randn(moveValuesView.sizes(), moveValuesView.options()) * 1e-5;
  LOGGG(moveValuesViewNoised);
  // Sample move actions
  auto moveSelected = FLAGS_simplecnn_sample_move
      ? at::multinomial(at::softmax(moveValuesViewNoised, 1), 1).squeeze(1)
      : std::get<1>(moveValuesViewNoised.max(1, false));
  LOGGG(moveSelected);
  auto moveSelectedValue =
      moveValuesViewNoised.gather(1, moveSelected.unsqueeze(1));

  // Assemble input to action selection network
  LOGGG(outputFriendly);
  LOGGG(attackSelectedValue);
  LOGGG(moveSelectedValue);
  auto actionValueInputs =
      at::cat({outputFriendly, attackSelectedValue, moveSelectedValue}, 1);
  LOGGG(actionValueInputs);
  auto actionValues = actions_->forward({actionValueInputs})[0];
  LOGGG(actionValues);
  auto actionSelected = FLAGS_simplecnn_sample_action
      ? at::multinomial(at::softmax(actionValues, 1), 1).squeeze(1)
      : std::get<1>(actionValues.max(1, false));
  LOGGG(actionSelected);

  auto moveWorst = FLAGS_simplecnn_sample_move
      ? at::multinomial(at::softmax(moveValuesView, 1), 1).squeeze(1)
      : std::get<1>(moveValuesView.min(1, false));
  auto moveWorstValue = moveValuesView.gather(1, moveWorst.unsqueeze(1));
  PRINTTT(attackValues);
  PRINTTT(attackSelected);
  PRINTTT(attackSelectedValue);
  PRINTTT(moveValues);
  PRINTTT(moveValuesViewNoised);
  PRINTTT(moveSelected);
  PRINTTT(moveSelectedValue);
  PRINTTT(moveWorst);
  PRINTTT(moveWorstValue);
  PRINTTT(actionValues);
  PRINTTT(actionSelected);

  // XXX This lets you visualize the model outputs occasionally
  /*
  if (std::rand() % 20000 == 0 || !kPrinted) {
    kPrinted = true;
    torch::NoGradGuard guard;
    fmt::print("Randomly printing some stats out...\n");
    std::cout << "attackValues: \n" << attackValues<< std::endl;;
    std::cout << "attackSelected: \n" << attackSelected << std::endl;;
    std::cout << "attackSelectedValue: \n" << actionValues << std::endl;;
    auto softmaxed = torch::softmax(moveValuesViewNoised.view({-1, kBoundingBox,
  kBoundingBox}), 1);
    for (auto i = 0; i < softmaxed.size(0); i++) {
      std::cout << cherrypi::utils::visualizeHeatmap(softmaxed[i]) << "\n\n";
    }
    std::cout << fmt::format("moveValuesNoised\t mean: {}\t mode: {}\t std:
  {}\n",
        moveValuesViewNoised.view({-1}).mean().item<float>(),
        std::get<0>(moveValuesViewNoised.view({-1}).mode()).item<float>(),
        moveValuesViewNoised.view({-1}).std().item<float>());
    std::cout << "moveSelected: \n" << moveSelected<< std::endl;;
    std::cout << "moveSelectedValue: \n" << moveSelectedValue<< std::endl;;
    std::cout << "actionValues: \n" << torch::softmax(actionValues, 1)<<
  std::endl;;
    std::cout << "actionSelected: \n" << actionSelected<< std::endl;;
  }
  */

  // Assemble output
  ag::tensor_list output(SimpleCNNAction::MAX + 1);
  output[SimpleCNNAction::Attack] = attackSelected;
  output[SimpleCNNAction::Move] = moveSelected;
  output[SimpleCNNAction::MAX] = actionSelected;
  return output;
}

std::vector<PFMicroActionModel::PFMicroAction> SimpleCNNModel::decodeOutput(
    cherrypi::State* state,
    ag::Variant /* input */,
    ag::Variant _output) {
  auto output = _output.getTensorList();
  auto& unitsFriendly = state->unitsInfo().myUnits();
  auto& unitsEnemy = state->unitsInfo().enemyUnits();

  // Terminate early if there are no units
  // (because otherwise we struggle with squeezed-out unit dimensions)
  if (unitsFriendly.empty() || unitsEnemy.empty()) {
    return std::vector<PFMicroActionModel::PFMicroAction>();
  }

  auto attackSelected = output[SimpleCNNAction::Attack].to(at::kCPU);
  auto moveSelected = output[SimpleCNNAction::Move].to(at::kCPU);
  auto actions = output[SimpleCNNAction::MAX].to(at::kCPU);

  LOGGG(moveSelected);
  auto attackSelectedAccessor = attackSelected.accessor<int64_t, 1>();
  auto moveSelectedAccessor = moveSelected.accessor<int64_t, 1>();
  auto actionAccessor = actions.accessor<int64_t, 1>();

  // Execute the selected action

  std::vector<PFMicroActionModel::PFMicroAction> outputActions;
  for (auto indexFriendly = 0U; indexFriendly < unitsFriendly.size();
       ++indexFriendly) {
    auto action = actionAccessor[indexFriendly];

    if (action == SimpleCNNAction::Attack) {
      outputActions.push_back(
          {PFMicroAction::Attack,
           unitsFriendly[indexFriendly],
           unitsEnemy[attackSelectedAccessor[indexFriendly]],
           cherrypi::kInvalidPosition});
    }
    if (action == SimpleCNNAction::Move) {
      auto moveIndex = moveSelectedAccessor[indexFriendly];
      auto x = unitsFriendly[indexFriendly]->x - bounds.kOffset +
          (int)moveIndex % (int)bounds.kSize;
      auto y = unitsFriendly[indexFriendly]->y - bounds.kOffset +
          (int)moveIndex / (int)bounds.kSize;
      x = cherrypi::utils::clamp(x, 0, state->mapWidth());
      y = cherrypi::utils::clamp(y, 0, state->mapWidth());
      outputActions.push_back({PFMicroAction::Move,
                               unitsFriendly[indexFriendly],
                               nullptr,
                               cherrypi::Position{x, y}});
    }
  }
  return outputActions;
}

} // namespace microbattles
