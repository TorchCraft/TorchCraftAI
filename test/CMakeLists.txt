# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Dlest_FEATURE_AUTO_REGISTER=1")
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})

SET(TESTS core common models)
IF(WITH_CPIDLIB)
  LIST(APPEND TESTS training)
ENDIF(WITH_CPIDLIB)

FOREACH(TDIR ${TESTS})
  FILE(GLOB SRCS ${TDIR}/*.cpp)

  IF(WITH_CPIDLIB)
  ELSE()
    LIST(REMOVE_ITEM SRCS ${CMAKE_CURRENT_SOURCE_DIR}/common/agutils_t.cpp)
  ENDIF(WITH_CPIDLIB)

  ADD_LIBRARY("_test_${TDIR}" STATIC ${SRCS})
  TARGET_LINK_LIBRARIES("_test_${TDIR}" cherpi visdom Torch)
  IF(WITH_CPIDLIB)
    TARGET_LINK_LIBRARIES("_test_${TDIR}" cpid)
  ENDIF(WITH_CPIDLIB)
  ADD_EXECUTABLE("test_${TDIR}" main_test.cpp)
  # Link with --whole-archive so that tests are discoverable
  IF(APPLE)
    TARGET_LINK_LIBRARIES("test_${TDIR}" -Wl,-all_load "_test_${TDIR}")
  ELSE()
    TARGET_LINK_LIBRARIES("test_${TDIR}"
      -Wl,--whole-archive "_test_${TDIR}" -Wl,--no-whole-archive)
  ENDIF()
  TARGET_LINK_CHERPI("test_${TDIR}")
  ADD_TEST(NAME ${TDIR}
    COMMAND "test_${TDIR}"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
ENDFOREACH(TDIR)

# Copy maps to build directory for easy testing
FILE(COPY
  "${CMAKE_CURRENT_SOURCE_DIR}/maps"
  DESTINATION "${CMAKE_BINARY_DIR}/test"
  FILES_MATCHING PATTERN "*.scm")
FILE(COPY
  "${CMAKE_CURRENT_SOURCE_DIR}/maps"
  DESTINATION "${CMAKE_BINARY_DIR}/test"
  FILES_MATCHING PATTERN "*.rep")
FILE(COPY
  "${CMAKE_CURRENT_SOURCE_DIR}/battles"
  DESTINATION "${CMAKE_BINARY_DIR}/test"
  FILES_MATCHING PATTERN "*.json")
