# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR})
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})
INCLUDE_DIRECTORIES("${PROJECT_SOURCE_DIR}/3rdparty/bwapilib")

FILE(GLOB SRCS *.cpp)
ADD_LIBRARY(tcbwapi ${SRCS})
TARGET_LINK_LIBRARIES(tcbwapi bwapilib torchcraft)
SET_PROPERTY(TARGET tcbwapi PROPERTY POSITION_INDEPENDENT_CODE ON)
