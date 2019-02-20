/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "upc.h"

#include "state.h"
#include "utils.h"

namespace cherrypi {

std::pair<Position, float> UPCTuple::positionArgMax() const {
  return position.match(
      [&](Empty const&) { return std::make_pair(Position(-1, -1), 0.0f); },
      [&](Position const& pos) { return std::make_pair(pos * scale, 1.0f); },
      [&](Area const* ar) {
        // TODO: Account for area size in order to estimate probability??
        return std::make_pair(Position(ar->x, ar->y), 0.5f);
      },
      [&](UnitMap const&) {
        auto amax = positionUArgMax();
        return std::make_pair(Position(amax.first), amax.second);
      },
      [&](torch::Tensor const& t) {
        auto amax = utils::argmax(t, scale);
        return std::make_pair(
            Position(std::get<0>(amax), std::get<1>(amax)), std::get<2>(amax));
      });
}

std::pair<Unit*, float> UPCTuple::positionUArgMax() const {
  Unit* unit = nullptr;
  float maxP = 0.0f;
  if (position.is<UnitMap>()) {
    auto const& map = position.get_unchecked<UnitMap>();
    for (auto const& el : map) {
      if (el.second > maxP) {
        unit = el.first;
        maxP = el.second;
      }
    }
  }
  return std::make_pair(unit, maxP);
}

float UPCTuple::positionProb(int x, int y) const {
  return position.match(
      [&](Empty const&) {
        // Unspecified position: we assume that any position is good.
        return 0.5f;
      },
      [&](Position const& pos) {
        if (scale == 1) {
          if (pos.x == x && pos.y == y) {
            return 1.0f;
          } else {
            return 0.0f;
          }
        } else {
          if (pos.x == x / scale && pos.y == y / scale) {
            return 1.0f;
          } else {
            return 0.0f;
          }
        }
      },
      [&](Area const* ar) {
        if (ar->areaInfo->tryGetArea({x, y}) == ar) {
          // Use 0.5 per area to not focus on an absolute position
          return 0.5f;
        } else {
          return 0.0f;
        }
      },
      [&](UnitMap const& map) {
        for (auto& pair : map) {
          if (pair.first->x == x && pair.first->y == y) {
            return pair.second;
          }
        }
        return 0.0f;
      },
      [&](torch::Tensor const& t) {
        // TODO: Should assert this
        if (t.defined() && t.dim() == 2) {
          int sx = x / scale;
          int sy = y / scale;
          if (sx < 0 || sy < 0 || sx >= t.size(1) || sy >= t.size(0)) {
            return 0.0f;
          }
          return t[sy][sx].item<float>();
        }
        // Unspecified position: we assume that any position is good.
        return 0.5f;
      });
}

float UPCTuple::commandProb(Command c) const {
  auto it = command.find(c);
  if (it != command.end()) {
    return it->second;
  }
  return 0.;
}

torch::Tensor UPCTuple::positionTensor(State* state) const {
  auto tensor = torch::zeros({state->mapHeight(), state->mapWidth()});
  auto acc = tensor.accessor<float, 2>();

  position.match(
      [&](torch::Tensor const& t) {
        auto pacc = const_cast<torch::Tensor&>(t).accessor<float, 2>();

        // TODO Maybe this can be done more effectively?
        for (int y = 0; y < pacc.size(0); y++) {
          for (int x = 0; x < pacc.size(1); x++) {
            auto p = pacc[y][x];
            if (p <= 0.0f) {
              continue;
            }
            // Equal distribution of probabiliy over all covered tiles
            p /= scale * scale;

            for (int dy = y * scale; dy < (y + 1) * scale; y++) {
              auto row = acc[dy];
              for (int dx = x * scale; dx < (x + 1) * scale; x++) {
                row[dx] = p;
              }
            }
          }
        }
      },
      [&](UnitMap const& map) {
        for (auto& pair : map) {
          acc[pair.first->y][pair.first->x] = pair.second;
        }
      },
      [&](Position const& pos) {
        float v = 1.0f / (scale * scale);
        for (int dy = pos.y * scale; dy < (pos.y + 1) * scale; dy++) {
          auto row = acc[dy];
          for (int dx = pos.x * scale; dx < (pos.x + 1) * scale; dx++) {
            row[dx] = v;
          }
        }
      },
      [&](Area const* ar) {
        // Mark all points in the given area with a probability of one;
        // normalize afterwards
        auto& ainfo = state->areaInfo();
        for (int y = ar->topLeft.y; y < ar->bottomRight.y; y++) {
          auto row = acc[y];
          for (int x = ar->topLeft.x; x < ar->bottomRight.x; x++) {
            if (ainfo.getArea(Position({x, y})).id == ar->id) {
              row[x] = 1.0f;
            }
          }
        }
        tensor.mul_(1.0f / tensor.sum().item<float>());
      },
      [&](Empty) {
        // Uniform over map
        tensor.fill_(1.0f / (state->mapWidth() * state->mapHeight()));
      });

  return tensor;
}

std::pair<BuildType const*, float> UPCTuple::createTypeArgMax() const {
  BuildType const* type = nullptr;
  float maxP = 0.0f;
  if (state.is<BuildTypeMap>()) {
    auto const& map = state.get_unchecked<BuildTypeMap>();
    for (auto& el : map) {
      if (el.second > maxP) {
        type = el.first;
        maxP = el.second;
      }
    }
  }
  return std::make_pair(type, maxP);
}

UPCTuple::CommandT UPCTuple::uniformCommand() {
  float constexpr v = 1.0f / numUpcCommands();
  std::unordered_map<Command, float> command;
  for (std::underlying_type<Command>::type i = 1; i < Command::MAX; i <<= 1) {
    command[static_cast<Command>(i)] = v;
  }
  return command;
}

} // namespace cherrypi
