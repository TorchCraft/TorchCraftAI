# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})



FILE(GLOB COMMON
  flags.cpp
  targetingmodule.cpp
  featurize.cpp
  model.cpp
  sampler.cpp
  custombatcher.cpp
  solver.cpp
  parameters.cpp
  )

SET(BIN targeting)
ENABLE_TESTING()
ADD_EXECUTABLE(${BIN} "${BIN}.cpp" ${COMMON})

ADD_EXECUTABLE(test_targeting test.cpp ${COMMON})
ADD_TEST(NAME "test_targeting"
    COMMAND "test__targeting"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})


#INCLUDE_DIRECTORIES("${PROJECT_SOURCE_DIR}/3rdparty/or-tools/dependencies/install/include/")
#INCLUDE_DIRECTORIES("${PROJECT_SOURCE_DIR}/3rdparty/or-tools/")
#INCLUDE_DIRECTORIES("${PROJECT_SOURCE_DIR}/3rdparty/or-tools/ortools/gen/")
INCLUDE_DIRECTORIES("${PROJECT_SOURCE_DIR}/3rdparty/eigen")
INCLUDE_DIRECTORIES("${PROJECT_SOURCE_DIR}/3rdparty/include")
#INCLUDE_DIRECTORIES("${PROJECT_SOURCE_DIR}/3rdparty/or-tools2/include")
#LINK_DIRECTORIES("${PROJECT_SOURCE_DIR}/3rdparty/or-tools2/lib")

TARGET_LINK_CHERPI(${BIN})
TARGET_LINK_LIBRARIES(${BIN} gflags cpid common glop stdc++fs)

TARGET_LINK_CHERPI(test_targeting)
TARGET_LINK_LIBRARIES(test_targeting gflags cpid common glop)

TARGET_LINK_CHERPI(test_fw)
TARGET_LINK_LIBRARIES(test_fw gflags cpid common glop)
