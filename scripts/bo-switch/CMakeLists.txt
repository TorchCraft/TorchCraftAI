# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

SET(SRCS modules.cpp loops.cpp common.cpp)
ADD_LIBRARY(bos_common SHARED ${SRCS})
TARGET_LINK_CHERPI(bos_common)

FOREACH(BIN train print-game-info)
  ADD_EXECUTABLE("bos-${BIN}" "${BIN}.cpp")
  TARGET_LINK_CHERPI("bos-${BIN}")
  TARGET_LINK_LIBRARIES("bos-${BIN}" bos_common)
ENDFOREACH(BIN)
