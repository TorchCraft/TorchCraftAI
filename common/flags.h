/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#define DEFINE_FLAG_OPERATORS(Type)                                          \
  inline Type operator|(Type a, Type b) {                                    \
    return static_cast<Type>(                                                \
        static_cast<std::underlying_type_t<Type>>(a) |                       \
        static_cast<std::underlying_type_t<Type>>(b));                       \
  }                                                                          \
  inline Type operator&(Type a, Type b) {                                    \
    return static_cast<Type>(                                                \
        static_cast<std::underlying_type_t<Type>>(a) &                       \
        static_cast<std::underlying_type_t<Type>>(b));                       \
  }                                                                          \
  inline Type operator~(Type a) {                                            \
    return static_cast<Type>(~static_cast<std::underlying_type_t<Type>>(a)); \
  }
