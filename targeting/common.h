/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 */

#pragma once

namespace cherrypi {
enum class Targeting {
  Random,
  Random_NoChange,
  Closest,
  BuiltinAI,
  Noop,
  Weakest_Closest,
  Weakest_Closest_NOK,
  Weakest_Closest_NOK_NC,
  Weakest_Closest_NOK_smart,
  Even_Split,
  Trainer
};

enum class ModelType {
  Argmax_DM,
  Argmax_PEM,
  LP_DM,
  LP_PEM,
  Quad_DM,
  Quad_PEM,
};

inline bool isModelSpatial(ModelType t) {
  return t == ModelType::Argmax_PEM || t == ModelType::LP_PEM ||
    t == ModelType::Quad_PEM;
}


inline bool isModelQuad(ModelType t) {
  return t == ModelType::Quad_DM || t == ModelType::Quad_PEM;
}

} // namespace cherrypi
